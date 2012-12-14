/*
 * pg_repack.c: bin/pg_repack.c
 *
 * Portions Copyright (c) 2008-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 * Portions Copyright (c) 2011, Itagaki Takahiro
 * Portions Copyright (c) 2012, The Reorg Development Team
 */

/**
 * @brief Client Modules
 */

const char *PROGRAM_URL		= "http://reorg.github.com/pg_repack";
const char *PROGRAM_EMAIL	= "reorg-general@lists.pgfoundry.org";

#ifdef REPACK_VERSION
/* macro trick to stringify a macro expansion */
#define xstr(s) str(s)
#define str(s) #s
const char *PROGRAM_VERSION = xstr(REPACK_VERSION);
#else
const char *PROGRAM_VERSION = "unknown";
#endif

#include "pgut/pgut-fe.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>


#ifdef HAVE_POLL_H
#include <poll.h>
#endif
#ifdef HAVE_SYS_POLL_H
#include <sys/poll.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif


/*
 * APPLY_COUNT: Number of applied logs per transaction. Larger values
 * could be faster, but will be long transactions in the REDO phase.
 */
#define APPLY_COUNT		1000

/* poll() or select() timeout, in seconds */
#define POLL_TIMEOUT    3

/* Compile an array of existing transactions which are active during
 * pg_repack's setup. Some transactions we can safely ignore:
 *  a. The '1/1, -1/0' lock skipped is from the bgwriter on newly promoted
 *     servers. See https://github.com/reorg/pg_reorg/issues/1
 *  b. Our own database connections
 *  c. Other pg_repack clients, as distinguished by application_name, which
 *     may be operating on other tables at the same time. See
 *     https://github.com/reorg/pg_repack/issues/1
 *
 * Note, there is some redundancy in how the filtering is done (e.g. excluding
 * based on pg_backend_pid() and application_name), but that shouldn't hurt
 * anything. Also, the test of application_name is not bulletproof -- for
 * instance, the application name when running installcheck will be
 * pg_regress.
 */
#define SQL_XID_SNAPSHOT_90200 \
	"SELECT repack.array_accum(l.virtualtransaction) " \
	"  FROM pg_locks AS l " \
	"  LEFT JOIN pg_stat_activity AS a " \
	"    ON l.pid = a.pid " \
	"  WHERE l.locktype = 'virtualxid' " \
	"  AND l.pid NOT IN (pg_backend_pid(), $1) " \
	"  AND (l.virtualxid, l.virtualtransaction) <> ('1/1', '-1/0') " \
	"  AND (a.application_name IS NULL OR a.application_name <> $2)"

#define SQL_XID_SNAPSHOT_90000 \
	"SELECT repack.array_accum(l.virtualtransaction) " \
	"  FROM pg_locks AS l " \
	"  LEFT JOIN pg_stat_activity AS a " \
	"    ON l.pid = a.procpid " \
	"  WHERE l.locktype = 'virtualxid' " \
	"  AND l.pid NOT IN (pg_backend_pid(), $1) " \
	"  AND (l.virtualxid, l.virtualtransaction) <> ('1/1', '-1/0') " \
	"  AND (a.application_name IS NULL OR a.application_name <> $2)"

/* application_name is not available before 9.0. The last clause of
 * the WHERE clause is just to eat the $2 parameter (application name).
 */
#define SQL_XID_SNAPSHOT_80300 \
	"SELECT repack.array_accum(virtualtransaction) FROM pg_locks" \
	" WHERE locktype = 'virtualxid' AND pid NOT IN (pg_backend_pid(), $1)" \
	" AND (virtualxid, virtualtransaction) <> ('1/1', '-1/0') " \
	" AND ($2 IS NOT NULL)"

#define SQL_XID_SNAPSHOT \
	(PQserverVersion(connection) >= 90200 ? SQL_XID_SNAPSHOT_90200 : \
	 (PQserverVersion(connection) >= 90000 ? SQL_XID_SNAPSHOT_90000 : \
	  SQL_XID_SNAPSHOT_80300))


/* Later, check whether any of the transactions we saw before are still
 * alive, and wait for them to go away.
 */
#define SQL_XID_ALIVE \
	"SELECT pid FROM pg_locks WHERE locktype = 'virtualxid'"\
	" AND pid <> pg_backend_pid() AND virtualtransaction = ANY($1)"

/* To be run while our main connection holds an AccessExclusive lock on the
 * target table, and our secondary conn is attempting to grab an AccessShare
 * lock. We know that "granted" must be false for these queries because
 * we already hold the AccessExclusive lock. Also, we only care about other
 * transactions trying to grab an ACCESS EXCLUSIVE lock, because we are only
 * trying to kill off disallowed DDL commands, e.g. ALTER TABLE or TRUNCATE.
 */
#define CANCEL_COMPETING_LOCKS \
	"SELECT pg_cancel_backend(pid) FROM pg_locks WHERE locktype = 'relation'"\
	" AND granted = false AND relation = %u"\
	" AND mode = 'AccessExclusiveLock' AND pid <> pg_backend_pid()"

#define KILL_COMPETING_LOCKS \
	"SELECT pg_terminate_backend(pid) "\
	"FROM pg_locks WHERE locktype = 'relation'"\
	" AND granted = false AND relation = %u"\
	" AND mode = 'AccessExclusiveLock' AND pid <> pg_backend_pid()"

/*
 * per-table information
 */
typedef struct repack_table
{
	const char	   *target_name;	/* target: relname */
	Oid				target_oid;		/* target: OID */
	Oid				target_toast;	/* target: toast OID */
	Oid				target_tidx;	/* target: toast index OID */
	Oid				pkid;			/* target: PK OID */
	Oid				ckid;			/* target: CK OID */
	const char	   *create_pktype;	/* CREATE TYPE pk */
	const char	   *create_log;		/* CREATE TABLE log */
	const char	   *create_trigger;	/* CREATE TRIGGER z_repack_trigger */
	const char	   *enable_trigger;	/* ALTER TABLE ENABLE ALWAYS TRIGGER z_repack_trigger */
	const char	   *create_table;	/* CREATE TABLE table AS SELECT */
	const char	   *drop_columns;	/* ALTER TABLE DROP COLUMNs */
	const char	   *delete_log;		/* DELETE FROM log */
	const char	   *lock_table;		/* LOCK TABLE table */
	const char	   *sql_peek;		/* SQL used in flush */
	const char	   *sql_insert;		/* SQL used in flush */
	const char	   *sql_delete;		/* SQL used in flush */
	const char	   *sql_update;		/* SQL used in flush */
	const char	   *sql_pop;		/* SQL used in flush */
} repack_table;


typedef enum
{
	UNPROCESSED,
	INPROGRESS,
	FINISHED
} index_status_t;

/*
 * per-index information
 */
typedef struct repack_index
{
	Oid				target_oid;		/* target: OID */
	const char	   *create_index;	/* CREATE INDEX */
    index_status_t  status; 		/* Track parallel build statuses. */
	int             worker_idx;		/* which worker conn is handling */
} repack_index;

static bool is_superuser(void);
static void repack_all_databases(const char *order_by);
static bool repack_one_database(const char *order_by, char *errbuf, size_t errsize);
static void repack_one_table(const repack_table *table, const char *order_by);
static void repack_cleanup(bool fatal, const repack_table *table);
static bool rebuild_indexes(const repack_table *table);

static char *getstr(PGresult *res, int row, int col);
static Oid getoid(PGresult *res, int row, int col);
static bool lock_exclusive(PGconn *conn, const char *relid, const char *lock_query, bool start_xact);
static bool kill_ddl(PGconn *conn, Oid relid, bool terminate);
static bool lock_access_share(PGconn *conn, Oid relid, const char *target_name);

#define SQLSTATE_INVALID_SCHEMA_NAME	"3F000"
#define SQLSTATE_QUERY_CANCELED			"57014"

static bool sqlstate_equals(PGresult *res, const char *state)
{
	return strcmp(PQresultErrorField(res, PG_DIAG_SQLSTATE), state) == 0;
}

static bool				analyze = true;
static bool				alldb = false;
static bool				noorder = false;
static SimpleStringList	table_list = {NULL, NULL};
static char				*orderby = NULL;
static int				wait_timeout = 60;	/* in seconds */
static int				jobs = 0;	/* number of concurrent worker conns. */

/* buffer should have at least 11 bytes */
static char *
utoa(unsigned int value, char *buffer)
{
	sprintf(buffer, "%u", value);
	return buffer;
}

static pgut_option options[] =
{
	{ 'b', 'a', "all", &alldb },
	{ 'l', 't', "table", &table_list },
	{ 'b', 'n', "no-order", &noorder },
	{ 's', 'o', "order-by", &orderby },
	{ 'i', 'T', "wait-timeout", &wait_timeout },
	{ 'B', 'Z', "no-analyze", &analyze },
	{ 'i', 'j', "jobs", &jobs },
	{ 0 },
};

int
main(int argc, char *argv[])
{
	int						i;

	i = pgut_getopt(argc, argv, options);

	if (i == argc - 1)
		dbname = argv[i];
	else if (i < argc)
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("too many arguments")));

	if (noorder)
		orderby = "";

	if (alldb)
	{
		if (table_list.head)
			ereport(ERROR,
				(errcode(EINVAL),
				 errmsg("cannot repack specific table(s) in all databases")));
		repack_all_databases(orderby);
	}
	else
	{
		char	errbuf[256];
		if (!repack_one_database(orderby, errbuf, sizeof(errbuf)))
			ereport(ERROR,
					(errcode(ERROR),
					 errmsg("%s", errbuf)));
	}

	return 0;
}


/*
 * Test if the current user is a database superuser.
 * Borrowed from psql/common.c
 *
 * Note: this will correctly detect superuserness only with a protocol-3.0
 * or newer backend; otherwise it will always say "false".
 */
bool
is_superuser(void)
{
	const char *val;

	if (!connection)
		return false;

	val = PQparameterStatus(connection, "is_superuser");

	if (val && strcmp(val, "on") == 0)
		return true;

	return false;
}


/*
 * Call repack_one_database for each database.
 */
static void
repack_all_databases(const char *orderby)
{
	PGresult   *result;
	int			i;

	dbname = "postgres";
	reconnect(ERROR);

	if (!is_superuser())
		elog(ERROR, "You must be a superuser to use %s", PROGRAM_NAME);

	result = execute("SELECT datname FROM pg_database WHERE datallowconn ORDER BY 1;", 0, NULL);
	disconnect();

	for (i = 0; i < PQntuples(result); i++)
	{
		bool	ret;
		char	errbuf[256];

		dbname = PQgetvalue(result, i, 0);

		if (pgut_log_level >= INFO)
		{
			printf("%s: repack database \"%s\"\n", PROGRAM_NAME, dbname);
			fflush(stdout);
		}

		ret = repack_one_database(orderby, errbuf, sizeof(errbuf));

		if (pgut_log_level >= INFO)
		{
			if (ret)
				printf("\n");
			else
				printf(" ... skipped: %s\n", errbuf);
			fflush(stdout);
		}
	}

	PQclear(result);
}

/* result is not copied */
static char *
getstr(PGresult *res, int row, int col)
{
	if (PQgetisnull(res, row, col))
		return NULL;
	else
		return PQgetvalue(res, row, col);
}

static Oid
getoid(PGresult *res, int row, int col)
{
	if (PQgetisnull(res, row, col))
		return InvalidOid;
	else
		return (Oid)strtoul(PQgetvalue(res, row, col), NULL, 10);
}

/*
 * Call repack_one_table for the target tables or each table in a database.
 */
static bool
repack_one_database(const char *orderby, char *errbuf, size_t errsize)
{
	bool					ret = false;
	PGresult	   		   *res = NULL;
	int						i;
	int						num;
	StringInfoData			sql;
	SimpleStringListCell   *cell;
	const char			  **params = NULL;
	size_t					num_params = simple_string_list_size(table_list);

	/* We need to be able to support at least two params, or more
	 * if we have multiple --tables specified.
	 */
	if (num_params && num_params > 2)
		params = pgut_malloc(num_params * sizeof(char *));
	else
		params = pgut_malloc(2 * sizeof(char *));

	initStringInfo(&sql);

	reconnect(ERROR);

	/* No sense in setting up concurrent workers if --jobs=1 */
	if (jobs > 1)
		setup_workers(jobs);

	if (!is_superuser()) {
		if (errbuf)
			snprintf(errbuf, errsize, "You must be a superuser to use %s",
					 PROGRAM_NAME);
		goto cleanup;
	}

	/* Query the extension version. Exit if no match */
	res = execute_elevel("select repack.version(), repack.version_sql()",
		0, NULL, DEBUG2);
	if (PQresultStatus(res) == PGRES_TUPLES_OK)
	{
		const char	   *libver;
		char			buf[64];

		/* the string is something like "pg_repack 1.1.7" */
		snprintf(buf, sizeof(buf), "%s %s", PROGRAM_NAME, PROGRAM_VERSION);

		/* check the version of the C library */
		libver = getstr(res, 0, 0);
		if (0 != strcmp(buf, libver))
		{
			if (errbuf)
				snprintf(errbuf, errsize,
					"program '%s' does not match database library '%s'",
					buf, libver);
			goto cleanup;
		}

		/* check the version of the SQL extension */
		libver = getstr(res, 0, 1);
		if (0 != strcmp(buf, libver))
		{
			if (errbuf)
				snprintf(errbuf, errsize,
					"extension '%s' required, found extension '%s'",
					buf, libver);
			goto cleanup;
		}
	}
	else
	{
		if (sqlstate_equals(res, SQLSTATE_INVALID_SCHEMA_NAME))
		{
			/* Schema repack does not exist. Skip the database. */
			if (errbuf)
				snprintf(errbuf, errsize,
						 "%s is not installed in the database", PROGRAM_NAME);
		}
		else
		{
			/* Return the error message otherwise */
			if (errbuf)
				snprintf(errbuf, errsize, "%s", PQerrorMessage(connection));
		}
		goto cleanup;
	}
	PQclear(res);

	/* Disable statement timeout. */
	command("SET statement_timeout = 0", 0, NULL);

	/* Restrict search_path to system catalog. */
	command("SET search_path = pg_catalog, pg_temp, public", 0, NULL);

	/* To avoid annoying "create implicit ..." messages. */
	command("SET client_min_messages = warning", 0, NULL);

	/* acquire target tables */
	appendStringInfoString(&sql, "SELECT * FROM repack.tables WHERE ");
	if (num_params)
	{
		appendStringInfoString(&sql, "(");
		for (i = 0, cell = table_list.head; cell; cell = cell->next, i++)
		{
			/* Construct table name placeholders to be used by PQexecParams */
			appendStringInfo(&sql, "relid = $%d::regclass", i + 1);
			params[i] = cell->val;
			if (cell->next)
				appendStringInfoString(&sql, " OR ");
		}
		appendStringInfoString(&sql, ")");
		res = execute_elevel(sql.data, (int) num_params, params, DEBUG2);
	}
	else
	{
		appendStringInfoString(&sql, "pkid IS NOT NULL");
		if (!orderby)
			appendStringInfoString(&sql, " AND ckid IS NOT NULL");
		res = execute_elevel(sql.data, 0, NULL, DEBUG2);
	}

	/* on error skip the database */
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		if (sqlstate_equals(res, SQLSTATE_INVALID_SCHEMA_NAME))
		{
			/* Schema repack does not exist. Skip the database. */
			if (errbuf)
				snprintf(errbuf, errsize,
						 "%s is not installed in the database", PROGRAM_NAME);
		}
		else
		{
			/* Return the error message otherwise */
			if (errbuf)
				snprintf(errbuf, errsize, "%s", PQerrorMessage(connection));
		}
		goto cleanup;
	}

	num = PQntuples(res);

	for (i = 0; i < num; i++)
	{
		repack_table	table;
		const char *create_table;
		const char *ckey;
		int			c = 0;

		table.target_name = getstr(res, i, c++);
		table.target_oid = getoid(res, i, c++);
		table.target_toast = getoid(res, i, c++);
		table.target_tidx = getoid(res, i, c++);
		table.pkid = getoid(res, i, c++);
		table.ckid = getoid(res, i, c++);

		if (table.pkid == 0) {
			ereport(WARNING,
					(errcode(E_PG_COMMAND),
					 errmsg("relation \"%s\" must have a primary key or not-null unique keys", table.target_name)));
			continue;
		}

		table.create_pktype = getstr(res, i, c++);
		table.create_log = getstr(res, i, c++);
		table.create_trigger = getstr(res, i, c++);
		table.enable_trigger = getstr(res, i, c++);

		create_table = getstr(res, i, c++);
		table.drop_columns = getstr(res, i, c++);
		table.delete_log = getstr(res, i, c++);
		table.lock_table = getstr(res, i, c++);
		ckey = getstr(res, i, c++);

		resetStringInfo(&sql);
		if (!orderby)
		{
			/* CLUSTER mode */
			if (ckey == NULL)
			{
				ereport(WARNING,
					(errcode(E_PG_COMMAND),
					 errmsg("relation \"%s\" has no cluster key", table.target_name)));
				continue;
			}
			appendStringInfo(&sql, "%s ORDER BY %s", create_table, ckey);
            table.create_table = sql.data;
		}
		else if (!orderby[0])
		{
			/* VACUUM FULL mode */
            table.create_table = create_table;
		}
		else
		{
			/* User specified ORDER BY */
			appendStringInfo(&sql, "%s ORDER BY %s", create_table, orderby);
            table.create_table = sql.data;
		}

		table.sql_peek = getstr(res, i, c++);
		table.sql_insert = getstr(res, i, c++);
		table.sql_delete = getstr(res, i, c++);
		table.sql_update = getstr(res, i, c++);
		table.sql_pop = getstr(res, i, c++);

		repack_one_table(&table, orderby);
	}
	ret = true;

cleanup:
	if (res)
		PQclear(res);
	disconnect();
	termStringInfo(&sql);
	return ret;
}

static int
apply_log(PGconn *conn, const repack_table *table, int count)
{
	int			result;
	PGresult   *res;
	const char *params[6];
	char		buffer[12];

	params[0] = table->sql_peek;
	params[1] = table->sql_insert;
	params[2] = table->sql_delete;
	params[3] = table->sql_update;
	params[4] = table->sql_pop;
	params[5] = utoa(count, buffer);

	res = pgut_execute(conn,
					   "SELECT repack.repack_apply($1, $2, $3, $4, $5, $6)",
					   6, params);
	result = atoi(PQgetvalue(res, 0, 0));
	PQclear(res);

	return result;
}

/*
 * Create indexes on temp table, possibly using multiple worker connections
 * concurrently if the user asked for --jobs=...
 */
static bool
rebuild_indexes(const repack_table *table)
{
	PGresult	   *res;
	const char	   *params[1];
	int			    num_indexes;
	int				i;
	int				num_active_workers = 0;
	repack_index   *index_jobs;
	char			buffer[12];
	bool            have_error = false;

	elog(DEBUG2, "---- create indexes ----");

	params[0] = utoa(table->target_oid, buffer);

	/* First, just display a warning message for any invalid indexes
	 * which may be on the table (mostly to match the behavior of 1.1.8).
	 */
	res = execute("SELECT pg_get_indexdef(indexrelid)"
				  " FROM pg_index WHERE indrelid = $1 AND NOT indisvalid",
				  1, params);
	for (i = 0; i < PQntuples(res); i++)
	{
		const char *indexdef;
		indexdef = getstr(res, i, 0);
		elog(WARNING, "skipping invalid index: %s", indexdef);
	}

	res = execute("SELECT indexrelid,"
		" repack.repack_indexdef(indexrelid, indrelid), "
		" pg_get_indexdef(indexrelid)"
		" FROM pg_index WHERE indrelid = $1 AND indisvalid", 1, params);

	num_indexes = PQntuples(res);
	elog(DEBUG2, "Have %d indexes and num_workers=%d", num_indexes,
		 workers.num_workers);

	index_jobs = pgut_malloc(sizeof(repack_index) * num_indexes);

	for (i = 0; i < num_indexes; i++)
	{
		int			c = 0;
		const char *indexdef;

		index_jobs[i].target_oid = getoid(res, i, c++);
		index_jobs[i].create_index = getstr(res, i, c++);
		index_jobs[i].status = UNPROCESSED;
		index_jobs[i].worker_idx = -1; /* Unassigned */

		indexdef = getstr(res, i, c++);

		elog(DEBUG2, "set up index_jobs [%d]", i);
		elog(DEBUG2, "target_oid   : %u", index_jobs[i].target_oid);
		elog(DEBUG2, "create_index : %s", index_jobs[i].create_index);

		if (workers.num_workers <= 1) {
			/* Use primary connection if we are not setting up parallel
			 * index building, or if we only have one worker.
			 */
			command(index_jobs[i].create_index, 0, NULL);

			/* This bookkeeping isn't actually important in this no-workers
			 * case, but just for clarity.
			 */
			index_jobs[i].status = FINISHED;
		}
		else if (i < workers.num_workers) {
			/* Assign available worker to build an index. */
			index_jobs[i].status = INPROGRESS;
			index_jobs[i].worker_idx = i;
			elog(DEBUG2, "Worker %d building index: %s", i,
				 index_jobs[i].create_index);

			if (!(PQsendQuery(workers.conns[i], index_jobs[i].create_index)))
			{
				elog(WARNING, "Error sending async query: %s\n%s",
					 index_jobs[i].create_index,
					 PQerrorMessage(workers.conns[i]));
				PQclear(res);
				have_error = true;
				goto cleanup;
			}
		}
		/* Else we have more indexes to be built than workers
		 * available. That's OK, we'll get to them later.
		 */
	}
	PQclear(res);

	/* How many workers we kicked off earlier. */
	num_active_workers = num_indexes > workers.num_workers ? workers.num_workers : num_indexes;

	if (workers.num_workers > 1)
	{
		int freed_worker = -1;
		int ret;

/* Prefer poll() over select(), following PostgreSQL custom. */
#ifdef HAVE_POLL
		struct pollfd *input_fds;

		input_fds = pgut_malloc(sizeof(struct pollfd) * num_active_workers);
		for (i = 0; i < num_active_workers; i++)
		{
			input_fds[i].fd = PQsocket(workers.conns[i]);
			input_fds[i].events = POLLIN | POLLERR;
			input_fds[i].revents = 0;
		}
#else
		fd_set input_mask;
		struct timeval timeout;
		/* select() needs the highest-numbered socket descriptor */
		int max_fd = 0;

		FD_ZERO(&input_mask);
		for (i = 0; i < num_active_workers; i++)
		{
			FD_SET(PQsocket(workers.conns[i]), &input_mask);
			if (PQsocket(workers.conns[i]) > max_fd)
				max_fd = PQsocket(workers.conns[i]);
		}
#endif

		/* Now go through our index builds, and look for any which is
		 * reported complete. Reassign that worker to the next index to
		 * be built, if any.
		 */
		while (num_active_workers > 0)
		{
			elog(DEBUG2, "polling %d active workers", num_active_workers);

#ifdef HAVE_POLL
			ret = poll(input_fds, num_active_workers, POLL_TIMEOUT * 1000);
#else
			/* re-initialize timeout before each invocation of select()
			 * just in case select() modifies timeout to indicate remaining
			 * time.
			 */
			timeout.tv_sec = POLL_TIMEOUT;
			timeout.tv_usec = 0;
			ret = select(max_fd + 1, &input_mask, NULL, NULL, &timeout);
#endif
			if (ret < 0 && errno != EINTR)
				elog(ERROR, "poll() failed: %d, %d", ret, errno);

			for (i = 0; i < num_indexes; i++)
			{
				if (index_jobs[i].status == INPROGRESS)
				{
					Assert(index_jobs[i].worker_idx >= 0);
					/* Must call PQconsumeInput before we can check PQisBusy */
					if (PQconsumeInput(workers.conns[index_jobs[i].worker_idx]) != 1)
					{
						elog(WARNING, "Error fetching async query status: %s",
							 PQerrorMessage(workers.conns[index_jobs[i].worker_idx]));
						have_error = true;
						goto cleanup;
					}
					if (!PQisBusy(workers.conns[index_jobs[i].worker_idx]))
					{
						elog(NOTICE, "Command finished in worker %d: %s",
							 index_jobs[i].worker_idx,
							 index_jobs[i].create_index);

						while ((res = PQgetResult(workers.conns[index_jobs[i].worker_idx])))
						{
							if (PQresultStatus(res) != PGRES_COMMAND_OK)
							{
								elog(WARNING, "Error with create index: %s",
									 PQerrorMessage(workers.conns[index_jobs[i].worker_idx]));
								PQclear(res);
								have_error = true;
								goto cleanup;
							}
							PQclear(res);
						}
						
						/* We are only going to re-queue one worker, even
						 * though more than one index build might be finished.
						 * Any other jobs which may be finished will
						 * just have to wait for the next pass through the
						 * poll()/select() loop.
						 */
						freed_worker = index_jobs[i].worker_idx;
						index_jobs[i].status = FINISHED;
						num_active_workers--;
						break;
					}
				}
			}
			if (freed_worker > -1)
			{
				for (i = 0; i < num_indexes; i++)
				{
					if (index_jobs[i].status == UNPROCESSED)
					{
						index_jobs[i].status = INPROGRESS;
						index_jobs[i].worker_idx = freed_worker;
						elog(NOTICE, "Assigning worker %d to build index #%d: "
							 "%s", freed_worker, i,
							 index_jobs[i].create_index);

						if (!(PQsendQuery(workers.conns[freed_worker],
										  index_jobs[i].create_index))) {
							elog(WARNING, "Error sending async query: %s\n%s",
								 index_jobs[i].create_index,
								 PQerrorMessage(workers.conns[freed_worker]));
							have_error = true;
							goto cleanup;
						}
						num_active_workers++;
						break;
					}
				}
				freed_worker = -1;
			}
		}

	}

cleanup:
	return (!have_error);
}


/*
 * Re-organize one table.
 */
static void
repack_one_table(const repack_table *table, const char *orderby)
{
	PGresult	   *res;
	const char	   *params[2];
	int				num;
	int				num_waiting = 0;

	char		   *vxid = NULL;
	char			buffer[12];
	StringInfoData	sql;
	bool            have_error = false;

	/* Keep track of whether we have gotten through setup to install
	 * the z_repack_trigger, log table, etc. ourselves. We don't want to
	 * go through repack_cleanup() if we didnt' actually set up the
	 * trigger ourselves, lest we be cleaning up another pg_repack's mess,
	 * or worse, interfering with a still-running pg_repack.
	 */
	bool            table_init = false;

	initStringInfo(&sql);

	elog(DEBUG2, "---- repack_one_table ----");
	elog(DEBUG2, "target_name    : %s", table->target_name);
	elog(DEBUG2, "target_oid     : %u", table->target_oid);
	elog(DEBUG2, "target_toast   : %u", table->target_toast);
	elog(DEBUG2, "target_tidx    : %u", table->target_tidx);
	elog(DEBUG2, "pkid           : %u", table->pkid);
	elog(DEBUG2, "ckid           : %u", table->ckid);
	elog(DEBUG2, "create_pktype  : %s", table->create_pktype);
	elog(DEBUG2, "create_log     : %s", table->create_log);
	elog(DEBUG2, "create_trigger : %s", table->create_trigger);
	elog(DEBUG2, "enable_trigger : %s", table->enable_trigger);
	elog(DEBUG2, "create_table   : %s", table->create_table);
	elog(DEBUG2, "drop_columns   : %s", table->drop_columns ? table->drop_columns : "(skipped)");
	elog(DEBUG2, "delete_log     : %s", table->delete_log);
	elog(DEBUG2, "lock_table     : %s", table->lock_table);
	elog(DEBUG2, "sql_peek       : %s", table->sql_peek);
	elog(DEBUG2, "sql_insert     : %s", table->sql_insert);
	elog(DEBUG2, "sql_delete     : %s", table->sql_delete);
	elog(DEBUG2, "sql_update     : %s", table->sql_update);
	elog(DEBUG2, "sql_pop        : %s", table->sql_pop);

	/*
	 * 1. Setup workspaces and a trigger.
	 */
	elog(DEBUG2, "---- setup ----");
	if (!(lock_exclusive(connection, utoa(table->target_oid, buffer), table->lock_table, TRUE)))
	{
		elog(WARNING, "lock_exclusive() failed for %s", table->target_name);
		have_error = true;
		goto cleanup;
	}

	/*
	 * Check z_repack_trigger is the trigger executed at last so that
	 * other before triggers cannot modify triggered tuples.
	 */
	params[0] = utoa(table->target_oid, buffer);

	res = execute("SELECT repack.conflicted_triggers($1)", 1, params);
	if (PQntuples(res) > 0)
	{
		ereport(WARNING,
			(errcode(E_PG_COMMAND),
			 errmsg("trigger %s conflicted for %s",
					PQgetvalue(res, 0, 0), table->target_name)));
		PQclear(res);
		have_error = true;
		goto cleanup;
	}
	PQclear(res);

	command(table->create_pktype, 0, NULL);
	command(table->create_log, 0, NULL);
	command(table->create_trigger, 0, NULL);
	command(table->enable_trigger, 0, NULL);
	printfStringInfo(&sql, "SELECT repack.disable_autovacuum('repack.log_%u')", table->target_oid);
	command(sql.data, 0, NULL);

	/* While we are still holding an AccessExclusive lock on the table, submit
	 * the request for an AccessShare lock asynchronously from conn2.
	 * We want to submit this query in conn2 while connection's
	 * transaction still holds its lock, so that no DDL may sneak in
	 * between the time that connection commits and conn2 gets its lock.
	 */
	pgut_command(conn2, "BEGIN ISOLATION LEVEL READ COMMITTED", 0, NULL);

	/* grab the backend PID of conn2; we'll need this when querying
	 * pg_locks momentarily.
	 */
	res = pgut_execute(conn2, "SELECT pg_backend_pid()", 0, NULL);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		printf("%s", PQerrorMessage(conn2));
		PQclear(res);
		have_error = true;
		goto cleanup;
	}
	buffer[0] = '\0';
	strncat(buffer, PQgetvalue(res, 0, 0), sizeof(buffer) - 1);
	PQclear(res);

	/*
	 * Not using lock_access_share() here since we know that
	 * it's not possible to obtain the ACCESS SHARE lock right now
	 * in conn2, since the primary connection holds ACCESS EXCLUSIVE.
	 */
	printfStringInfo(&sql, "LOCK TABLE %s IN ACCESS SHARE MODE",
					 table->target_name);
	elog(DEBUG2, "LOCK TABLE %s IN ACCESS SHARE MODE", table->target_name);
	if (PQsetnonblocking(conn2, 1))
	{
		elog(WARNING, "Unable to set conn2 nonblocking.");
		have_error = true;
		goto cleanup;
	}
	if (!(PQsendQuery(conn2, sql.data)))
	{
		elog(WARNING, "Error sending async query: %s\n%s", sql.data,
			 PQerrorMessage(conn2));
		have_error = true;
		goto cleanup;
	}

	/* Now that we've submitted the LOCK TABLE request through conn2,
	 * look for and cancel any (potentially dangerous) DDL commands which
	 * might also be waiting on our table lock at this point --
	 * it's not safe to let them wait, because they may grab their
	 * AccessExclusive lock before conn2 gets its AccessShare lock,
	 * and perform unsafe DDL on the table.
	 *
	 * Normally, lock_access_share() would take care of this for us,
	 * but we're not able to use it here.
	 */
	if (!(kill_ddl(connection, table->target_oid, true)))
	{
		elog(WARNING, "kill_ddl() failed.");
		have_error = true;
		goto cleanup;
	}

	/* We're finished killing off any unsafe DDL. COMMIT in our main
	 * connection, so that conn2 may get its AccessShare lock.
	 */
	command("COMMIT", 0, NULL);

	/* The main connection has now committed its z_repack_trigger,
	 * log table, and temp. table. If any error occurs from this point
	 * on and we bail out, we should try to clean those up.
	 */
	table_init = true;

	/* Keep looping PQgetResult() calls until it returns NULL, indicating the
	 * command is done and we have obtained our lock.
	 */
	while ((res = PQgetResult(conn2)))
	{
		elog(DEBUG2, "Waiting on ACCESS SHARE lock...");
		if (PQresultStatus(res) != PGRES_COMMAND_OK)
		{
			elog(WARNING, "Error with LOCK TABLE: %s", PQerrorMessage(conn2));
			PQclear(res);
			have_error = true;
			goto cleanup;
		}
		PQclear(res);
	}

	/* Turn conn2 back into blocking mode for further non-async use. */
	if (PQsetnonblocking(conn2, 0))
	{
		elog(WARNING, "Unable to set conn2 blocking.");
		have_error = true;
		goto cleanup;
	}

	/*
	 * 2. Copy tuples into temp table.
	 */
	elog(DEBUG2, "---- copy tuples ----");

	/* Must use SERIALIZABLE (or at least not READ COMMITTED) to avoid race
	 * condition between the create_table statement and rows subsequently
	 * being added to the log.
	 */
	command("BEGIN ISOLATION LEVEL SERIALIZABLE", 0, NULL);
	/* SET work_mem = maintenance_work_mem */
	command("SELECT set_config('work_mem', current_setting('maintenance_work_mem'), true)", 0, NULL);
	if (orderby && !orderby[0])
		command("SET LOCAL synchronize_seqscans = off", 0, NULL);

	/* Fetch an array of Virtual IDs of all transactions active right now.
	 */
	params[0] = buffer; /* backend PID of conn2 */
	params[1] = PROGRAM_NAME;
	res = execute(SQL_XID_SNAPSHOT, 2, params);
	if (!(vxid = strdup(PQgetvalue(res, 0, 0))))
	{
		elog(WARNING, "Unable to allocate vxid, length: %d\n",
			 PQgetlength(res, 0, 0));
		PQclear(res);
		have_error = true;
		goto cleanup;
	}
	PQclear(res);

	/* Delete any existing entries in the log table now, since we have not
	 * yet run the CREATE TABLE ... AS SELECT, which will take in all existing
	 * rows from the target table; if we also included prior rows from the
	 * log we could wind up with duplicates.
	 */
	command(table->delete_log, 0, NULL);

	/* We need to be able to obtain an AccessShare lock on the target table
	 * for the create_table command to go through, so go ahead and obtain
	 * the lock explicitly.
	 *
	 * Since conn2 has been diligently holding its AccessShare lock, it
	 * is possible that another transaction has been waiting to acquire
	 * an AccessExclusive lock on the table (e.g. a concurrent ALTER TABLE
	 * or TRUNCATE which we must not allow). If there are any such
	 * transactions, lock_access_share() will kill them so that our
	 * CREATE TABLE ... AS SELECT does not deadlock waiting for an
	 * AccessShare lock.
	 */
	if (!(lock_access_share(connection, table->target_oid, table->target_name)))
	{
		have_error = true;
		goto cleanup;
	}

	command(table->create_table, 0, NULL);
	printfStringInfo(&sql, "SELECT repack.disable_autovacuum('repack.table_%u')", table->target_oid);
	if (table->drop_columns)
		command(table->drop_columns, 0, NULL);
	command(sql.data, 0, NULL);
	command("COMMIT", 0, NULL);

	/*
	 * 3. Create indexes on temp table.
	 */
	if (!rebuild_indexes(table)) {
		have_error = true;
		goto cleanup;
	}

	/*
	 * 4. Apply log to temp table until no tuples are left in the log
	 * and all of the old transactions are finished.
	 */
	for (;;)
	{
		num = apply_log(connection, table, APPLY_COUNT);
		if (num > 0)
			continue;	/* there might be still some tuples, repeat. */

		/* old transactions still alive ? */
		params[0] = vxid;
		res = execute(SQL_XID_ALIVE, 1, params);
		num = PQntuples(res);

		if (num > 0)
		{
			/* Wait for old transactions.
			 * Only display the message below when the number of
			 * transactions we are waiting on changes (presumably,
			 * num_waiting should only go down), so as not to
			 * be too noisy.
			 */
			if (num != num_waiting)
			{
				elog(NOTICE, "Waiting for %d transactions to finish. First PID: %s", num, PQgetvalue(res, 0, 0));
				num_waiting = num;
			}

			PQclear(res);
			sleep(1);
			continue;
		}
		else
		{
			/* All old transactions are finished;
			 * go to next step. */
			PQclear(res);
			break;
		}
	}

	/*
	 * 5. Swap: will be done with conn2, since it already holds an
	 *    AccessShare lock.
	 */
	elog(DEBUG2, "---- swap ----");
	/* Bump our existing AccessShare lock to AccessExclusive */

	if (!(lock_exclusive(conn2, utoa(table->target_oid, buffer),
						 table->lock_table, FALSE)))
	{
		elog(WARNING, "lock_exclusive() failed in conn2 for %s",
			 table->target_name);
		have_error = true;
		goto cleanup;
	}

	apply_log(conn2, table, 0);
	params[0] = utoa(table->target_oid, buffer);
	pgut_command(conn2, "SELECT repack.repack_swap($1)", 1, params);
	pgut_command(conn2, "COMMIT", 0, NULL);

	/*
	 * 6. Drop.
	 */
	elog(DEBUG2, "---- drop ----");

	command("BEGIN ISOLATION LEVEL READ COMMITTED", 0, NULL);
	params[0] = utoa(table->target_oid, buffer);
	command("SELECT repack.repack_drop($1)", 1, params);
	command("COMMIT", 0, NULL);

	/*
	 * 7. Analyze.
	 * Note that cleanup hook has been already uninstalled here because analyze
	 * is not an important operation; No clean up even if failed.
	 */
	if (analyze)
	{
		elog(DEBUG2, "---- analyze ----");

		command("BEGIN ISOLATION LEVEL READ COMMITTED", 0, NULL);
		printfStringInfo(&sql, "ANALYZE %s", table->target_name);
		command(sql.data, 0, NULL);
		command("COMMIT", 0, NULL);
	}

cleanup:
	termStringInfo(&sql);
	if (vxid)
		free(vxid);

	/* XXX: distinguish between fatal and non-fatal errors via the first
	 * arg to repack_cleanup().
	 */
	if (have_error && table_init)
		repack_cleanup(false, table);
}

/* Kill off any concurrent DDL (or any transaction attempting to take
 * an AccessExclusive lock) trying to run against our table. Note, we're
 * killing these queries off *before* they are granted an AccessExclusive
 * lock on our table.
 *
 * Returns true if no problems encountered, false otherwise.
 */
static bool
kill_ddl(PGconn *conn, Oid relid, bool terminate)
{
	bool			ret = true;
	PGresult	   *res;
	StringInfoData	sql;

	initStringInfo(&sql);

	printfStringInfo(&sql, CANCEL_COMPETING_LOCKS, relid);
	res = pgut_execute(conn, sql.data, 0, NULL);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		elog(WARNING, "Error canceling unsafe queries: %s",
			 PQerrorMessage(conn));
		ret = false;
	}
	else if (PQntuples(res) > 0 && terminate && PQserverVersion(conn) >= 80400)
	{
		elog(WARNING,
			 "Canceled %d unsafe queries. Terminating any remaining PIDs.",
			 PQntuples(res));

		PQclear(res);
		printfStringInfo(&sql, KILL_COMPETING_LOCKS, relid);
		res = pgut_execute(conn, sql.data, 0, NULL);
		if (PQresultStatus(res) != PGRES_TUPLES_OK)
		{
			elog(WARNING, "Error killing unsafe queries: %s",
				 PQerrorMessage(conn));
			ret = false;
		}
	}
	else if (PQntuples(res) > 0)
		elog(NOTICE, "Canceled %d unsafe queries", PQntuples(res));
	else
		elog(DEBUG2, "No competing DDL to cancel.");

	PQclear(res);
	termStringInfo(&sql);

	return ret;
}


/*
 * Try to acquire an ACCESS SHARE table lock, avoiding deadlocks and long
 * waits by killing off other sessions which may be stuck trying to obtain
 * an ACCESS EXCLUSIVE lock.
 *
 * Arguments:
 *
 *  conn: connection to use
 *  relid: OID of relation
 *  target_name: name of table
 */
static bool
lock_access_share(PGconn *conn, Oid relid, const char *target_name)
{
	StringInfoData	sql;
	time_t			start = time(NULL);
	int				i;
	bool			ret = true;

	initStringInfo(&sql);

	for (i = 1; ; i++)
	{
		time_t		duration;
		PGresult   *res;
		int			wait_msec;

		duration = time(NULL) - start;

		/* Cancel queries unconditionally, i.e. don't bother waiting
		 * wait_timeout as lock_exclusive() does -- the only queries we
		 * should be killing are disallowed DDL commands hanging around
		 * for an AccessExclusive lock, which must be deadlocked at
		 * this point anyway since conn2 holds its AccessShare lock
		 * already.
		 */
		if (duration > (wait_timeout * 2))
			ret = kill_ddl(conn, relid, true);
		else
			ret = kill_ddl(conn, relid, false);

		if (!ret)
			break;

		/* wait for a while to lock the table. */
		wait_msec = Min(1000, i * 100);
		printfStringInfo(&sql, "SET LOCAL statement_timeout = %d", wait_msec);
		pgut_command(conn, sql.data, 0, NULL);

		printfStringInfo(&sql, "LOCK TABLE %s IN ACCESS SHARE MODE", target_name);
		res = pgut_execute_elevel(conn, sql.data, 0, NULL, DEBUG2);
		if (PQresultStatus(res) == PGRES_COMMAND_OK)
		{
			PQclear(res);
			break;
		}
		else if (sqlstate_equals(res, SQLSTATE_QUERY_CANCELED))
		{
			/* retry if lock conflicted */
			PQclear(res);
			pgut_rollback(conn);
			continue;
		}
		else
		{
			/* exit otherwise */
			elog(WARNING, "%s", PQerrorMessage(connection));
			PQclear(res);
			ret = false;
			break;
		}
	}

	termStringInfo(&sql);
	pgut_command(conn, "RESET statement_timeout", 0, NULL);
	return ret;
}


/*
 * Try acquire an ACCESS EXCLUSIVE table lock, avoiding deadlocks and long
 * waits by killing off other sessions.
 * Arguments:
 *
 *  conn: connection to use
 *  relid: OID of relation
 *  lock_query: LOCK TABLE ... IN ACCESS EXCLUSIVE query to be executed
 *  start_xact: whether we need to issue a BEGIN;
 */
static bool
lock_exclusive(PGconn *conn, const char *relid, const char *lock_query, bool start_xact)
{
	time_t		start = time(NULL);
	int			i;
	bool		ret = true;

	for (i = 1; ; i++)
	{
		time_t		duration;
		char		sql[1024];
		PGresult   *res;
		int			wait_msec;

		if (start_xact)
			pgut_command(conn, "BEGIN ISOLATION LEVEL READ COMMITTED", 0, NULL);

		duration = time(NULL) - start;
		if (duration > wait_timeout)
		{
			const char *cancel_query;
			if (PQserverVersion(conn) >= 80400 &&
				duration > wait_timeout * 2)
			{
				elog(WARNING, "terminating conflicted backends");
				cancel_query =
					"SELECT pg_terminate_backend(pid) FROM pg_locks"
					" WHERE locktype = 'relation'"
					"   AND relation = $1 AND pid <> pg_backend_pid()";
			}
			else
			{
				elog(WARNING, "canceling conflicted backends");
				cancel_query =
					"SELECT pg_cancel_backend(pid) FROM pg_locks"
					" WHERE locktype = 'relation'"
					"   AND relation = $1 AND pid <> pg_backend_pid()";
			}

			pgut_command(conn, cancel_query, 1, &relid);
		}

		/* wait for a while to lock the table. */
		wait_msec = Min(1000, i * 100);
		snprintf(sql, lengthof(sql), "SET LOCAL statement_timeout = %d", wait_msec);
		pgut_command(conn, sql, 0, NULL);

		res = pgut_execute_elevel(conn, lock_query, 0, NULL, DEBUG2);
		if (PQresultStatus(res) == PGRES_COMMAND_OK)
		{
			PQclear(res);
			break;
		}
		else if (sqlstate_equals(res, SQLSTATE_QUERY_CANCELED))
		{
			/* retry if lock conflicted */
			PQclear(res);
			pgut_rollback(conn);
			continue;
		}
		else
		{
			/* exit otherwise */
			printf("%s", PQerrorMessage(connection));
			PQclear(res);
			ret = false;
			break;
		}
	}

	pgut_command(conn, "RESET statement_timeout", 0, NULL);
	return ret;
}

/*
 * The userdata pointing a table being re-organized. We need to cleanup temp
 * objects before the program exits.
 */
static void
repack_cleanup(bool fatal, const repack_table *table)
{
	if (fatal)
	{
		fprintf(stderr, "!!!FATAL ERROR!!! Please refer to the manual.\n\n");
	}
	else
	{
		char		buffer[12];
		const char *params[1];

		/* Rollback current transactions */
		pgut_rollback(connection);
		pgut_rollback(conn2);

		/* Try reconnection if not available. */
		if (PQstatus(connection) != CONNECTION_OK ||
			PQstatus(conn2) != CONNECTION_OK)
			reconnect(ERROR);

		/* do cleanup */
		params[0] = utoa(table->target_oid, buffer);
		command("SELECT repack.repack_drop($1)", 1, params);
	}
}

void
pgut_help(bool details)
{
	printf("%s re-organizes a PostgreSQL database.\n\n", PROGRAM_NAME);
	printf("Usage:\n");
	printf("  %s [OPTION]... [DBNAME]\n", PROGRAM_NAME);

	if (!details)
		return;

	printf("Options:\n");
	printf("  -a, --all                 repack all databases\n");
	printf("  -j --jobs                 Use this many parallel jobs");
	printf("  -n, --no-order            do vacuum full instead of cluster\n");
	printf("  -o, --order-by=COLUMNS    order by columns instead of cluster keys\n");
	printf("  -t, --table=TABLE         repack specific table only\n");
	printf("  -T, --wait-timeout=SECS   timeout to cancel other backends on conflict\n");
	printf("  -Z, --no-analyze          don't analyze at end\n");
}
