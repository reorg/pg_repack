/*-------------------------------------------------------------------------
 *
 * pgut.c
 *
 * Copyright (c) 2009, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#include "postgres_fe.h"
#include "libpq/pqsignal.h"

#include <unistd.h>

#include "pgut.h"

const char *PROGRAM_NAME = NULL;

const char *dbname = NULL;
const char *host = NULL;
const char *port = NULL;
const char *username = NULL;
bool		password = false;
bool		debug = false;

/* Database connections */
PGconn	   *connection = NULL;
static PGcancel *volatile cancel_conn = NULL;

/* Interrupted by SIGINT (Ctrl+C) ? */
bool		interrupted = false;

/* Connection routines */
static void init_cancel_handler(void);
static void on_before_exec(PGconn *conn);
static void on_after_exec(void);
static void on_interrupt(void);
static void on_cleanup(void);
static void exit_or_abort(int exitcode);
static void help(void);
static const char *get_user_name(const char *PROGRAM_NAME);

const struct option default_options[] =
{
	{"dbname", required_argument, NULL, 'd'},
	{"host", required_argument, NULL, 'h'},
	{"port", required_argument, NULL, 'p'},
	{"username", required_argument, NULL, 'U'},
	{"password", no_argument, NULL, 'W'},
	{"debug", no_argument, NULL, '!'},
	{NULL, 0, NULL, 0}
};

static const struct option	   *longopts = NULL;;

static const struct option *
merge_longopts(const struct option *opts)
{
	size_t	len;
	struct option *result;

	if (opts == NULL)
		return default_options;

	for (len = 0; opts[len].name; len++) { }
	if (len == 0)
		return default_options;

	result = (struct option *) malloc((len + lengthof(default_options)) * sizeof(struct option));
	memcpy(&result[0], opts, len * sizeof(struct option));
	memcpy(&result[len], default_options, lengthof(default_options) * sizeof(struct option));
	return result;
}

static const char *
longopts_to_optstring(const struct option *opts)
{
	size_t	len;
	char   *result;
	char   *s;

	for (len = 0; opts[len].name; len++) { }
	result = malloc(len * 2 + 1);

	s = result;
	for (len = 0; opts[len].name; len++)
	{
		*s++ = opts[len].val;
		if (opts[len].has_arg == required_argument)
			*s++ = ':';
	}
	*s = '\0';

	return result;
}

void
parse_options(int argc, char **argv)
{
	int			c;
	int			optindex = 0;
	const char *optstring;

	PROGRAM_NAME = get_progname(argv[0]);
	set_pglocale_pgservice(argv[0], "pgscripts");

	/* Help message and version are handled at first. */
	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0)
		{
			help();
			exit_or_abort(HELP);
		}
		if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)
		{
			fprintf(stderr, "%s %s\n", PROGRAM_NAME, PROGRAM_VERSION);
			exit_or_abort(HELP);
		}
	}

	/* Merge default and user options. */
	longopts = merge_longopts(pgut_options);
	optstring = longopts_to_optstring(longopts);

	while ((c = getopt_long(argc, argv, optstring, longopts, &optindex)) != -1)
	{
		switch (c)
		{
		case 'd':
			assign_option(&dbname, c, optarg);
			break;
		case 'h':
			assign_option(&host, c, optarg);
			break;
		case 'p':
			assign_option(&port, c, optarg);
			break;
		case 'U':
			assign_option(&username, c, optarg);
			break;
		case 'W':
			password = true;
			break;
		case '!':
			debug = true;
			break;
		default:
			if (!pgut_argument(c, optarg))
			{
				fprintf(stderr, "Try \"%s --help\" for more information.\n", PROGRAM_NAME);
				exit_or_abort(ERROR);
			}
			break;
		}
	}

	for (; optind < argc; optind++)
	{
		if (!pgut_argument(0, argv[optind]))
		{
			fprintf(stderr, "%s: too many command-line arguments (first is \"%s\")\n",
					PROGRAM_NAME, argv[optind]);
			fprintf(stderr, "Try \"%s --help\" for more information.\n", PROGRAM_NAME);
			exit_or_abort(ERROR);
		}
	}

	init_cancel_handler();
	atexit(on_cleanup);

	(void) (dbname ||
	(dbname = getenv("PGDATABASE")) ||
	(dbname = getenv("PGUSER")) ||
	(dbname = get_user_name(PROGRAM_NAME)));
}

bool
assign_option(const char **value, int c, const char *arg)
{
	if (*value != NULL)
	{
		const struct option *opt;
		for (opt = longopts; opt->name; opt++)
		{
			if (opt->val == c)
				break;
		}
		if (opt->name)
			elog(ERROR, "option -%c(--%s) should be specified only once", c, opt->name);
		else
			elog(ERROR, "option -%c should be specified only once", c);
		return false;
	}
	*value = arg;
	return true;
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
			elog(ERROR, "could not connect to database %s", dbname);

		if (PQstatus(conn) == CONNECTION_BAD &&
#if PG_VERSION_NUM >= 80300
			PQconnectionNeedsPassword(conn) &&
#else
			strcmp(PQerrorMessage(conn), PQnoPasswordSupplied) == 0 &&
			!feof(stdin) &&
#endif
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
		elog(ERROR, "could not connect to database %s: %s",
					dbname, PQerrorMessage(conn));

	connection = conn;
}

void
disconnect(void)
{
	if (connection)
	{
		PQfinish(connection);
		connection = NULL;
	}
}

PGresult *
execute_elevel(const char *query, int nParams, const char **params, int elevel)
{
	PGresult   *res;

	if (interrupted)
	{
		interrupted = false;
		elog(ERROR, "%s: interrupted", PROGRAM_NAME);
	}

	/* write query to elog if debug */
	if (debug)
	{
		int		i;

		if (strchr(query, '\n'))
			elog(LOG, "(query)\n%s", query);
		else
			elog(LOG, "(query) %s", query);
		for (i = 0; i < nParams; i++)
			elog(LOG, "\t(param:%d) = %s", i, params[i] ? params[i] : "(null)");
	}

	on_before_exec(connection);
	if (nParams == 0)
		res = PQexec(connection, query);
	else
		res = PQexecParams(connection, query, nParams, NULL, params, NULL, NULL, 0);
	on_after_exec();

	switch (PQresultStatus(res))
	{
		case PGRES_TUPLES_OK:
		case PGRES_COMMAND_OK:
			break;
		default:
			elog(elevel, "query failed: %squery was: %s",
				PQerrorMessage(connection), query);
			break;
	}

	return res;
}

/*
 * execute - Execute a SQL and return the result, or exit_or_abort() if failed.
 */
PGresult *
execute(const char *query, int nParams, const char **params)
{
	return execute_elevel(query, nParams, params, ERROR);
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

/*
 * elog - log to stderr and exit if ERROR or FATAL
 */
void
elog(int elevel, const char *fmt, ...)
{
	va_list		args;

	if (!debug && elevel <= LOG)
		return;

	switch (elevel)
	{
	case LOG:
		fputs("LOG: ", stderr);
		break;
	case INFO:
		fputs("INFO: ", stderr);
		break;
	case NOTICE:
		fputs("NOTICE: ", stderr);
		break;
	case WARNING:
		fputs("WARNING: ", stderr);
		break;
	case ERROR:
		fputs("ERROR: ", stderr);
		break;
	case FATAL:
		fputs("FATAL: ", stderr);
		break;
	case PANIC:
		fputs("PANIC: ", stderr);
		break;
	}

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	fflush(stderr);

	if (elevel > 0)
		exit_or_abort(elevel);
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
		fprintf(stderr, "Cancel request sent\n");

	errno = save_errno;			/* just in case the write changed it */
}

static bool	in_cleanup = false;

static void
on_cleanup(void)
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

static void help(void)
{
	pgut_help();
	fprintf(stderr, "\nConnection options:\n");
	fprintf(stderr, "  -d, --dbname=DBNAME       database to connect\n");
	fprintf(stderr, "  -h, --host=HOSTNAME       database server host or socket directory\n");
	fprintf(stderr, "  -p, --port=PORT           database server port\n");
	fprintf(stderr, "  -U, --username=USERNAME   user name to connect as\n");
	fprintf(stderr, "  -W, --password            force password prompt\n");
	fprintf(stderr, "\nGeneric options:\n");
	fprintf(stderr, "  --debug                   debug mode\n");
	fprintf(stderr, "  --help                    show this help, then exit\n");
	fprintf(stderr, "  --version                 output version information, then exit\n\n");
	if (PROGRAM_URL)
		fprintf(stderr, "Read the website for details. <%s>\n", PROGRAM_URL);
	if (PROGRAM_EMAIL)
		fprintf(stderr, "Report bugs to <%s>.\n", PROGRAM_EMAIL);
}

/*
 * Returns the current user name.
 */
static const char *
get_user_name(const char *PROGRAM_NAME)
{
#ifndef WIN32
	struct passwd *pw;

	pw = getpwuid(geteuid());
	if (!pw)
		elog(ERROR, "%s: could not obtain information about current user: %s",
				PROGRAM_NAME, strerror(errno));
	return pw->pw_name;
#else
	static char username[128];	/* remains after function exit */
	DWORD		len = sizeof(username) - 1;

	if (!GetUserName(username, &len))
		elog(ERROR, "%s: could not get current user name: %s",
			PROGRAM_NAME, strerror(errno));
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
