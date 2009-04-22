/*
 * pgut.c
 *
 * Copyright (c) 2009, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */
#include "pgut.h"

#include "postgres_fe.h"
#include "libpq/pqsignal.h"

#include <unistd.h>

const char *progname = NULL;
const char *dbname = NULL;
char	   *host = NULL;
char	   *port = NULL;
char	   *username = NULL;
pqbool		password = false;

/* Interrupted by SIGINT (Ctrl+C) ? */
pqbool		interrupted = false;

/* Database connections */
PGconn	   *current_conn = NULL;
static PGcancel *volatile cancel_conn = NULL;

/* Connection routines */
static void init_cancel_handler(void);
static void on_before_exec(PGconn *conn);
static void on_after_exec(void);
static void on_interrupt(void);
static void on_exit(void);
static void exit_or_abort(int exitcode);
const char *get_user_name(const char *progname);

const char default_optstring[] = "d:h:p:U:W";

const struct option default_longopts[] =
{
	{"dbname", required_argument, NULL, 'd'},
	{"host", required_argument, NULL, 'h'},
	{"port", required_argument, NULL, 'p'},
	{"username", required_argument, NULL, 'U'},
	{"password", no_argument, NULL, 'W'},
	{NULL, 0, NULL, 0}
};

static const char *
merge_optstring(const char *opts)
{
	size_t	len;
	char   *result;

	if (opts == NULL)
		return default_optstring;

	len = strlen(opts);
	if (len == 0)
		return default_optstring;

	result = malloc(len + lengthof(default_optstring));
	memcpy(&result[0], opts, len);
	memcpy(&result[len], default_optstring, lengthof(default_optstring));
	return result;
}

static const struct option *
merge_longopts(const struct option *opts)
{
	size_t	len;
	struct option *result;

	if (opts == NULL)
		return default_longopts;

	for (len = 0; opts[len].name; len++) { }
	if (len == 0)
		return default_longopts;

	result = (struct option *) malloc((len + lengthof(default_longopts)) * sizeof(struct option));
	memcpy(&result[0], opts, len * sizeof(struct option));
	memcpy(&result[len], default_longopts, lengthof(default_longopts) * sizeof(struct option));
	return result;
}

int
pgut_getopt(int argc, char **argv)
{
	const char			   *optstring;
	const struct option	   *longopts;

	int		c;
	int		optindex = 0;

	progname = get_progname(argv[0]);
	set_pglocale_pgservice(argv[0], "pgscripts");

	/*
	 * Help message and version are handled at first.
	 */
	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0)
			return pgut_help();
		if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)
			return pgut_version();
	}

	optstring = merge_optstring(pgut_optstring);
	longopts = merge_longopts(pgut_longopts);

	while ((c = getopt_long(argc, argv, optstring, longopts, &optindex)) != -1)
	{
		switch (c)
		{
		case 'h':
			host = optarg;
			break;
		case 'p':
			port = optarg;
			break;
		case 'U':
			username = optarg;
			break;
		case 'W':
			password = true;
			break;
		case 'd':
			dbname = optarg;
			break;
		default:
			if (!pgut_argument(c, optarg))
			{
				fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
				exit_or_abort(EXITCODE_ERROR);
			}
			break;
		}
	}

	for (; optind < argc; optind++)
	{
		if (!pgut_argument(0, argv[optind]))
		{
			fprintf(stderr, _("%s: too many command-line arguments (first is \"%s\")\n"),
					progname, argv[optind]);
			fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
			exit_or_abort(EXITCODE_ERROR);
		}
	}

	init_cancel_handler();
	atexit(on_exit);

	(void) (dbname ||
	(dbname = getenv("PGDATABASE")) ||
	(dbname = getenv("PGUSER")) ||
	(dbname = get_user_name(progname)));

	return 0;
}

void
reconnect(void)
{
	PGconn	   *conn;
	char	   *pwd = NULL;
	bool		new_pass;

	disconnect();

	if (password)
		pwd = simple_prompt("Password: ", 100, false);

	/*
	 * Start the connection.  Loop until we have a password if requested by
	 * backend.
	 */
	do
	{
		new_pass = false;
		conn = PQsetdbLogin(host, port, NULL, NULL, dbname, username, pwd);

		if (!conn)
		{
			fprintf(stderr, _("%s: could not connect to database %s\n"),
					progname, dbname);
			exit_or_abort(EXITCODE_ERROR);
		}

		if (PQstatus(conn) == CONNECTION_BAD &&
			PQconnectionNeedsPassword(conn) &&
			pwd == NULL)
		{
			PQfinish(conn);
			pwd = simple_prompt("Password: ", 100, false);
			new_pass = true;
		}
	} while (new_pass);

	free(pwd);

	/* check to see that the backend connection was successfully made */
	if (PQstatus(conn) == CONNECTION_BAD)
	{
		fprintf(stderr, _("%s: could not connect to database %s: %s"),
				progname, dbname, PQerrorMessage(conn));
		exit_or_abort(EXITCODE_ERROR);
	}

	current_conn = conn;
}

void
disconnect(void)
{
	if (current_conn)
	{
		PQfinish(current_conn);
		current_conn = NULL;
	}
}

PGresult *
execute_nothrow(const char *query, int nParams, const char **params)
{
	PGresult   *res;

	on_before_exec(current_conn);
	if (nParams == 0)
		res = PQexec(current_conn, query);
	else
		res = PQexecParams(current_conn, query, nParams, NULL, params, NULL, NULL, 0);
	on_after_exec();

	return res;
}

/*
 * execute - Execute a SQL and return the result, or exit_or_abort() if failed.
 */
PGresult *
execute(const char *query, int nParams, const char **params)
{
	if (interrupted)
	{
		interrupted = false;
		fprintf(stderr, _("%s: interrupted\n"), progname);
	}
	else
	{
		PGresult   *res = execute_nothrow(query, nParams, params);

		if (PQresultStatus(res) == PGRES_TUPLES_OK ||
			PQresultStatus(res) == PGRES_COMMAND_OK)
			return res;

		fprintf(stderr, _("%s: query failed: %s"),
				progname, PQerrorMessage(current_conn));
		fprintf(stderr, _("%s: query was: %s\n"),
				progname, query);
		PQclear(res);
	}

	exit_or_abort(EXITCODE_ERROR);
	return NULL;	/* keep compiler quiet */
}

/*
 * command - Execute a SQL and discard the result, or exit_or_abort() if failed.
 */
void
command(const char *query, int nParams, const char **params)
{
	PGresult *res = execute(query, nParams, params);
	PQclear(res);
}


#ifdef WIN32
static CRITICAL_SECTION cancelConnLock;
#endif

/*
 * on_before_exec
 *
 * Set cancel_conn to point to the current database connection.
 */
static void
on_before_exec(PGconn *conn)
{
	PGcancel   *old;

#ifdef WIN32
	EnterCriticalSection(&cancelConnLock);
#endif

	/* Free the old one if we have one */
	old = cancel_conn;

	/* be sure handle_sigint doesn't use pointer while freeing */
	cancel_conn = NULL;

	if (old != NULL)
		PQfreeCancel(old);

	cancel_conn = PQgetCancel(conn);

#ifdef WIN32
	LeaveCriticalSection(&cancelConnLock);
#endif
}

/*
 * on_after_exec
 *
 * Free the current cancel connection, if any, and set to NULL.
 */
static void
on_after_exec(void)
{
	PGcancel   *old;

#ifdef WIN32
	EnterCriticalSection(&cancelConnLock);
#endif

	old = cancel_conn;

	/* be sure handle_sigint doesn't use pointer while freeing */
	cancel_conn = NULL;

	if (old != NULL)
		PQfreeCancel(old);

#ifdef WIN32
	LeaveCriticalSection(&cancelConnLock);
#endif
}

/*
 * Handle interrupt signals by cancelling the current command.
 */
static void
on_interrupt(void)
{
	int			save_errno = errno;
	char		errbuf[256];

	/* Set interruped flag */
	interrupted = true;

	/* Send QueryCancel if we are processing a database query */
	if (cancel_conn != NULL && PQcancel(cancel_conn, errbuf, sizeof(errbuf)))
		fprintf(stderr, _("Cancel request sent\n"));

	errno = save_errno;			/* just in case the write changed it */
}

static pqbool	in_cleanup = false;

static void
on_exit(void)
{
	in_cleanup = true;
	pgut_cleanup(false);
	disconnect();
}

static void
exit_or_abort(int exitcode)
{
	if (in_cleanup)
	{
		/* oops, error in cleanup*/
		pgut_cleanup(true);
		abort();
	}
	else
	{
		/* normal exit */
		exit(exitcode);
	}
}

/*
 * Returns the current user name.
 */
const char *
get_user_name(const char *progname)
{
#ifndef WIN32
	struct passwd *pw;

	pw = getpwuid(geteuid());
	if (!pw)
	{
		fprintf(stderr, _("%s: could not obtain information about current user: %s\n"),
				progname, strerror(errno));
		exit_or_abort(EXITCODE_ERROR);
	}
	return pw->pw_name;
#else
	static char username[128];	/* remains after function exit */
	DWORD		len = sizeof(username) - 1;

	if (!GetUserName(username, &len))
	{
		fprintf(stderr, _("%s: could not get current user name: %s\n"),
				progname, strerror(errno));
		exit_or_abort(EXITCODE_ERROR);
	}
	return username;
#endif
}

#ifndef WIN32
static void
handle_sigint(SIGNAL_ARGS)
{
	on_interrupt();
}

static void
init_cancel_handler(void)
{
	pqsignal(SIGINT, handle_sigint);
}
#else							/* WIN32 */

/*
 * Console control handler for Win32. Note that the control handler will
 * execute on a *different thread* than the main one, so we need to do
 * proper locking around those structures.
 */
static BOOL WINAPI
consoleHandler(DWORD dwCtrlType)
{
	if (dwCtrlType == CTRL_C_EVENT ||
		dwCtrlType == CTRL_BREAK_EVENT)
	{
		EnterCriticalSection(&cancelConnLock);
		on_interrupt();
		LeaveCriticalSection(&cancelConnLock);
		return TRUE;
	}
	else
		/* Return FALSE for any signals not being handled */
		return FALSE;
}

static void
init_cancel_handler(void)
{
	InitializeCriticalSection(&cancelConnLock);

	SetConsoleCtrlHandler(consoleHandler, TRUE);
}

unsigned int
sleep(unsigned int seconds)
{
	Sleep(seconds * 1000);
	return 0;
}

#endif   /* WIN32 */
