/*
 * pg_reorg.c: bin/pg_reorg.c
 *
 * Copyright (c) 2008-2009, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

/**
 * @brief Client Modules
 */

#define PROGRAM_VERSION	"1.0.4"
#define PROGRAM_URL		"http://reorg.projects.postgresql.org/"
#define PROGRAM_EMAIL	"reorg-general@lists.pgfoundry.org"

#include "pgut/pgut.h"
#include "pqexpbuffer.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define EXITCODE_HELP	2
#define APPLY_COUNT		1000

#define SQL_XID_SNAPSHOT_80300 \
	"SELECT reorg.array_accum(virtualtransaction) FROM pg_locks"\
	" WHERE locktype = 'virtualxid' AND pid <> pg_backend_pid()"
#define SQL_XID_SNAPSHOT_80200 \
	"SELECT reorg.array_accum(transactionid) FROM pg_locks"\
	" WHERE locktype = 'transactionid' AND pid <> pg_backend_pid()"

#define SQL_XID_ALIVE_80300 \
	"SELECT 1 FROM pg_locks WHERE locktype = 'virtualxid'"\
	" AND pid <> pg_backend_pid() AND virtualtransaction = ANY($1) LIMIT 1"
#define SQL_XID_ALIVE_80200 \
	"SELECT 1 FROM pg_locks WHERE locktype = 'transactionid'"\
	" AND pid <> pg_backend_pid() AND transactionid = ANY($1) LIMIT 1"

#define SQL_XID_SNAPSHOT \
	(PQserverVersion(current_conn) >= 80300 \
	? SQL_XID_SNAPSHOT_80300 \
	: SQL_XID_SNAPSHOT_80200)

#define SQL_XID_ALIVE \
	(PQserverVersion(current_conn) >= 80300 \
	? SQL_XID_ALIVE_80300 \
	: SQL_XID_ALIVE_80200)

/*
 * per-table information
 */
typedef struct reorg_table
{
	const char	   *target_name;	/* target: relname */
	Oid				target_oid;		/* target: OID */
	Oid				target_toast;	/* target: toast OID */
	Oid				target_tidx;	/* target: toast index OID */
	Oid				pkid;			/* target: PK OID */
	Oid				ckid;			/* target: CK OID */
	const char	   *create_pktype;	/* CREATE TYPE pk */
	const char	   *create_log;		/* CREATE TABLE log */
	const char	   *create_trigger;	/* CREATE TRIGGER z_reorg_trigger */
	const char	   *create_table;	/* CREATE TABLE table AS SELECT */
	const char	   *delete_log;		/* DELETE FROM log */
	const char	   *lock_table;		/* LOCK TABLE table */
	const char	   *sql_peek;		/* SQL used in flush */
	const char	   *sql_insert;		/* SQL used in flush */
	const char	   *sql_delete;		/* SQL used in flush */
	const char	   *sql_update;		/* SQL used in flush */
	const char	   *sql_pop;		/* SQL used in flush */
} reorg_table;

/*
 * per-index information
 */
typedef struct reorg_index
{
	Oid				target_oid;		/* target: OID */
	const char	   *create_index;	/* CREATE INDEX */
} reorg_index;

static void reorg_all_databases(const char *orderby);
static pqbool reorg_one_database(const char *orderby, const char *table);
static void reorg_one_table(const reorg_table *table, const char *orderby);

static char *getstr(PGresult *res, int row, int col);
static Oid getoid(PGresult *res, int row, int col);

#define SQLSTATE_INVALID_SCHEMA_NAME	"3F000"
#define SQLSTATE_LOCK_NOT_AVAILABLE		"55P03"

static pqbool sqlstate_equals(PGresult *res, const char *state)
{
	return strcmp(PQresultErrorField(res, PG_DIAG_SQLSTATE), state) == 0;
}

static pqbool	echo = false;
static pqbool	verbose = false;
static pqbool	quiet = false;

/*
 * The table begin re-organized. If not null, we need to cleanup temp
 * objects before the program exits.
 */
static const reorg_table *current_table = NULL;

/* buffer should have at least 11 bytes */
static char *
utoa(unsigned int value, char *buffer)
{
	sprintf(buffer, "%u", value);
	return buffer;
}

const char *pgut_optstring = "eqvat:no:";

const struct option pgut_longopts[] = {
	{"echo", no_argument, NULL, 'e'},
	{"quiet", no_argument, NULL, 'q'},
	{"verbose", no_argument, NULL, 'v'},
	{"all", no_argument, NULL, 'a'},
	{"table", required_argument, NULL, 't'},
	{"no-order", no_argument, NULL, 'n'},
	{"order-by", required_argument, NULL, 'o'},
	{NULL, 0, NULL, 0}
};

pqbool		alldb = false;
const char *table = NULL;
const char *orderby = NULL;

pqbool
pgut_argument(int c, const char *arg)
{
	switch (c)
	{
		case 'e':
			echo = true;
			break;
		case 'q':
			quiet = true;
			break;
		case 'v':
			verbose = true;
			break;
		case 'a':
			alldb = true;
			break;
		case 't':
			table = arg;
			break;
		case 'n':
			orderby = "";
			break;
		case 'o':
			orderby = arg;
			break;
		default:
			return false;
	}
	return true;
}

int
main(int argc, char *argv[])
{
	int			exitcode;

	exitcode = pgut_getopt(argc, argv);
	if (exitcode)
		return exitcode;

	if (alldb)
	{
		if (table)
		{
			fprintf(stderr, "%s: cannot reorg a specific table in all databases\n",
					progname);
			exit(1);
		}

		reorg_all_databases(orderby);
	}
	else
	{
		if (!reorg_one_database(orderby, table))
		{
			fprintf(stderr, "ERROR:  %s is not installed\n", progname);
			return 1;
		}
	}

	return 0;
}

/*
 * Call reorg_one_database for each database.
 */
static void
reorg_all_databases(const char *orderby)
{
	PGresult   *result;
	int			i;

	dbname = "postgres";
	reconnect();
	result = execute("SELECT datname FROM pg_database WHERE datallowconn ORDER BY 1;", 0, NULL);
	disconnect();

	for (i = 0; i < PQntuples(result); i++)
	{
		pqbool	ret;

		dbname = PQgetvalue(result, i, 0);

		if (!quiet)
		{
			printf("%s: reorg database \"%s\"", progname, dbname);
			fflush(stdout);
		}

		ret = reorg_one_database(orderby, NULL);

		if (!quiet)
		{
			if (ret)
				printf("\n");
			else
				printf(" ... skipped\n");
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
 * Call reorg_one_table for the target table or each table in a database.
 */
static pqbool
reorg_one_database(const char *orderby, const char *table)
{
	pqbool			ret = true;
	PGresult	   *res;
	int				i;
	int				num;
	PQExpBufferData sql;

	initPQExpBuffer(&sql);

	reconnect();

	/* Restrict search_path to system catalog. */
	command("SET search_path = pg_catalog, pg_temp, public", 0, NULL);

	/* To avoid annoying "create implicit ..." messages. */
	command("SET client_min_messages = warning", 0, NULL);

	/* acquire target tables */
	appendPQExpBufferStr(&sql, "SELECT * FROM reorg.tables WHERE ");
	if (table)
	{
		appendPQExpBufferStr(&sql, "relid = $1::regclass");
		res = execute_nothrow(sql.data, 1, &table);
	}
	else
	{
		appendPQExpBufferStr(&sql, "pkid IS NOT NULL");
		if (!orderby)
			appendPQExpBufferStr(&sql, " AND ckid IS NOT NULL");
		res = execute_nothrow(sql.data, 0, NULL);
	}

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		if (sqlstate_equals(res, SQLSTATE_INVALID_SCHEMA_NAME))
		{
			/* Schema reorg does not exist. Skip the database. */
			ret = false;
			goto cleanup;
		}
		else
		{
			/* exit otherwise */
			printf("%s", PQerrorMessage(current_conn));
			PQclear(res);
			exit(1);
		}
	}

	num = PQntuples(res);

	for (i = 0; i < num; i++)
	{
		reorg_table	table;
		const char *create_table;
		const char *ckey;
		int			c = 0;

		table.target_name = getstr(res, i, c++);
		table.target_oid = getoid(res, i, c++);
		table.target_toast = getoid(res, i, c++);
		table.target_tidx = getoid(res, i, c++);
		table.pkid = getoid(res, i, c++);
		table.ckid = getoid(res, i, c++);

		if (table.pkid == 0)
		{
			fprintf(stderr, "ERROR:  relation \"%s\" has no primary key\n", table.target_name);
			exit(1);
		}

		table.create_pktype = getstr(res, i, c++);
		table.create_log = getstr(res, i, c++);
		table.create_trigger = getstr(res, i, c++);

		create_table = getstr(res, i, c++);
		table.delete_log = getstr(res, i, c++);
		table.lock_table = getstr(res, i, c++);
		ckey = getstr(res, i, c++);

		resetPQExpBuffer(&sql);
		if (!orderby)
		{
			/* CLUSTER mode */
			if (ckey == NULL)
			{
				fprintf(stderr, "ERROR:  relation \"%s\" has no cluster key\n", table.target_name);
				exit(1);
			}
			appendPQExpBuffer(&sql, "%s ORDER BY %s", create_table, ckey);
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
			appendPQExpBuffer(&sql, "%s ORDER BY %s", create_table, orderby);
            table.create_table = sql.data;
		}

		table.sql_peek = getstr(res, i, c++);
		table.sql_insert = getstr(res, i, c++);
		table.sql_delete = getstr(res, i, c++);
		table.sql_update = getstr(res, i, c++);
		table.sql_pop = getstr(res, i, c++);

		reorg_one_table(&table, orderby);
	}

cleanup:
	PQclear(res);
	disconnect();
	termPQExpBuffer(&sql);
	return ret;
}

static int
apply_log(const reorg_table *table, int count)
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

	res = execute("SELECT reorg.reorg_apply($1, $2, $3, $4, $5, $6)",
		6, params);
	result = atoi(PQgetvalue(res, 0, 0));
	PQclear(res);

	return result;
}

/*
 * Re-organize one table.
 */
static void
reorg_one_table(const reorg_table *table, const char *orderby)
{
	PGresult   *res;
	const char *params[1];
	int			num;
	int			i;
	char	   *vxid;
	char		buffer[12];

	if (verbose)
	{
		fprintf(stderr, "---- reorg_one_table ----\n");
		fprintf(stderr, "target_name    : %s\n", table->target_name);
		fprintf(stderr, "target_oid     : %u\n", table->target_oid);
		fprintf(stderr, "target_toast   : %u\n", table->target_toast);
		fprintf(stderr, "target_tidx    : %u\n", table->target_tidx);
		fprintf(stderr, "pkid           : %u\n", table->pkid);
		fprintf(stderr, "ckid           : %u\n", table->ckid);
		fprintf(stderr, "create_pktype  : %s\n", table->create_pktype);
		fprintf(stderr, "create_log     : %s\n", table->create_log);
		fprintf(stderr, "create_trigger : %s\n", table->create_trigger);
		fprintf(stderr, "create_table   : %s\n", table->create_table);
		fprintf(stderr, "delete_log     : %s\n", table->delete_log);
		fprintf(stderr, "lock_table     : %s\n", table->lock_table);
		fprintf(stderr, "sql_peek       : %s\n", table->sql_peek);
		fprintf(stderr, "sql_insert     : %s\n", table->sql_insert);
		fprintf(stderr, "sql_delete     : %s\n", table->sql_delete);
		fprintf(stderr, "sql_update     : %s\n", table->sql_update);
		fprintf(stderr, "sql_pop        : %s\n", table->sql_pop);
	}

	/*
	 * 1. Setup workspaces and a trigger.
	 */
	if (verbose)
		fprintf(stderr, "---- setup ----\n");

	command("BEGIN ISOLATION LEVEL READ COMMITTED", 0, NULL);

	/*
	 * Check z_reorg_trigger is the trigger executed at last so that
	 * other before triggers cannot modify triggered tuples.
	 */
	params[0] = utoa(table->target_oid, buffer);

	res = execute(
		"SELECT 1 FROM pg_trigger"
		" WHERE tgrelid = $1 AND tgname >= 'z_reorg_trigger' LIMIT 1",
		1, params);
	if (PQntuples(res) > 0)
	{
		fprintf(stderr, "%s: trigger conflicted for %s\n",
			progname, table->target_name);
		exit(1);
	}

	command(table->create_pktype, 0, NULL);
	command(table->create_log, 0, NULL);
	command(table->create_trigger, 0, NULL);
	command("COMMIT", 0, NULL);

	/*
	 * Register the table to be dropped on error. We use pktype as
	 * an advisory lock. The registration should be done after
	 * the first command is succeeded.
	 */
	current_table = table;

	/*
	 * 2. Copy tuples into temp table.
	 */
	if (verbose)
		fprintf(stderr, "---- copy tuples ----\n");

	command("BEGIN ISOLATION LEVEL SERIALIZABLE", 0, NULL);
	/* SET work_mem = maintenance_work_mem */
	command("SELECT set_config('work_mem', current_setting('maintenance_work_mem'), true)", 0, NULL);
	if (PQserverVersion(current_conn) >= 80300 && orderby && !orderby[0])
		command("SET LOCAL synchronize_seqscans = off", 0, NULL);
	res = execute(SQL_XID_SNAPSHOT, 0, NULL);
	vxid = strdup(PQgetvalue(res, 0, 0));
	PQclear(res);
	command(table->delete_log, 0, NULL);
	command(table->create_table, 0, NULL);
	command("COMMIT", 0, NULL);

	/*
	 * 3. Create indexes on temp table.
	 */
	if (verbose)
		fprintf(stderr, "---- create indexes ----\n");

	params[0] = utoa(table->target_oid, buffer);
	res = execute("SELECT indexrelid,"
		" reorg.reorg_indexdef(indexrelid, indrelid)"
		" FROM pg_index WHERE indrelid = $1", 1, params);

	num = PQntuples(res);
	for (i = 0; i < num; i++)
	{
		reorg_index	index;
		int			c = 0;

		index.target_oid = getoid(res, i, c++);
		index.create_index = getstr(res, i, c++);

		if (verbose)
		{
			fprintf(stderr, "[%d]\n", i);
			fprintf(stderr, "target_oid   : %u\n", index.target_oid);
			fprintf(stderr, "create_index : %s\n", index.create_index);
		}

		/*
		 * NOTE: If we want to create multiple indexes in parallel,
		 * we need to call create_index in multiple connections.
		 */
		command(index.create_index, 0, NULL);
	}
	PQclear(res);

	/*
	 * 4. Apply log to temp table until no tuples left in the log
	 * and all of old transactions are finished.
	 */
	for (;;)
	{
		num = apply_log(table, APPLY_COUNT);
		if (num > 0)
			continue;	/* there might be still some tuples, repeat. */

		/* old transactions still alive ? */
		params[0] = vxid;
		res = execute(SQL_XID_ALIVE, 1, params);
		num = PQntuples(res);
		PQclear(res);

		if (num > 0)
		{
			sleep(1);
			continue;	/* wait for old transactions */
		}

		/* ok, go next step. */
		break;
	}

	/*
	 * 5. Swap.
	 */
	if (verbose)
		fprintf(stderr, "---- swap ----\n");

	for (;;)
	{
		command("BEGIN ISOLATION LEVEL READ COMMITTED", 0, NULL);
		res = execute_nothrow(table->lock_table, 0, NULL);
		if (PQresultStatus(res) == PGRES_COMMAND_OK)
		{
			PQclear(res);
			break;
		}
		else if (sqlstate_equals(res, SQLSTATE_LOCK_NOT_AVAILABLE))
		{
			/* retry if lock conflicted */
			PQclear(res);
			command("ROLLBACK", 0, NULL);
			sleep(1);
			continue;
		}
		else
		{
			/* exit otherwise */
			printf("%s", PQerrorMessage(current_conn));
			PQclear(res);
			exit(1);
		}
	}

	apply_log(table, 0);
	params[0] = utoa(table->target_oid, buffer);
	command("SELECT reorg.reorg_swap($1)", 1, params);
	command("COMMIT", 0, NULL);

	/*
	 * 6. Drop.
	 */
	if (verbose)
		fprintf(stderr, "---- drop ----\n");

	command("BEGIN ISOLATION LEVEL READ COMMITTED", 0, NULL);
	params[0] = utoa(table->target_oid, buffer);
	command("SELECT reorg.reorg_drop($1)", 1, params);
	command("COMMIT", 0, NULL);

	current_table = NULL;

	free(vxid);
}

void
pgut_cleanup(pqbool fatal)
{
	if (fatal)
	{
		if (current_table)
			fprintf(stderr, "!!!FATAL ERROR!!! Please refer to a manual.\n\n");
	}
	else
	{
		char		buffer[12];
		const char *params[1];

		if (current_table == NULL)
			return;	/* no needs to cleanup */

		/* Rollback current transaction */
		if (current_conn)
			command("ROLLBACK", 0, NULL);

		/* Try reconnection if not available. */
		if (PQstatus(current_conn) != CONNECTION_OK)
			reconnect();

		/* do cleanup */
		params[0] = utoa(current_table->target_oid, buffer);
		command("SELECT reorg.reorg_drop($1)", 1, params);
		current_table = NULL;
	}
}

int
pgut_help(void)
{
	fprintf(stderr,
		"%s re-organizes a PostgreSQL database.\n\n"
		"Usage:\n"
		"  %s [OPTION]... [DBNAME]\n"
		"\nOptions:\n"
		"  -a, --all                 reorg all databases\n"
		"  -d, --dbname=DBNAME       database to reorg\n"
		"  -t, --table=TABLE         reorg specific table only\n"
		"  -n, --no-order            do vacuum full instead of cluster\n"
		"  -o, --order-by=columns    order by columns instead of cluster keys\n"
		"  -e, --echo                show the commands being sent to the server\n"
		"  -q, --quiet               don't write any messages\n"
		"  -v, --verbose             display detailed information during processing\n"
		"  --help                    show this help, then exit\n"
		"  --version                 output version information, then exit\n"
		"\nConnection options:\n"
		"  -h, --host=HOSTNAME       database server host or socket directory\n"
		"  -p, --port=PORT           database server port\n"
		"  -U, --username=USERNAME   user name to connect as\n"
		"  -W, --password            force password prompt\n",
		progname, progname);
#ifdef PROGRAM_URL
	fprintf(stderr,"\nRead the website for details. <" PROGRAM_URL ">\n");
#endif
#ifdef PROGRAM_EMAIL
	fprintf(stderr,"\nReport bugs to <" PROGRAM_EMAIL ">.\n");
#endif
	return EXITCODE_HELP;
}

int
pgut_version(void)
{
	fprintf(stderr, "%s %s\n", progname, PROGRAM_VERSION);
	return EXITCODE_HELP;
}
