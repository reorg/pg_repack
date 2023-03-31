/*
 * pg_repack.c: bin/pg_repack.c
 *
 * Portions Copyright (c) 2008-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 * Portions Copyright (c) 2011, Itagaki Takahiro
 * Portions Copyright (c) 2012-2020, The Reorg Development Team
 */

/**
 * @brief Client Modules
 */

const char *PROGRAM_URL		= "https://reorg.github.io/pg_repack/";
const char *PROGRAM_ISSUES	= "https://github.com/reorg/pg_repack/issues";

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

/* Once we get down to seeing fewer than this many tuples in the
 * log table, we'll say that we're ready to perform the switch.
 */
#define MIN_TUPLES_BEFORE_SWITCH	20

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
 *  d. open transactions/locks existing on other databases than the actual
 *     processing relation (except for locks on shared objects)
 *  e. VACUUMs which are always executed outside transaction blocks.
 *
 * Note, there is some redundancy in how the filtering is done (e.g. excluding
 * based on pg_backend_pid() and application_name), but that shouldn't hurt
 * anything. Also, the test of application_name is not bulletproof -- for
 * instance, the application name when running installcheck will be
 * pg_regress.
 */
#define SQL_XID_SNAPSHOT_90200 \
	"SELECT coalesce(array_agg(l.virtualtransaction), '{}') " \
	"  FROM pg_locks AS l " \
	"  LEFT JOIN pg_stat_activity AS a " \
	"    ON l.pid = a.pid " \
	"  LEFT JOIN pg_database AS d " \
	"    ON a.datid = d.oid " \
	"  WHERE l.locktype = 'virtualxid' " \
	"  AND l.pid NOT IN (pg_backend_pid(), $1) " \
	"  AND (l.virtualxid, l.virtualtransaction) <> ('1/1', '-1/0') " \
	"  AND (a.application_name IS NULL OR a.application_name <> $2)" \
	"  AND a.query !~* E'^\\\\s*vacuum\\\\s+' " \
	"  AND a.query !~ E'^autovacuum: ' " \
	"  AND ((d.datname IS NULL OR d.datname = current_database()) OR l.database = 0)"

#define SQL_XID_SNAPSHOT_90000 \
	"SELECT coalesce(array_agg(l.virtualtransaction), '{}') " \
	"  FROM pg_locks AS l " \
	"  LEFT JOIN pg_stat_activity AS a " \
	"    ON l.pid = a.procpid " \
	"  LEFT JOIN pg_database AS d " \
	"    ON a.datid = d.oid " \
	"  WHERE l.locktype = 'virtualxid' " \
	"  AND l.pid NOT IN (pg_backend_pid(), $1) " \
	"  AND (l.virtualxid, l.virtualtransaction) <> ('1/1', '-1/0') " \
	"  AND (a.application_name IS NULL OR a.application_name <> $2)" \
	"  AND a.current_query !~* E'^\\\\s*vacuum\\\\s+' " \
	"  AND a.current_query !~ E'^autovacuum: ' " \
	"  AND ((d.datname IS NULL OR d.datname = current_database()) OR l.database = 0)"

/* application_name is not available before 9.0. The last clause of
 * the WHERE clause is just to eat the $2 parameter (application name).
 */
#define SQL_XID_SNAPSHOT_80300 \
	"SELECT coalesce(array_agg(l.virtualtransaction), '{}') " \
	"  FROM pg_locks AS l" \
	"  LEFT JOIN pg_stat_activity AS a " \
	"    ON l.pid = a.procpid " \
	"  LEFT JOIN pg_database AS d " \
	"    ON a.datid = d.oid " \
	" WHERE l.locktype = 'virtualxid' AND l.pid NOT IN (pg_backend_pid(), $1)" \
	" AND (l.virtualxid, l.virtualtransaction) <> ('1/1', '-1/0') " \
	" AND a.current_query !~* E'^\\\\s*vacuum\\\\s+' " \
	" AND a.current_query !~ E'^autovacuum: ' " \
	" AND ((d.datname IS NULL OR d.datname = current_database()) OR l.database = 0)" \
	" AND ($2::text IS NOT NULL)"

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

#define COUNT_COMPETING_LOCKS \
	"SELECT pid FROM pg_locks WHERE locktype = 'relation'" \
	" AND granted = false AND relation = %u" \
	" AND mode = 'AccessExclusiveLock' AND pid <> pg_backend_pid()"

/* Will be used as a unique prefix for advisory locks. */
#define REPACK_LOCK_PREFIX_STR "16185446"

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
	const char	   *create_trigger;	/* CREATE TRIGGER repack_trigger */
	const char	   *enable_trigger;	/* ALTER TABLE ENABLE ALWAYS TRIGGER repack_trigger */
	const char	   *create_table;	/* CREATE TABLE table AS SELECT WITH NO DATA*/
	const char	   *copy_data;		/* INSERT INTO */
	const char	   *alter_col_storage;	/* ALTER TABLE ALTER COLUMN SET STORAGE */
	const char	   *drop_columns;	/* ALTER TABLE DROP COLUMNs */
	const char	   *delete_log;		/* DELETE FROM log */
	const char	   *lock_table;		/* LOCK TABLE table */
	const char	   *sql_peek;		/* SQL used in flush */
	const char	   *sql_insert;		/* SQL used in flush */
	const char	   *sql_delete;		/* SQL used in flush */
	const char	   *sql_update;		/* SQL used in flush */
	const char	   *sql_pop;		/* SQL used in flush */
	int             n_indexes;      /* number of indexes */
	repack_index   *indexes;        /* info on each index */
} repack_table;


static bool is_superuser(void);
static void check_tablespace(void);
static bool preliminary_checks(char *errbuf, size_t errsize);
static bool is_requested_relation_exists(char *errbuf, size_t errsize);
static void repack_all_databases(const char *order_by);
static bool repack_one_database(const char *order_by, char *errbuf, size_t errsize);
static void repack_one_table(repack_table *table, const char *order_by);
static bool repack_table_indexes(PGresult *index_details);
static bool repack_all_indexes(char *errbuf, size_t errsize);
static void repack_cleanup(bool fatal, const repack_table *table);
static void repack_cleanup_callback(bool fatal, void *userdata);
static bool rebuild_indexes(const repack_table *table);

static char *getstr(PGresult *res, int row, int col);
static Oid getoid(PGresult *res, int row, int col);
static bool advisory_lock(PGconn *conn, const char *relid);
static bool lock_exclusive(PGconn *conn, const char *relid, const char *lock_query, bool start_xact);
static bool kill_ddl(PGconn *conn, Oid relid, bool terminate);
static bool lock_access_share(PGconn *conn, Oid relid, const char *target_name);

#define SQLSTATE_INVALID_SCHEMA_NAME	"3F000"
#define SQLSTATE_UNDEFINED_FUNCTION		"42883"
#define SQLSTATE_QUERY_CANCELED			"57014"

static bool sqlstate_equals(PGresult *res, const char *state)
{
	return strcmp(PQresultErrorField(res, PG_DIAG_SQLSTATE), state) == 0;
}

static bool				analyze = true;
static bool				alldb = false;
static bool				noorder = false;
static SimpleStringList	parent_table_list = {NULL, NULL};
static SimpleStringList	table_list = {NULL, NULL};
static SimpleStringList	schema_list = {NULL, NULL};
static char				*orderby = NULL;
static char				*tablespace = NULL;
static bool				moveidx = false;
static SimpleStringList	r_index = {NULL, NULL};
static bool				only_indexes = false;
static int				wait_timeout = 60;	/* in seconds */
static int				jobs = 0;	/* number of concurrent worker conns. */
static bool				dryrun = false;
static unsigned int		temp_obj_num = 0; /* temporary objects counter */
static bool				no_kill_backend = false; /* abandon when timed-out */
static bool				no_superuser_check = false;
static SimpleStringList	exclude_extension_list = {NULL, NULL}; /* don't repack tables of these extensions */

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
	{ 'l', 'I', "parent-table", &parent_table_list },
	{ 'l', 'c', "schema", &schema_list },
	{ 'b', 'n', "no-order", &noorder },
	{ 'b', 'N', "dry-run", &dryrun },
	{ 's', 'o', "order-by", &orderby },
	{ 's', 's', "tablespace", &tablespace },
	{ 'b', 'S', "moveidx", &moveidx },
	{ 'l', 'i', "index", &r_index },
	{ 'b', 'x', "only-indexes", &only_indexes },
	{ 'i', 'T', "wait-timeout", &wait_timeout },
	{ 'B', 'Z', "no-analyze", &analyze },
	{ 'i', 'j', "jobs", &jobs },
	{ 'b', 'D', "no-kill-backend", &no_kill_backend },
	{ 'b', 'k', "no-superuser-check", &no_superuser_check },
	{ 'l', 'C', "exclude-extension", &exclude_extension_list },
	{ 0 },
};

int
main(int argc, char *argv[])
{
	int						i;
	char						errbuf[256];

	i = pgut_getopt(argc, argv, options);

	if (i == argc - 1)
		dbname = argv[i];
	else if (i < argc)
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("too many arguments")));

	check_tablespace();

	if (dryrun)
		elog(INFO, "Dry run enabled, not executing repack");

	if (r_index.head || only_indexes)
	{
		if (r_index.head && table_list.head)
			ereport(ERROR, (errcode(EINVAL),
				errmsg("cannot specify --index (-i) and --table (-t)")));
		if (r_index.head && parent_table_list.head)
			ereport(ERROR, (errcode(EINVAL),
				errmsg("cannot specify --index (-i) and --parent-table (-I)")));
		else if (r_index.head && only_indexes)
			ereport(ERROR, (errcode(EINVAL),
				errmsg("cannot specify --index (-i) and --only-indexes (-x)")));
		else if (r_index.head && exclude_extension_list.head)
			ereport(ERROR, (errcode(EINVAL),
				errmsg("cannot specify --index (-i) and --exclude-extension (-C)")));
		else if (only_indexes && !(table_list.head || parent_table_list.head))
			ereport(ERROR, (errcode(EINVAL),
				errmsg("cannot repack all indexes of database, specify the table(s)"
					   "via --table (-t) or --parent-table (-I)")));
		else if (only_indexes && exclude_extension_list.head)
			ereport(ERROR, (errcode(EINVAL),
				errmsg("cannot specify --only-indexes (-x) and --exclude-extension (-C)")));
		else if (alldb)
			ereport(ERROR, (errcode(EINVAL),
				errmsg("cannot repack specific index(es) in all databases")));
		else
		{
			if (orderby)
				ereport(WARNING, (errcode(EINVAL),
					errmsg("option -o (--order-by) has no effect while repacking indexes")));
			else if (noorder)
				ereport(WARNING, (errcode(EINVAL),
					errmsg("option -n (--no-order) has no effect while repacking indexes")));
			else if (!analyze)
				ereport(WARNING, (errcode(EINVAL),
					errmsg("ANALYZE is not performed after repacking indexes, -z (--no-analyze) has no effect")));
			else if (jobs)
				ereport(WARNING, (errcode(EINVAL),
					errmsg("option -j (--jobs) has no effect, repacking indexes does not use parallel jobs")));
			if (!repack_all_indexes(errbuf, sizeof(errbuf)))
				ereport(ERROR,
					(errcode(ERROR), errmsg("%s", errbuf)));
		}
	}
	else
	{
		if (schema_list.head && (table_list.head || parent_table_list.head))
			ereport(ERROR,
				(errcode(EINVAL),
				 errmsg("cannot repack specific table(s) in schema, use schema.table notation instead")));

		if (exclude_extension_list.head && table_list.head)
			ereport(ERROR,
				(errcode(EINVAL),
				 errmsg("cannot specify --table (-t) and --exclude-extension (-C)")));

		if (exclude_extension_list.head && parent_table_list.head)
			ereport(ERROR,
				(errcode(EINVAL),
				 errmsg("cannot specify --parent-table (-I) and --exclude-extension (-C)")));

		if (noorder)
			orderby = "";

		if (alldb)
		{
			if (table_list.head || parent_table_list.head)
				ereport(ERROR,
					(errcode(EINVAL),
					 errmsg("cannot repack specific table(s) in all databases")));
			if (schema_list.head)
				ereport(ERROR,
					(errcode(EINVAL),
					 errmsg("cannot repack specific schema(s) in all databases")));
			repack_all_databases(orderby);
		}
		else
		{
			if (!repack_one_database(orderby, errbuf, sizeof(errbuf)))
				ereport(ERROR,
					(errcode(ERROR), errmsg("%s failed with error: %s", PROGRAM_NAME, errbuf)));
		}
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

	if (no_superuser_check)
		return true;

	if (!connection)
		return false;

	val = PQparameterStatus(connection, "is_superuser");

	if (val && strcmp(val, "on") == 0)
		return true;

	return false;
}

/*
 * Check if the tablespace requested exists.
 *
 * Raise an exception on error.
 */
void
check_tablespace()
{
	PGresult		*res = NULL;
	const char *params[1];

	if (tablespace == NULL)
	{
		/* nothing to check, but let's see the options */
		if (moveidx)
		{
			ereport(ERROR,
				(errcode(EINVAL),
				 errmsg("cannot specify --moveidx (-S) without --tablespace (-s)")));
		}
		return;
	}

	/* check if the tablespace exists */
	reconnect(ERROR);
	params[0] = tablespace;
	res = execute_elevel(
		"select spcname from pg_tablespace where spcname = $1",
		1, params, DEBUG2);

	if (PQresultStatus(res) == PGRES_TUPLES_OK)
	{
		if (PQntuples(res) == 0)
		{
			ereport(ERROR,
				(errcode(EINVAL),
				 errmsg("the tablespace \"%s\" doesn't exist", tablespace)));
		}
	}
	else
	{
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("error checking the namespace: %s",
				 PQerrorMessage(connection))));
	}

	CLEARPGRES(res);
}

/*
 * Perform sanity checks before beginning work. Make sure pg_repack is
 * installed in the database, the user is a superuser, etc.
 */
static bool
preliminary_checks(char *errbuf, size_t errsize){
	bool			ret = false;
	PGresult		*res = NULL;

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
					"extension '%s' required, found '%s';"
					" please drop and re-create the extension",
					buf, libver);
			goto cleanup;
		}
	}
	else
	{
		if (sqlstate_equals(res, SQLSTATE_INVALID_SCHEMA_NAME)
			|| sqlstate_equals(res, SQLSTATE_UNDEFINED_FUNCTION))
		{
			/* Schema repack does not exist, or version too old (version
			 * functions not found). Skip the database.
			 */
			if (errbuf)
				snprintf(errbuf, errsize,
					"%s %s is not installed in the database",
					PROGRAM_NAME, PROGRAM_VERSION);
		}
		else
		{
			/* Return the error message otherwise */
			if (errbuf)
				snprintf(errbuf, errsize, "%s", PQerrorMessage(connection));
		}
		goto cleanup;
	}
	CLEARPGRES(res);

	/* Disable statement timeout. */
	command("SET statement_timeout = 0", 0, NULL);

	/* Restrict search_path to system catalog. */
	command("SET search_path = pg_catalog, pg_temp, public", 0, NULL);

	/* To avoid annoying "create implicit ..." messages. */
	command("SET client_min_messages = warning", 0, NULL);

	ret = true;

cleanup:
	CLEARPGRES(res);
	return ret;
}

/*
 * Check the presence of tables specified by --parent-table and --table
 * otherwise format user-friendly message
 */
static bool
is_requested_relation_exists(char *errbuf, size_t errsize){
	bool			ret = false;
	PGresult		*res = NULL;
	const char	    **params = NULL;
	int				iparam = 0;
	StringInfoData	sql;
	int				num_relations;
	SimpleStringListCell   *cell;

	num_relations = simple_string_list_size(parent_table_list) +
					simple_string_list_size(table_list);

	/* nothing was implicitly requested, so nothing to do here */
	if (num_relations == 0)
		return true;

	/* has no suitable to_regclass(text) */
	if (PQserverVersion(connection)<90600)
		return true;

	params = pgut_malloc(num_relations * sizeof(char *));
	initStringInfo(&sql);
	appendStringInfoString(&sql, "SELECT r FROM (VALUES ");

	for (cell = table_list.head; cell; cell = cell->next)
	{
		appendStringInfo(&sql, "($%d, 'r')", iparam + 1);
		params[iparam++] = cell->val;
		if (iparam < num_relations)
			appendStringInfoChar(&sql, ',');
	}
	for (cell = parent_table_list.head; cell; cell = cell->next)
	{
		appendStringInfo(&sql, "($%d, 'p')", iparam + 1);
		params[iparam++] = cell->val;
		if (iparam < num_relations)
			appendStringInfoChar(&sql, ',');
	}
	appendStringInfoString(&sql,
		") AS given_t(r,kind) WHERE"
		/* regular --table relation or inherited --parent-table */
		" NOT EXISTS("
		"  SELECT FROM repack.tables WHERE relid=to_regclass(given_t.r))"
		/* declarative partitioned --parent-table */
		" AND NOT EXISTS("
		"  SELECT FROM pg_catalog.pg_class c WHERE c.oid=to_regclass(given_t.r) AND c.relkind = given_t.kind AND given_t.kind = 'p')"
	);

	/* double check the parameters array is sane */
	if (iparam != num_relations)
	{
		if (errbuf)
			snprintf(errbuf, errsize,
				"internal error: bad parameters count: %i instead of %i",
				 iparam, num_relations);
		goto cleanup;
	}

	res = execute_elevel(sql.data, iparam, params, DEBUG2);
	if (PQresultStatus(res) == PGRES_TUPLES_OK)
	{
		int 	num;

		num = PQntuples(res);

		if (num != 0)
		{
			int i;
			StringInfoData	rel_names;
			initStringInfo(&rel_names);

			for (i = 0; i < num; i++)
			{
				appendStringInfo(&rel_names, "\"%s\"", getstr(res, i, 0));
				if ((i + 1) != num)
					appendStringInfoString(&rel_names, ", ");
			}

			if (errbuf)
			{
				if (num > 1)
					snprintf(errbuf, errsize,
							"relations do not exist: %s", rel_names.data);
				else
					snprintf(errbuf, errsize,
							"ERROR:  relation %s does not exist", rel_names.data);
			}
			termStringInfo(&rel_names);
		}
		else
			ret = true;
	}
	else
	{
		if (errbuf)
			snprintf(errbuf, errsize, "%s", PQerrorMessage(connection));
	}
	CLEARPGRES(res);

cleanup:
	CLEARPGRES(res);
	termStringInfo(&sql);
	free(params);
	return ret;
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

		elog(INFO, "repacking database \"%s\"", dbname);
		if (!dryrun)
		{
			ret = repack_one_database(orderby, errbuf, sizeof(errbuf));
			if (!ret)
				elog(INFO, "database \"%s\" skipped: %s", dbname, errbuf);
		}
	}

	CLEARPGRES(result);
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
	PGresult			   *res = NULL;
	int						i;
	int						num;
	StringInfoData			sql;
	SimpleStringListCell   *cell;
	const char			  **params = NULL;
	int						iparam = 0;
	size_t					num_parent_tables,
							num_tables,
							num_schemas,
							num_params,
							num_excluded_extensions;

	num_parent_tables = simple_string_list_size(parent_table_list);
	num_tables = simple_string_list_size(table_list);
	num_schemas = simple_string_list_size(schema_list);
	num_excluded_extensions = simple_string_list_size(exclude_extension_list);

	/* 1st param is the user-specified tablespace */
	num_params = num_excluded_extensions +
				 num_parent_tables +
				 num_tables +
				 num_schemas + 1;
	params = pgut_malloc(num_params * sizeof(char *));

	initStringInfo(&sql);

	reconnect(ERROR);

	/* No sense in setting up concurrent workers if --jobs=1 */
	if (jobs > 1)
		setup_workers(jobs);

	if (!preliminary_checks(errbuf, errsize))
		goto cleanup;

	if (!is_requested_relation_exists(errbuf, errsize))
		goto cleanup;

	/* acquire target tables */
	appendStringInfoString(&sql,
		"SELECT t.*,"
		" coalesce(v.tablespace, t.tablespace_orig) as tablespace_dest"
		" FROM repack.tables t, "
		" (VALUES (quote_ident($1::text))) as v (tablespace)"
		" WHERE ");

	params[iparam++] = tablespace;
	if (num_tables || num_parent_tables)
	{
		/* standalone tables */
		if (num_tables)
		{
			appendStringInfoString(&sql, "(");
			for (cell = table_list.head; cell; cell = cell->next)
			{
				/* Construct table name placeholders to be used by PQexecParams */
				appendStringInfo(&sql, "relid = $%d::regclass", iparam + 1);
				params[iparam++] = cell->val;
				if (cell->next)
					appendStringInfoString(&sql, " OR ");
			}
			appendStringInfoString(&sql, ")");
		}

		if (num_tables && num_parent_tables)
			appendStringInfoString(&sql, " OR ");

		/* parent tables + inherited children */
		if (num_parent_tables)
		{
			appendStringInfoString(&sql, "(");
			for (cell = parent_table_list.head; cell; cell = cell->next)
			{
				/* Construct table name placeholders to be used by PQexecParams */
				appendStringInfo(&sql,
								 "relid = ANY(repack.get_table_and_inheritors($%d::regclass))",
								 iparam + 1);
				params[iparam++] = cell->val;
				if (cell->next)
					appendStringInfoString(&sql, " OR ");
			}
			appendStringInfoString(&sql, ")");
		}
	}
	else if (num_schemas)
	{
		appendStringInfoString(&sql, "schemaname IN (");
		for (cell = schema_list.head; cell; cell = cell->next)
		{
			/* Construct schema name placeholders to be used by PQexecParams */
			appendStringInfo(&sql, "$%d", iparam + 1);
			params[iparam++] = cell->val;
			if (cell->next)
				appendStringInfoString(&sql, ", ");
		}
		appendStringInfoString(&sql, ")");
	}
	else
	{
		appendStringInfoString(&sql, "pkid IS NOT NULL");
	}

	/* Exclude tables which belong to extensions */
	if (exclude_extension_list.head)
	{
		appendStringInfoString(&sql, " AND t.relid NOT IN"
									 "  (SELECT d.objid::regclass"
									 "   FROM pg_depend d JOIN pg_extension e"
									 "   ON d.refobjid = e.oid"
									 "   WHERE d.classid = 'pg_class'::regclass AND (");

		/* List all excluded extensions */
		for (cell = exclude_extension_list.head; cell; cell = cell->next)
		{
			appendStringInfo(&sql, "e.extname = $%d", iparam + 1);
			params[iparam++] = cell->val;

			appendStringInfoString(&sql, cell->next ? " OR " : ")");
		}

		/* Close subquery */
		appendStringInfoString(&sql, ")");
	}

	/* Ensure the regression tests get a consistent ordering of tables */
	appendStringInfoString(&sql, " ORDER BY t.relname, t.schemaname");

	/* double check the parameters array is sane */
	if (iparam != num_params)
	{
		if (errbuf)
			snprintf(errbuf, errsize,
				"internal error: bad parameters count: %i instead of %zi",
				 iparam, num_params);
		goto cleanup;
	}

	res = execute_elevel(sql.data, (int) num_params, params, DEBUG2);

	/* on error skip the database */
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		/* Return the error message otherwise */
		if (errbuf)
			snprintf(errbuf, errsize, "%s", PQerrorMessage(connection));
		goto cleanup;
	}

	num = PQntuples(res);

	for (i = 0; i < num; i++)
	{
		repack_table	table;
		StringInfoData	copy_sql;
		const char *create_table_1;
		const char *create_table_2;
		const char *tablespace;
		const char *ckey;
		int			c = 0;

		table.target_name = getstr(res, i, c++);
		table.target_oid = getoid(res, i, c++);
		table.target_toast = getoid(res, i, c++);
		table.target_tidx = getoid(res, i, c++);
		c++; // Skip schemaname
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

		create_table_1 = getstr(res, i, c++);
		tablespace = getstr(res, i, c++);	/* to be clobbered */
		create_table_2 = getstr(res, i, c++);
		table.copy_data = getstr(res, i , c++);
		table.alter_col_storage = getstr(res, i, c++);
		table.drop_columns = getstr(res, i, c++);
		table.delete_log = getstr(res, i, c++);
		table.lock_table = getstr(res, i, c++);
		ckey = getstr(res, i, c++);
		table.sql_peek = getstr(res, i, c++);
		table.sql_insert = getstr(res, i, c++);
		table.sql_delete = getstr(res, i, c++);
		table.sql_update = getstr(res, i, c++);
		table.sql_pop = getstr(res, i, c++);
		tablespace = getstr(res, i, c++);

		/* Craft CREATE TABLE SQL */
		resetStringInfo(&sql);
		appendStringInfoString(&sql, create_table_1);
		appendStringInfoString(&sql, tablespace);
		appendStringInfoString(&sql, create_table_2);

		/* Always append WITH NO DATA to CREATE TABLE SQL*/
		appendStringInfoString(&sql, " WITH NO DATA");
		table.create_table = sql.data;

		/* Craft Copy SQL */
		initStringInfo(&copy_sql);
		appendStringInfoString(&copy_sql, table.copy_data);
		if (!orderby)

		{
			if (ckey != NULL)
			{
				/* CLUSTER mode */
				appendStringInfoString(&copy_sql, " ORDER BY ");
				appendStringInfoString(&copy_sql, ckey);
			}

			/* else, VACUUM FULL mode (non-clustered tables) */
		}
		else if (!orderby[0])
		{
			/* VACUUM FULL mode (for clustered tables too), do nothing */
		}
		else
		{
			/* User specified ORDER BY */
			appendStringInfoString(&copy_sql, " ORDER BY ");
			appendStringInfoString(&copy_sql, orderby);
		}
		table.copy_data = copy_sql.data;

		repack_one_table(&table, orderby);
	}
	ret = true;

cleanup:
	CLEARPGRES(res);
	disconnect();
	termStringInfo(&sql);
	free(params);
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
	CLEARPGRES(res);

	return result;
}

/*
 * Create indexes on temp table, possibly using multiple worker connections
 * concurrently if the user asked for --jobs=...
 */
static bool
rebuild_indexes(const repack_table *table)
{
	PGresult	   *res = NULL;
	int			    num_indexes;
	int				i;
	int				num_active_workers;
	int				num_workers;
	repack_index   *index_jobs;
	bool            have_error = false;

	elog(DEBUG2, "---- create indexes ----");

	num_indexes = table->n_indexes;

	/* We might have more actual worker connections than we need,
	 * if the number of workers exceeds the number of indexes to be
	 * built. In that case, ignore the extra workers.
	 */
	num_workers = num_indexes > workers.num_workers ? workers.num_workers : num_indexes;
	num_active_workers = num_workers;

	elog(DEBUG2, "Have %d indexes and num_workers=%d", num_indexes,
		 num_workers);

	index_jobs = table->indexes;

	for (i = 0; i < num_indexes; i++)
	{

		elog(DEBUG2, "set up index_jobs [%d]", i);
		elog(DEBUG2, "target_oid   : %u", index_jobs[i].target_oid);
		elog(DEBUG2, "create_index : %s", index_jobs[i].create_index);

		if (num_workers <= 1) {
			/* Use primary connection if we are not setting up parallel
			 * index building, or if we only have one worker.
			 */
			command(index_jobs[i].create_index, 0, NULL);

			/* This bookkeeping isn't actually important in this no-workers
			 * case, but just for clarity.
			 */
			index_jobs[i].status = FINISHED;
		}
		else if (i < num_workers) {
			/* Assign available worker to build an index. */
			index_jobs[i].status = INPROGRESS;
			index_jobs[i].worker_idx = i;
			elog(LOG, "Initial worker %d to build index: %s",
				 i, index_jobs[i].create_index);

			if (!(PQsendQuery(workers.conns[i], index_jobs[i].create_index)))
			{
				elog(WARNING, "Error sending async query: %s\n%s",
					 index_jobs[i].create_index,
					 PQerrorMessage(workers.conns[i]));
				have_error = true;
				goto cleanup;
			}
		}
		/* Else we have more indexes to be built than workers
		 * available. That's OK, we'll get to them later.
		 */
	}

	if (num_workers > 1)
	{
		int freed_worker = -1;
		int ret;

/* Prefer poll() over select(), following PostgreSQL custom. */
#ifdef HAVE_POLL
		struct pollfd *input_fds;

		input_fds = pgut_malloc(sizeof(struct pollfd) * num_workers);
		for (i = 0; i < num_workers; i++)
		{
			input_fds[i].fd = PQsocket(workers.conns[i]);
			input_fds[i].events = POLLIN | POLLERR;
			input_fds[i].revents = 0;
		}
#else
		fd_set input_mask;
		struct timeval timeout;
		/* select() needs the highest-numbered socket descriptor */
		int max_fd;
#endif

		/* Now go through our index builds, and look for any which is
		 * reported complete. Reassign that worker to the next index to
		 * be built, if any.
		 */
		while (num_active_workers > 0)
		{
			elog(DEBUG2, "polling %d active workers", num_active_workers);

#ifdef HAVE_POLL
			ret = poll(input_fds, num_workers, POLL_TIMEOUT * 1000);
#else
			/* re-initialize timeout and input_mask before each
			 * invocation of select(). I think this isn't
			 * necessary on many Unixen, but just in case.
			 */
			timeout.tv_sec = POLL_TIMEOUT;
			timeout.tv_usec = 0;

			FD_ZERO(&input_mask);
			for (i = 0, max_fd = 0; i < num_workers; i++)
			{
				FD_SET(PQsocket(workers.conns[i]), &input_mask);
				if (PQsocket(workers.conns[i]) > max_fd)
					max_fd = PQsocket(workers.conns[i]);
			}

			ret = select(max_fd + 1, &input_mask, NULL, NULL, &timeout);
#endif
			/* XXX: the errno != EINTR check means we won't bail
			 * out on SIGINT. We should probably just remove this
			 * check, though it seems we also need to fix up
			 * the on_interrupt handling for workers' index
			 * builds (those PGconns don't seem to have c->cancel
			 * set, so we don't cancel the in-progress builds).
			 */
			if (ret < 0 && errno != EINTR)
				elog(ERROR, "poll() failed: %d, %d", ret, errno);

			elog(DEBUG2, "Poll returned: %d", ret);

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
						elog(LOG, "Command finished in worker %d: %s",
							 index_jobs[i].worker_idx,
							 index_jobs[i].create_index);

						while ((res = PQgetResult(workers.conns[index_jobs[i].worker_idx])))
						{
							if (PQresultStatus(res) != PGRES_COMMAND_OK)
							{
								elog(WARNING, "Error with create index: %s",
									 PQerrorMessage(workers.conns[index_jobs[i].worker_idx]));
								have_error = true;
								goto cleanup;
							}
							CLEARPGRES(res);
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
						elog(LOG, "Assigning worker %d to build index #%d: "
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
	CLEARPGRES(res);
	return (!have_error);
}


/*
 * Re-organize one table.
 */
static void
repack_one_table(repack_table *table, const char *orderby)
{
	PGresult	   *res = NULL;
	const char	   *params[3];
	int				num;
	char		   *vxid = NULL;
	char			buffer[12];
	StringInfoData	sql;
	bool            ret = false;
	PGresult       *indexres = NULL;
	const char     *indexparams[2];
	char		    indexbuffer[12];
	int             j;

	/* appname will be "pg_repack" in normal use on 9.0+, or
	 * "pg_regress" when run under `make installcheck`
	 */
	const char     *appname = getenv("PGAPPNAME");

	/* Keep track of whether we have gotten through setup to install
	 * the repack_trigger, log table, etc. ourselves. We don't want to
	 * go through repack_cleanup() if we didn't actually set up the
	 * trigger ourselves, lest we be cleaning up another pg_repack's mess,
	 * or worse, interfering with a still-running pg_repack.
	 */
	bool            table_init = false;

	initStringInfo(&sql);

	elog(INFO, "repacking table \"%s\"", table->target_name);

	elog(DEBUG2, "---- repack_one_table ----");
	elog(DEBUG2, "target_name       : %s", table->target_name);
	elog(DEBUG2, "target_oid        : %u", table->target_oid);
	elog(DEBUG2, "target_toast      : %u", table->target_toast);
	elog(DEBUG2, "target_tidx       : %u", table->target_tidx);
	elog(DEBUG2, "pkid              : %u", table->pkid);
	elog(DEBUG2, "ckid              : %u", table->ckid);
	elog(DEBUG2, "create_pktype     : %s", table->create_pktype);
	elog(DEBUG2, "create_log        : %s", table->create_log);
	elog(DEBUG2, "create_trigger    : %s", table->create_trigger);
	elog(DEBUG2, "enable_trigger    : %s", table->enable_trigger);
	elog(DEBUG2, "create_table      : %s", table->create_table);
	elog(DEBUG2, "copy_data         : %s", table->copy_data);
	elog(DEBUG2, "alter_col_storage : %s", table->alter_col_storage ?
		 table->alter_col_storage : "(skipped)");
	elog(DEBUG2, "drop_columns      : %s", table->drop_columns ? table->drop_columns : "(skipped)");
	elog(DEBUG2, "delete_log        : %s", table->delete_log);
	elog(DEBUG2, "lock_table        : %s", table->lock_table);
	elog(DEBUG2, "sql_peek          : %s", table->sql_peek);
	elog(DEBUG2, "sql_insert        : %s", table->sql_insert);
	elog(DEBUG2, "sql_delete        : %s", table->sql_delete);
	elog(DEBUG2, "sql_update        : %s", table->sql_update);
	elog(DEBUG2, "sql_pop           : %s", table->sql_pop);

	if (dryrun)
		return;

	/* push repack_cleanup_callback() on stack to clean temporary objects */
	pgut_atexit_push(repack_cleanup_callback, table);

	/*
	 * 1. Setup advisory lock and trigger on main table.
	 */
	elog(DEBUG2, "---- setup ----");

	params[0] = utoa(table->target_oid, buffer);

	if (!advisory_lock(connection, buffer))
		goto cleanup;

	if (!(lock_exclusive(connection, buffer, table->lock_table, true)))
	{
		if (no_kill_backend)
			elog(INFO, "Skipping repack %s due to timeout", table->target_name);
		else
			elog(WARNING, "lock_exclusive() failed for %s", table->target_name);
		goto cleanup;
	}

	/*
	 * pg_get_indexdef requires an access share lock, so do those calls while
	 * we have an access exclusive lock anyway, so we know they won't block.
	 */

	indexparams[0] = utoa(table->target_oid, indexbuffer);
	indexparams[1] = moveidx ? tablespace : NULL;

	/* First, just display a warning message for any invalid indexes
	 * which may be on the table (mostly to match the behavior of 1.1.8).
	 */
	indexres = execute(
		"SELECT pg_get_indexdef(indexrelid)"
		" FROM pg_index WHERE indrelid = $1 AND NOT indisvalid",
		1, indexparams);

	for (j = 0; j < PQntuples(indexres); j++)
	{
		const char *indexdef;
		indexdef = getstr(indexres, j, 0);
		elog(WARNING, "skipping invalid index: %s", indexdef);
	}

	indexres = execute(
		"SELECT indexrelid,"
		" repack.repack_indexdef(indexrelid, indrelid, $2, FALSE) "
		" FROM pg_index WHERE indrelid = $1 AND indisvalid",
		2, indexparams);

	table->n_indexes = PQntuples(indexres);
	table->indexes = pgut_malloc(table->n_indexes * sizeof(repack_index));

	for (j = 0; j < table->n_indexes; j++)
	{
		table->indexes[j].target_oid = getoid(indexres, j, 0);
		table->indexes[j].create_index = getstr(indexres, j, 1);
		table->indexes[j].status = UNPROCESSED;
		table->indexes[j].worker_idx = -1; /* Unassigned */
	}

	for (j = 0; j < table->n_indexes; j++)
	{
		elog(DEBUG2, "index[%d].target_oid      : %u", j, table->indexes[j].target_oid);
		elog(DEBUG2, "index[%d].create_index    : %s", j, table->indexes[j].create_index);
	}


	/*
	 * Check if repack_trigger is not conflict with existing trigger. We can
	 * find it out later but we check it in advance and go to cleanup if needed.
	 * In AFTER trigger context, since triggered tuple is not changed by other
	 * trigger we don't care about the fire order.
	 */
	res = execute("SELECT repack.conflicted_triggers($1)", 1, params);
	if (PQntuples(res) > 0)
	{
		ereport(WARNING,
				(errcode(E_PG_COMMAND),
				 errmsg("the table \"%s\" already has a trigger called \"%s\"",
						table->target_name, "repack_trigger"),
				 errdetail(
					 "The trigger was probably installed during a previous"
					 " attempt to run pg_repack on the table which was"
					 " interrupted and for some reason failed to clean up"
					 " the temporary objects.  Please drop the trigger or drop"
					" and recreate the pg_repack extension altogether"
					 " to remove all the temporary objects left over.")));
		goto cleanup;
	}

	CLEARPGRES(res);

	command(table->create_pktype, 0, NULL);
	temp_obj_num++;
	command(table->create_log, 0, NULL);
	temp_obj_num++;
	command(table->create_trigger, 0, NULL);
	temp_obj_num++;
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
	buffer[0] = '\0';
	strncat(buffer, PQgetvalue(res, 0, 0), sizeof(buffer) - 1);
	CLEARPGRES(res);

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
		goto cleanup;
	}
	if (!(PQsendQuery(conn2, sql.data)))
	{
		elog(WARNING, "Error sending async query: %s\n%s", sql.data,
			 PQerrorMessage(conn2));
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
		if (no_kill_backend)
			elog(INFO, "Skipping repack %s due to timeout.", table->target_name);
		else
			elog(WARNING, "kill_ddl() failed.");
		goto cleanup;
	}

	/* We're finished killing off any unsafe DDL. COMMIT in our main
	 * connection, so that conn2 may get its AccessShare lock.
	 */
	command("COMMIT", 0, NULL);

	/* The main connection has now committed its repack_trigger,
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
			goto cleanup;
		}
		CLEARPGRES(res);
	}

	/* Turn conn2 back into blocking mode for further non-async use. */
	if (PQsetnonblocking(conn2, 0))
	{
		elog(WARNING, "Unable to set conn2 blocking.");
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
	vxid = pgut_strdup(PQgetvalue(res, 0, 0));

	CLEARPGRES(res);

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
		goto cleanup;

	/*
	 * Before copying data to the target table, we need to set the column storage
	 * type if its storage type has been changed from the type default.
	 */
	command(table->create_table, 0, NULL);
	if (table->alter_col_storage)
		command(table->alter_col_storage, 0, NULL);
	command(table->copy_data, 0, NULL);
	temp_obj_num++;
	printfStringInfo(&sql, "SELECT repack.disable_autovacuum('repack.table_%u')", table->target_oid);
	if (table->drop_columns)
		command(table->drop_columns, 0, NULL);
	command(sql.data, 0, NULL);
	command("COMMIT", 0, NULL);

	/*
	 * 3. Create indexes on temp table.
	 */
	if (!rebuild_indexes(table))
		goto cleanup;

	/* don't clear indexres until after rebuild_indexes or bad things happen */
	CLEARPGRES(indexres);
	CLEARPGRES(res);

	/*
	 * 4. Apply log to temp table until no tuples are left in the log
	 * and all of the old transactions are finished.
	 */
	for (;;)
	{
		num = apply_log(connection, table, APPLY_COUNT);

		/* We'll keep applying tuples from the log table in batches
		 * of APPLY_COUNT, until applying a batch of tuples
		 * (via LIMIT) results in our having applied
		 * MIN_TUPLES_BEFORE_SWITCH or fewer tuples. We don't want to
		 * get stuck repetitively applying some small number of tuples
		 * from the log table as inserts/updates/deletes may be
		 * constantly coming into the original table.
		 */
		if (num > MIN_TUPLES_BEFORE_SWITCH)
			continue;	/* there might be still some tuples, repeat. */

		/* old transactions still alive ? */
		params[0] = vxid;
		res = execute(SQL_XID_ALIVE, 1, params);
		num = PQntuples(res);

		if (num > 0)
		{
			/* Wait for old transactions.
			 * Only display this message if we are NOT
			 * running under pg_regress, so as not to cause
			 * noise which would trip up pg_regress.
			 */

			if (!appname || strcmp(appname, "pg_regress") != 0)
			{
				elog(NOTICE, "Waiting for %d transactions to finish. First PID: %s", num, PQgetvalue(res, 0, 0));
			}

			CLEARPGRES(res);
			sleep(1);
			continue;
		}
		else
		{
			/* All old transactions are finished;
			 * go to next step. */
			CLEARPGRES(res);
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
						 table->lock_table, false)))
	{
		elog(WARNING, "lock_exclusive() failed in conn2 for %s",
			 table->target_name);
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
	if (!(lock_exclusive(connection, utoa(table->target_oid, buffer),
						 table->lock_table, false)))
	{
		elog(WARNING, "lock_exclusive() failed in connection for %s",
			 table->target_name);
		goto cleanup;
	}

	params[1] = utoa(temp_obj_num, indexbuffer);
	command("SELECT repack.repack_drop($1, $2)", 2, params);
	command("COMMIT", 0, NULL);
	temp_obj_num = 0; /* reset temporary object counter after cleanup */

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

	/* Release advisory lock on table. */
	params[0] = REPACK_LOCK_PREFIX_STR;
	params[1] = utoa(table->target_oid, buffer);

	res = pgut_execute(connection, "SELECT pg_advisory_unlock($1, CAST(-2147483648 + $2::bigint AS integer))",
			   2, params);
	ret = true;

cleanup:
	CLEARPGRES(res);
	termStringInfo(&sql);
	if (vxid)
		free(vxid);

	/* Rollback current transactions */
	pgut_rollback(connection);
	pgut_rollback(conn2);

	/* XXX: distinguish between fatal and non-fatal errors via the first
	 * arg to repack_cleanup().
	 */
	if ((!ret) && table_init)
		repack_cleanup(false, table);
}

/* Kill off any concurrent DDL (or any transaction attempting to take
 * an AccessExclusive lock) trying to run against our table if we want to
 * do. Note, we're killing these queries off *before* they are granted
 * an AccessExclusive lock on our table.
 *
 * Returns true if no problems encountered, false otherwise.
 */
static bool
kill_ddl(PGconn *conn, Oid relid, bool terminate)
{
	bool			ret = true;
	PGresult	   *res;
	StringInfoData	sql;
	int				n_tuples;

	initStringInfo(&sql);

	/* Check the number of backends competing AccessExclusiveLock */
	printfStringInfo(&sql, COUNT_COMPETING_LOCKS, relid);
	res = pgut_execute(conn, sql.data, 0, NULL);
	n_tuples = PQntuples(res);

	if (n_tuples != 0)
	{
		/* Competing backend is exsits, but if we do not want to calcel/terminate
		 * any backend, do nothing.
		 */
		if (no_kill_backend)
		{
			elog(WARNING, "%d unsafe queries remain but do not cancel them and skip to repack it",
				 n_tuples);
			ret = false;
		}
		else
		{
			resetStringInfo(&sql);
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

				CLEARPGRES(res);
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
		}
	}
	else
		elog(DEBUG2, "No competing DDL to cancel.");

	CLEARPGRES(res);
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
			CLEARPGRES(res);
			break;
		}
		else if (sqlstate_equals(res, SQLSTATE_QUERY_CANCELED))
		{
			/* retry if lock conflicted */
			CLEARPGRES(res);
			pgut_rollback(conn);
			continue;
		}
		else
		{
			/* exit otherwise */
			elog(WARNING, "%s", PQerrorMessage(connection));
			CLEARPGRES(res);
			ret = false;
			break;
		}
	}

	termStringInfo(&sql);
	pgut_command(conn, "RESET statement_timeout", 0, NULL);
	return ret;
}


/* Obtain an advisory lock on the table's OID, to make sure no other
 * pg_repack is working on the table. This is not so much a concern with
 * full-table repacks, but mainly so that index-only repacks don't interfere
 * with each other or a full-table repack.
 */
static bool advisory_lock(PGconn *conn, const char *relid)
{
	PGresult	   *res = NULL;
	bool			ret = false;
	const char	   *params[2];

	params[0] = REPACK_LOCK_PREFIX_STR;
	params[1] = relid;

	/* For the 2-argument form of pg_try_advisory_lock, we need to
	 * pass in two signed 4-byte integers. But a table OID is an
	 * *unsigned* 4-byte integer. Add -2147483648 to that OID to make
	 * it fit reliably into signed int space.
	 */
	res = pgut_execute(conn, "SELECT pg_try_advisory_lock($1, CAST(-2147483648 + $2::bigint AS integer))",
			   2, params);

	if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		elog(ERROR, "%s",  PQerrorMessage(connection));
	}
	else if (strcmp(getstr(res, 0, 0), "t") != 0) {
		elog(ERROR, "Another pg_repack command may be running on the table. Please try again later.");
	}
	else {
		ret = true;
	}
	CLEARPGRES(res);
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
 *  start_xact: whether we will issue a BEGIN ourselves. If not, we will
 *              use a SAVEPOINT and ROLLBACK TO SAVEPOINT if our query
 *              times out, to avoid leaving the transaction in error state.
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
		else
			pgut_command(conn, "SAVEPOINT repack_sp1", 0, NULL);

		duration = time(NULL) - start;
		if (duration > wait_timeout)
		{
			if (no_kill_backend)
			{
				elog(WARNING, "timed out, do not cancel conflicting backends");
				ret = false;

				/* Before exit the loop reset the transaction */
				if (start_xact)
					pgut_rollback(conn);
				else
					pgut_command(conn, "ROLLBACK TO SAVEPOINT repack_sp1", 0, NULL);
				break;
			}
			else
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
		}

		/* wait for a while to lock the table. */
		wait_msec = Min(1000, i * 100);
		snprintf(sql, lengthof(sql), "SET LOCAL statement_timeout = %d", wait_msec);
		pgut_command(conn, sql, 0, NULL);

		res = pgut_execute_elevel(conn, lock_query, 0, NULL, DEBUG2);
		if (PQresultStatus(res) == PGRES_COMMAND_OK)
		{
			CLEARPGRES(res);
			break;
		}
		else if (sqlstate_equals(res, SQLSTATE_QUERY_CANCELED))
		{
			/* retry if lock conflicted */
			CLEARPGRES(res);
			if (start_xact)
				pgut_rollback(conn);
			else
				pgut_command(conn, "ROLLBACK TO SAVEPOINT repack_sp1", 0, NULL);
			continue;
		}
		else
		{
			/* exit otherwise */
			printf("%s", PQerrorMessage(connection));
			CLEARPGRES(res);
			ret = false;
			break;
		}
	}

	pgut_command(conn, "RESET statement_timeout", 0, NULL);
	return ret;
}

/* This function calls to repack_drop() to clean temporary objects on error
 * in creation of temporary objects.
 */
void
repack_cleanup_callback(bool fatal, void *userdata)
{
	repack_table *table = (repack_table *) userdata;
	Oid			target_table = table->target_oid;
	const char *params[2];
	char		buffer[12];
	char		num_buff[12];

	if(fatal)
	{
		params[0] = utoa(target_table, buffer);
		params[1] = utoa(temp_obj_num, num_buff);

		/* testing PQstatus() of connection and conn2, as we do
		 * in repack_cleanup(), doesn't seem to work here,
		 * so just use an unconditional reconnect().
		 */
		reconnect(ERROR);

		command("BEGIN ISOLATION LEVEL READ COMMITTED", 0, NULL);
		if (!(lock_exclusive(connection, params[0], table->lock_table, false)))
		{
			pgut_rollback(connection);
			elog(ERROR, "lock_exclusive() failed in connection for %s during cleanup callback",
				 table->target_name);
		}

		command("SELECT repack.repack_drop($1, $2)", 2, params);
		command("COMMIT", 0, NULL);
		temp_obj_num = 0; /* reset temporary object counter after cleanup */
	}
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
		char		num_buff[12];
		const char *params[2];

		/* Try reconnection if not available. */
		if (PQstatus(connection) != CONNECTION_OK ||
			PQstatus(conn2) != CONNECTION_OK)
			reconnect(ERROR);

		/* do cleanup */
		params[0] = utoa(table->target_oid, buffer);
		params[1] =  utoa(temp_obj_num, num_buff);

		command("BEGIN ISOLATION LEVEL READ COMMITTED", 0, NULL);
		if (!(lock_exclusive(connection, params[0], table->lock_table, false)))
		{
			pgut_rollback(connection);
			elog(ERROR, "lock_exclusive() failed in connection for %s during cleanup",
				 table->target_name);
		}

		command("SELECT repack.repack_drop($1, $2)", 2, params);
		command("COMMIT", 0, NULL);
		temp_obj_num = 0; /* reset temporary object counter after cleanup */
	}
}

/*
 * Indexes of a table are repacked.
 */
static bool
repack_table_indexes(PGresult *index_details)
{
	bool				ret = false;
	PGresult			*res = NULL, *res2 = NULL;
	StringInfoData		sql, sql_drop;
	char				buffer[2][12];
	const char			*create_idx, *schema_name, *table_name, *params[3];
	Oid					table, index;
	int					i, num, num_repacked = 0;
	bool                *repacked_indexes;

	initStringInfo(&sql);

	num = PQntuples(index_details);
	table = getoid(index_details, 0, 3);
	params[1] = utoa(table, buffer[1]);
	params[2] = tablespace;
	schema_name = getstr(index_details, 0, 5);
	/* table_name is schema-qualified */
	table_name = getstr(index_details, 0, 4);

	/* Keep track of which of the table's indexes we have successfully
	 * repacked, so that we may DROP only those indexes.
	 */
	if (!(repacked_indexes = calloc(num, sizeof(bool))))
		ereport(ERROR, (errcode(ENOMEM),
						errmsg("Unable to calloc repacked_indexes")));

	/* Check if any concurrent pg_repack command is being run on the same
	 * table.
	 */
	if (!advisory_lock(connection, params[1]))
		ereport(ERROR, (errcode(EINVAL),
			errmsg("Unable to obtain advisory lock on \"%s\"", table_name)));

	for (i = 0; i < num; i++)
	{
		char *isvalid = getstr(index_details, i, 2);
		char *idx_name = getstr(index_details, i, 0);

		if (isvalid[0] == 't')
		{
			index = getoid(index_details, i, 1);

			resetStringInfo(&sql);
			appendStringInfo(&sql, "SELECT pgc.relname, nsp.nspname "
							 "FROM pg_class pgc INNER JOIN pg_namespace nsp "
							 "ON nsp.oid = pgc.relnamespace "
							 "WHERE pgc.relname = 'index_%u' "
							 "AND nsp.nspname = $1", index);
			params[0] = schema_name;
			elog(INFO, "repacking index \"%s\"", idx_name);
			res = execute(sql.data, 1, params);
			if (PQresultStatus(res) != PGRES_TUPLES_OK)
			{
				elog(WARNING, "%s", PQerrorMessage(connection));
				continue;
			}
			if (PQntuples(res) > 0)
			{
				ereport(WARNING,
						(errcode(E_PG_COMMAND),
						 errmsg("Cannot create index \"%s\".\"index_%u\", "
								"already exists", schema_name, index),
						 errdetail("An invalid index may have been left behind"
								   " by a previous pg_repack on the table"
								   " which was interrupted. Please use DROP "
								   "INDEX \"%s\".\"index_%u\""
								   " to remove this index and try again.",
								   schema_name, index)));
				continue;
			}

			if (dryrun)
				continue;

			params[0] = utoa(index, buffer[0]);
			res = execute("SELECT repack.repack_indexdef($1, $2, $3, true)", 3,
						  params);

			if (PQntuples(res) < 1)
			{
				elog(WARNING,
					"unable to generate SQL to CREATE work index for %s",
					getstr(index_details, i, 0));
				continue;
			}

			create_idx = getstr(res, 0, 0);
			/* Use a separate PGresult to avoid stomping on create_idx */
			res2 = execute_elevel(create_idx, 0, NULL, DEBUG2);

			if (PQresultStatus(res2) != PGRES_COMMAND_OK)
			{
				ereport(WARNING,
						(errcode(E_PG_COMMAND),
						 errmsg("Error creating index \"%s\".\"index_%u\": %s",
								schema_name, index, PQerrorMessage(connection)
							 ) ));
			}
			else
			{
				repacked_indexes[i] = true;
				num_repacked++;
			}

			CLEARPGRES(res);
			CLEARPGRES(res2);
		}
		else
			elog(WARNING, "skipping invalid index: %s.%s", schema_name,
				 getstr(index_details, i, 0));
	}

	if (dryrun) {
		ret = true;
		goto done;
	}

	/* If we did not successfully repack any indexes, e.g. because of some
	 * error affecting every CREATE INDEX attempt, don't waste time with
	 * the ACCESS EXCLUSIVE lock on the table, and return false.
	 * N.B. none of the DROP INDEXes should be performed since
	 * repacked_indexes[] flags should all be false.
	 */
	if (!num_repacked)
	{
		elog(WARNING,
			 "Skipping index swapping for \"%s\", since no new indexes built",
			 table_name);
		goto drop_idx;
	}

	/* take an exclusive lock on table before calling repack_index_swap() */
	resetStringInfo(&sql);
	appendStringInfo(&sql, "LOCK TABLE %s IN ACCESS EXCLUSIVE MODE",
					 table_name);
	if (!(lock_exclusive(connection, params[1], sql.data, true)))
	{
		elog(WARNING, "lock_exclusive() failed in connection for %s",
			 table_name);
		goto drop_idx;
	}

	for (i = 0; i < num; i++)
	{
		index = getoid(index_details, i, 1);
		if (repacked_indexes[i])
		{
			params[0] = utoa(index, buffer[0]);
			pgut_command(connection, "SELECT repack.repack_index_swap($1)", 1,
						 params);
		}
		else
			elog(INFO, "Skipping index swap for index_%u", index);
	}
	pgut_command(connection, "COMMIT", 0, NULL);
	ret = true;

drop_idx:
	resetStringInfo(&sql);
	initStringInfo(&sql_drop);
	appendStringInfoString(&sql, "DROP INDEX CONCURRENTLY ");
	appendStringInfo(&sql, "\"%s\".",  schema_name);

	for (i = 0; i < num; i++)
	{
		index = getoid(index_details, i, 1);
		if (repacked_indexes[i])
		{
			initStringInfo(&sql_drop);
			appendStringInfo(&sql_drop, "%s\"index_%u\"", sql.data, index);
			command(sql_drop.data, 0, NULL);
		}
		else
			elog(INFO, "Skipping drop of index_%u", index);
	}
	termStringInfo(&sql_drop);
	termStringInfo(&sql);

done:
	CLEARPGRES(res);
	free(repacked_indexes);

	return ret;
}

/*
 * Call repack_table_indexes for each of the tables
 */
static bool
repack_all_indexes(char *errbuf, size_t errsize)
{
	bool					ret = false;
	PGresult				*res = NULL;
	StringInfoData			sql;
	SimpleStringListCell	*cell = NULL;
	const char				*params[1];

	initStringInfo(&sql);
	reconnect(ERROR);

	assert(r_index.head || table_list.head || parent_table_list.head);

	if (!preliminary_checks(errbuf, errsize))
		goto cleanup;

	if (!is_requested_relation_exists(errbuf, errsize))
		goto cleanup;

	if (r_index.head)
	{
		appendStringInfoString(&sql,
			"SELECT repack.oid2text(i.oid), idx.indexrelid, idx.indisvalid, idx.indrelid, repack.oid2text(idx.indrelid), n.nspname"
			" FROM pg_index idx JOIN pg_class i ON i.oid = idx.indexrelid"
			" JOIN pg_namespace n ON n.oid = i.relnamespace"
			" WHERE idx.indexrelid = $1::regclass ORDER BY indisvalid DESC, i.relname, n.nspname");

		cell = r_index.head;
	}
	else if (table_list.head || parent_table_list.head)
	{
		appendStringInfoString(&sql,
			"SELECT repack.oid2text(i.oid), idx.indexrelid, idx.indisvalid, idx.indrelid, $1::text, n.nspname"
			" FROM pg_index idx JOIN pg_class i ON i.oid = idx.indexrelid"
			" JOIN pg_namespace n ON n.oid = i.relnamespace"
			" WHERE idx.indrelid = $1::regclass ORDER BY indisvalid DESC, i.relname, n.nspname");

		for (cell = parent_table_list.head; cell; cell = cell->next)
		{
			int nchildren, i;

			params[0] = cell->val;

			/* find children of this parent table */
			res = execute_elevel("SELECT quote_ident(n.nspname) || '.' || quote_ident(c.relname)"
								 " FROM pg_class c JOIN pg_namespace n on n.oid = c.relnamespace"
								 " WHERE c.oid = ANY (repack.get_table_and_inheritors($1::regclass))"
								 " ORDER BY n.nspname, c.relname", 1, params, DEBUG2);

			if (PQresultStatus(res) != PGRES_TUPLES_OK)
			{
				elog(WARNING, "%s", PQerrorMessage(connection));
				continue;
			}

			nchildren = PQntuples(res);

			if (nchildren == 0)
			{
				elog(WARNING, "relation \"%s\" does not exist", cell->val);
				continue;
			}

			/* append new tables to 'table_list' */
			for (i = 0; i < nchildren; i++)
				simple_string_list_append(&table_list, getstr(res, i, 0));
		}

		CLEARPGRES(res);

		cell = table_list.head;
	}

	for (; cell; cell = cell->next)
	{
		params[0] = cell->val;
		res = execute_elevel(sql.data, 1, params, DEBUG2);

		if (PQresultStatus(res) != PGRES_TUPLES_OK)
		{
			elog(WARNING, "%s", PQerrorMessage(connection));
			continue;
		}

		if (PQntuples(res) == 0)
		{
			if(table_list.head)
				elog(WARNING, "\"%s\" does not have any indexes",
					cell->val);
			else if(r_index.head)
				elog(WARNING, "\"%s\" is not a valid index",
					cell->val);

			continue;
		}

		if(table_list.head)
			elog(INFO, "repacking indexes of \"%s\"", cell->val);

		if (!repack_table_indexes(res))
			elog(WARNING, "repack failed for \"%s\"", cell->val);

		CLEARPGRES(res);
	}
	ret = true;

cleanup:
	disconnect();
	termStringInfo(&sql);
	return ret;
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
	printf("  -t, --table=TABLE         repack specific table only\n");
	printf("  -I, --parent-table=TABLE  repack specific parent table and its inheritors\n");
	printf("  -c, --schema=SCHEMA       repack tables in specific schema only\n");
	printf("  -s, --tablespace=TBLSPC   move repacked tables to a new tablespace\n");
	printf("  -S, --moveidx             move repacked indexes to TBLSPC too\n");
	printf("  -o, --order-by=COLUMNS    order by columns instead of cluster keys\n");
	printf("  -n, --no-order            do vacuum full instead of cluster\n");
	printf("  -N, --dry-run             print what would have been repacked\n");
	printf("  -j, --jobs=NUM            Use this many parallel jobs for each table\n");
	printf("  -i, --index=INDEX         move only the specified index\n");
	printf("  -x, --only-indexes        move only indexes of the specified table\n");
	printf("  -T, --wait-timeout=SECS   timeout to cancel other backends on conflict\n");
	printf("  -D, --no-kill-backend     don't kill other backends when timed out\n");
	printf("  -Z, --no-analyze          don't analyze at end\n");
	printf("  -k, --no-superuser-check  skip superuser checks in client\n");
	printf("  -C, --exclude-extension   don't repack tables which belong to specific extension\n");
}
