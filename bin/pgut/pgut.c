/*-------------------------------------------------------------------------
 *
 * pgut.c
 *
 * Copyright (c) 2009-2010, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#define FRONTEND
#include "postgres_fe.h"
#include "libpq/pqsignal.h"

#include <getopt.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "pgut.h"

/* old gcc doesn't have LLONG_MAX. */
#ifndef LLONG_MAX
#if defined(HAVE_LONG_INT_64) || !defined(HAVE_LONG_LONG_INT_64)
#define LLONG_MAX		LONG_MAX
#else
#define LLONG_MAX		INT64CONST(0x7FFFFFFFFFFFFFFF)
#endif
#endif

const char *PROGRAM_NAME = NULL;

const char	   *dbname = NULL;
const char	   *host = NULL;
const char	   *port = NULL;
const char	   *username = NULL;
char		   *password = NULL;
bool			debug = false;
bool			quiet = false;

#ifndef PGUT_NO_PROMPT
YesNo	prompt_password = DEFAULT;
#endif

/* Database connections */
PGconn	   *connection = NULL;
static PGcancel *volatile cancel_conn = NULL;

/* Interrupted by SIGINT (Ctrl+C) ? */
bool			interrupted = false;
static bool		in_cleanup = false;

static bool parse_pair(const char buffer[], char key[], char value[]);

/* Connection routines */
static void init_cancel_handler(void);
static void on_before_exec(PGconn *conn);
static void on_after_exec(void);
static void on_interrupt(void);
static void on_cleanup(void);
static void exit_or_abort(int exitcode);
static const char *get_username(void);

static pgut_option default_options[] =
{
	{ 'b', '!', "debug"		, &debug },
	{ 's', 'd', "dbname"	, &dbname },
	{ 's', 'h', "host"		, &host },
	{ 's', 'p', "port"		, &port },
	{ 'b', 'q', "quiet"		, &quiet },
	{ 's', 'U', "username"	, &username },
#ifndef PGUT_NO_PROMPT
	{ 'y', 'w', "no-password"	, &prompt_password },
	{ 'Y', 'W', "password"		, &prompt_password },
#endif
	{ 0 }
};

static size_t
option_length(const pgut_option opts[])
{
	size_t	len;
	for (len = 0; opts && opts[len].type; len++) { }
	return len;
}

static int
option_has_arg(char type)
{
	switch (type)
	{
		case 'b':
		case 'B':
		case 'y':
		case 'Y':
			return no_argument;
		default:
			return required_argument;
	}
}

static void
option_copy(struct option dst[], const pgut_option opts[], size_t len)
{
	size_t	i;

	for (i = 0; i < len; i++)
	{
		dst[i].name = opts[i].lname;
		dst[i].has_arg = option_has_arg(opts[i].type);
		dst[i].flag = NULL;
		dst[i].val = opts[i].sname;
	}
}

static struct option *
option_merge(const pgut_option opts1[], const pgut_option opts2[])
{
	struct option *result;
	size_t	len1 = option_length(opts1);
	size_t	len2 = option_length(opts2);
	size_t	n = len1 + len2;

	result = pgut_newarray(struct option, n + 1);
	option_copy(result, opts1, len1);
	option_copy(result + len1, opts2, len2);
	memset(&result[n], 0, sizeof(pgut_option));

	return result;
}

static pgut_option *
option_find(int c, pgut_option opts1[], pgut_option opts2[])
{
	size_t	i;

	for (i = 0; opts1 && opts1[i].type; i++)
		if (opts1[i].sname == c)
			return &opts1[i];
	for (i = 0; opts2 && opts2[i].type; i++)
		if (opts2[i].sname == c)
			return &opts2[i];

	return NULL;	/* not found */
}

void
pgut_setopt(pgut_option *opt, const char *optarg, pgut_optsrc src)
{
	const char	  *message;

	if (opt == NULL)
	{
		fprintf(stderr, "Try \"%s --help\" for more information.\n", PROGRAM_NAME);
		exit_or_abort(ERROR_ARGS);
	}

	if (opt->source > src)
	{
		/* high prior value has been set already. */
		return;
	}
	else if (src >= SOURCE_CMDLINE && opt->source >= src)
	{
		/* duplicated option in command line */
		message = "specified only once";
	}
	else
	{
		/* can be overwritten if non-command line source */
		opt->source = src;

		switch (opt->type)
		{
			case 'b':
			case 'B':
				if (optarg == NULL)
				{
					*((bool *) opt->var) = (opt->type == 'b');
					return;
				}
				else if (parse_bool(optarg, (bool *) opt->var))
				{
					return;
				}
				message = "a boolean";
				break;
			case 'f':
				((pgut_optfn) opt->var)(opt, optarg);
				return;
			case 'i':
				if (parse_int32(optarg, opt->var))
					return;
				message = "a 32bit signed integer";
				break;
			case 'u':
				if (parse_uint32(optarg, opt->var))
					return;
				message = "a 32bit unsigned integer";
				break;
			case 'I':
				if (parse_int64(optarg, opt->var))
					return;
				message = "a 64bit signed integer";
				break;
			case 'U':
				if (parse_uint64(optarg, opt->var))
					return;
				message = "a 64bit unsigned integer";
				break;
			case 's':
				if (opt->source != SOURCE_DEFAULT)
					free(*(char **) opt->var);
				*(char **) opt->var = pgut_strdup(optarg);
				return;
			case 't':
				if (parse_time(optarg, opt->var))
					return;
				message = "a time";
				break;
			case 'y':
			case 'Y':
				if (optarg == NULL)
				{
					*(YesNo *) opt->var = (opt->type == 'y' ? YES : NO);
					return;
				}
				else
				{
					bool	value;
					if (parse_bool(optarg, &value))
					{
						*(YesNo *) opt->var = (value ? YES : NO);
						return;
					}
				}
				message = "a boolean";
				break;
			default:
				elog(ERROR, "invalid option type: %c", opt->type);
				return;	/* keep compiler quiet */
		}
	}

	if (isprint(opt->sname))
		elog(ERROR_ARGS, "option -%c, --%s should be %s: '%s'",
			opt->sname, opt->lname, message, optarg);
	else
		elog(ERROR_ARGS, "option --%s should be %s: '%s'",
			opt->lname, message, optarg);
}

/*
 * Try to interpret value as boolean value.  Valid values are: true,
 * false, yes, no, on, off, 1, 0; as well as unique prefixes thereof.
 * If the string parses okay, return true, else false.
 * If okay and result is not NULL, return the value in *result.
 */
bool
parse_bool(const char *value, bool *result)
{
	return parse_bool_with_len(value, strlen(value), result);
}

bool
parse_bool_with_len(const char *value, size_t len, bool *result)
{
	switch (*value)
	{
		case 't':
		case 'T':
			if (pg_strncasecmp(value, "true", len) == 0)
			{
				if (result)
					*result = true;
				return true;
			}
			break;
		case 'f':
		case 'F':
			if (pg_strncasecmp(value, "false", len) == 0)
			{
				if (result)
					*result = false;
				return true;
			}
			break;
		case 'y':
		case 'Y':
			if (pg_strncasecmp(value, "yes", len) == 0)
			{
				if (result)
					*result = true;
				return true;
			}
			break;
		case 'n':
		case 'N':
			if (pg_strncasecmp(value, "no", len) == 0)
			{
				if (result)
					*result = false;
				return true;
			}
			break;
		case 'o':
		case 'O':
			/* 'o' is not unique enough */
			if (pg_strncasecmp(value, "on", (len > 2 ? len : 2)) == 0)
			{
				if (result)
					*result = true;
				return true;
			}
			else if (pg_strncasecmp(value, "off", (len > 2 ? len : 2)) == 0)
			{
				if (result)
					*result = false;
				return true;
			}
			break;
		case '1':
			if (len == 1)
			{
				if (result)
					*result = true;
				return true;
			}
			break;
		case '0':
			if (len == 1)
			{
				if (result)
					*result = false;
				return true;
			}
			break;
		default:
			break;
	}

	if (result)
		*result = false;		/* suppress compiler warning */
	return false;
}

/*
 * Parse string as 32bit signed int.
 * valid range: -2147483648 ~ 2147483647
 */
bool
parse_int32(const char *value, int32 *result)
{
	int64	val;
	char   *endptr;

	if (strcmp(value, INFINITE_STR) == 0)
	{
		*result = INT_MAX;
		return true;
	}

	errno = 0;
	val = strtol(value, &endptr, 0);
	if (endptr == value || *endptr)
		return false;

	if (errno == ERANGE || val != (int64) ((int32) val))
		return false;

	*result = val;

	return true;
}

/*
 * Parse string as 32bit unsigned int.
 * valid range: 0 ~ 4294967295 (2^32-1)
 */
bool
parse_uint32(const char *value, uint32 *result)
{
	uint64	val;
	char   *endptr;

	if (strcmp(value, INFINITE_STR) == 0)
	{
		*result = UINT_MAX;
		return true;
	}

	errno = 0;
	val = strtoul(value, &endptr, 0);
	if (endptr == value || *endptr)
		return false;

	if (errno == ERANGE || val != (uint64) ((uint32) val))
		return false;

	*result = val;

	return true;
}

/*
 * Parse string as int64
 * valid range: -9223372036854775808 ~ 9223372036854775807
 */
bool
parse_int64(const char *value, int64 *result)
{
	int64	val;
	char   *endptr;

	if (strcmp(value, INFINITE_STR) == 0)
	{
		*result = LLONG_MAX;
		return true;
	}

	errno = 0;
#if defined(HAVE_LONG_INT_64)
	val = strtol(value, &endptr, 0);
#elif defined(HAVE_LONG_LONG_INT_64)
	val = strtoll(value, &endptr, 0);
#else
	val = strtol(value, &endptr, 0);
#endif
	if (endptr == value || *endptr)
		return false;

	if (errno == ERANGE)
		return false;

	*result = val;

	return true;
}

/*
 * Parse string as uint64
 * valid range: 0 ~ (2^64-1)
 */
bool
parse_uint64(const char *value, uint64 *result)
{
	uint64	val;
	char   *endptr;

	if (strcmp(value, INFINITE_STR) == 0)
	{
#if defined(HAVE_LONG_INT_64)
		*result = ULONG_MAX;
#elif defined(HAVE_LONG_LONG_INT_64)
		*result = ULLONG_MAX;
#else
		*result = ULONG_MAX;
#endif
		return true;
	}

	errno = 0;
#if defined(HAVE_LONG_INT_64)
	val = strtoul(value, &endptr, 0);
#elif defined(HAVE_LONG_LONG_INT_64)
	val = strtoull(value, &endptr, 0);
#else
	val = strtoul(value, &endptr, 0);
#endif
	if (endptr == value || *endptr)
		return false;

	if (errno == ERANGE)
		return false;

	*result = val;

	return true;
}

/*
 * Convert ISO-8601 format string to time_t value.
 */
bool
parse_time(const char *value, time_t *time)
{
	size_t		len;
	char	   *tmp;
	int			i;
	struct tm	tm;
	char		junk[2];

	/* tmp = replace( value, !isalnum, ' ' ) */
	tmp = pgut_malloc(strlen(value) + + 1);
	len = 0;
	for (i = 0; value[i]; i++)
		tmp[len++] = (IsAlnum(value[i]) ? value[i] : ' ');
	tmp[len] = '\0';

	/* parse for "YYYY-MM-DD HH:MI:SS" */
	tm.tm_year = 0;		/* tm_year is year - 1900 */
	tm.tm_mon = 0;		/* tm_mon is 0 - 11 */
	tm.tm_mday = 1;		/* tm_mday is 1 - 31 */
	tm.tm_hour = 0;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	i = sscanf(tmp, "%04d %02d %02d %02d %02d %02d%1s",
		&tm.tm_year, &tm.tm_mon, &tm.tm_mday,
		&tm.tm_hour, &tm.tm_min, &tm.tm_sec, junk);
	free(tmp);

	if (i < 1 || 6 < i)
		return false;

	/* adjust year */
	if (tm.tm_year < 100)
		tm.tm_year += 2000 - 1900;
	else if (tm.tm_year >= 1900)
		tm.tm_year -= 1900;

	/* adjust month */
	if (i > 1)
		tm.tm_mon -= 1;

	*time = mktime(&tm);

	return true;
}

static char *
longopts_to_optstring(const struct option opts[])
{
	size_t	len;
	char   *result;
	char   *s;

	for (len = 0; opts[len].name; len++) { }
	result = pgut_malloc(len * 2 + 1);

	s = result;
	for (len = 0; opts[len].name; len++)
	{
		if (!isprint(opts[len].val))
			continue;
		*s++ = opts[len].val;
		if (opts[len].has_arg != no_argument)
			*s++ = ':';
	}
	*s = '\0';

	return result;
}

static void
option_from_env(pgut_option options[])
{
	size_t	i;

	for (i = 0; options && options[i].type; i++)
	{
		pgut_option	   *opt = &options[i];
		char			name[256];
		size_t			j;
		const char	   *s;
		const char	   *value;

		if (opt->source > SOURCE_ENV ||
			opt->allowed == SOURCE_DEFAULT || opt->allowed > SOURCE_ENV)
			continue;

		for (s = opt->lname, j = 0; *s && j < lengthof(name) - 1; s++, j++)
		{
			if (strchr("-_ ", *s))
				name[j] = '_';	/* - to _ */
			else
				name[j] = toupper(*s);
		}
		name[j] = '\0';

		if ((value = getenv(name)) != NULL)
			pgut_setopt(opt, value, SOURCE_ENV);
	}
}

int
pgut_getopt(int argc, char **argv, pgut_option options[])
{
	int					c;
	int					optindex = 0;
	char			   *optstring;
	struct option	   *longopts;
	pgut_option		   *opt;

	if (PROGRAM_NAME == NULL)
	{
		PROGRAM_NAME = get_progname(argv[0]);
		set_pglocale_pgservice(argv[0], "pgscripts");
	}

	/* Help message and version are handled at first. */
	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0)
		{
			help(true);
			exit_or_abort(HELP);
		}
		if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)
		{
			fprintf(stderr, "%s %s\n", PROGRAM_NAME, PROGRAM_VERSION);
			exit_or_abort(HELP);
		}
	}

	/* Merge default and user options. */
	longopts = option_merge(default_options, options);
	optstring = longopts_to_optstring(longopts);

	/* Assign named options */
	while ((c = getopt_long(argc, argv, optstring, longopts, &optindex)) != -1)
	{
		opt = option_find(c, default_options, options);
		pgut_setopt(opt, optarg, SOURCE_CMDLINE);
	}

	/* Read environment variables */
	option_from_env(options);
	(void) (dbname ||
	(dbname = getenv("PGDATABASE")) ||
	(dbname = getenv("PGUSER")) ||
	(dbname = get_username()));

	init_cancel_handler();
	atexit(on_cleanup);

	return optind;
}

/* compare two strings ignore cases and ignore -_ */
bool
pgut_keyeq(const char *lhs, const char *rhs)
{
	for (; *lhs && *rhs; lhs++, rhs++)
	{
		if (strchr("-_ ", *lhs))
		{
			if (!strchr("-_ ", *rhs))
				return false;
		}
		else if (ToLower(*lhs) != ToLower(*rhs))
			return false;
	}

	return *lhs == '\0' && *rhs == '\0';
}

/*
 * Get configuration from configuration file.
 */
void
pgut_readopt(const char *path, pgut_option options[], int elevel)
{
	FILE   *fp;
	char	buf[1024];
	char	key[1024];
	char	value[1024];

	if (!options)
		return;

	if ((fp = pgut_fopen(path, "rt", true)) == NULL)
		return;

	while (fgets(buf, lengthof(buf), fp))
	{
		size_t		i;

		for (i = strlen(buf); i > 0 && IsSpace(buf[i - 1]); i--)
			buf[i - 1] = '\0';

		if (parse_pair(buf, key, value))
		{
			for (i = 0; options[i].type; i++)
			{
				pgut_option *opt = &options[i];

				if (pgut_keyeq(key, opt->lname))
				{
					if (opt->allowed == SOURCE_DEFAULT ||
						opt->allowed > SOURCE_FILE)
						elog(elevel, "option %s cannot specified in file", opt->lname);
					else if (opt->source <= SOURCE_FILE)
						pgut_setopt(opt, value, SOURCE_FILE);
					break;
				}
			}
			if (!options[i].type)
				elog(elevel, "invalid option \"%s\"", key);
		}
	}

	fclose(fp);
}

static const char *
skip_space(const char *str, const char *line)
{
	while (IsSpace(*str)) { str++; }
	return str;
}

static const char *
get_next_token(const char *src, char *dst, const char *line)
{
	const char   *s;
	int		i;
	int		j;

	if ((s = skip_space(src, line)) == NULL)
		return NULL;

	/* parse quoted string */
	if (*s == '\'')
	{
		s++;
		for (i = 0, j = 0; s[i] != '\0'; i++)
		{
			if (s[i] == '\\')
			{
				i++;
				switch (s[i])
				{
					case 'b':
						dst[j] = '\b';
						break;
					case 'f':
						dst[j] = '\f';
						break;
					case 'n':
						dst[j] = '\n';
						break;
					case 'r':
						dst[j] = '\r';
						break;
					case 't':
						dst[j] = '\t';
						break;
					case '0':
					case '1':
					case '2':
					case '3':
					case '4':
					case '5':
					case '6':
					case '7':
						{
							int			k;
							long		octVal = 0;

							for (k = 0;
								 s[i + k] >= '0' && s[i + k] <= '7' && k < 3;
									 k++)
								octVal = (octVal << 3) + (s[i + k] - '0');
							i += k - 1;
							dst[j] = ((char) octVal);
						}
						break;
					default:
						dst[j] = s[i];
						break;
				}
			}
			else if (s[i] == '\'')
			{
				i++;
				/* doubled quote becomes just one quote */
				if (s[i] == '\'')
					dst[j] = s[i];
				else
					break;
			}
			else
				dst[j] = s[i];
			j++;
		}
	}
	else
	{
		i = j = strcspn(s, "# \n\r\t\v");
		memcpy(dst, s, j);
	}

	dst[j] = '\0';
	return s + i;
}

static bool
parse_pair(const char buffer[], char key[], char value[])
{
	const char *start;
	const char *end;

	key[0] = value[0] = '\0';

	/*
	 * parse key
	 */
	start = buffer;
	if ((start = skip_space(start, buffer)) == NULL)
		return false;

	end = start + strcspn(start, "=# \n\r\t\v");

	/* skip blank buffer */
	if (end - start <= 0)
	{
		if (*start == '=')
			elog(WARNING, "syntax error in \"%s\"", buffer);
		return false;
	}

	/* key found */
	strncpy(key, start, end - start);
	key[end - start] = '\0';

	/* find key and value split char */
	if ((start = skip_space(end, buffer)) == NULL)
		return false;

	if (*start != '=')
	{
		elog(WARNING, "syntax error in \"%s\"", buffer);
		return false;
	}

	start++;

	/*
	 * parse value
	 */
	if ((end = get_next_token(start, value, buffer)) == NULL)
		return false;

	if ((start = skip_space(end, buffer)) == NULL)
		return false;

	if (*start != '\0' && *start != '#')
	{
		elog(WARNING, "syntax error in \"%s\"", buffer);
		return false;
	}

	return true;
}

#ifndef PGUT_NO_PROMPT
/*
 * Ask the user for a password; 'username' is the username the
 * password is for, if one has been explicitly specified.
 * Set malloc'd string to the global variable 'password'.
 */
static void
prompt_for_password(const char *username)
{
	if (password)
	{
		free(password);
		password = NULL;
	}

	if (username == NULL)
		password = simple_prompt("Password: ", 100, false);
	else
	{
		char	message[256];
		snprintf(message, lengthof(message), "Password for user %s: ", username);
		password = simple_prompt(message, 100, false);
	}
}
#endif

#if PG_VERSION_NUM < 80300
static bool
PQconnectionNeedsPassword(PGconn *conn)
{
	return strcmp(PQerrorMessage(conn), PQnoPasswordSupplied) == 0 && !feof(stdin);
}
#endif

PGconn *
pgut_connect(int elevel)
{
	PGconn	   *conn;

	if (interrupted && !in_cleanup)
		elog(ERROR_INTERRUPTED, "interrupted");

#ifndef PGUT_NO_PROMPT
	if (prompt_password == YES)
		prompt_for_password(username);
#endif

	/* Start the connection. Loop until we have a password if requested by backend. */
	for (;;)
	{
		conn = PQsetdbLogin(host, port, NULL, NULL, dbname, username, password);

		if (PQstatus(conn) == CONNECTION_OK)
			return conn;

#ifndef PGUT_NO_PROMPT
		if (conn && PQconnectionNeedsPassword(conn) && prompt_password != NO)
		{
			PQfinish(conn);
			prompt_for_password(username);
			continue;
		}
#endif
		elog(elevel, "could not connect to database %s: %s",
			(dbname ? dbname : "(default)"), PQerrorMessage(conn));
		PQfinish(conn);
		return NULL;
	}
}

void
pgut_disconnect(PGconn *conn)
{
	if (conn)
	{
		PQfinish(conn);
		if (conn == connection)
			connection = NULL;
	}
}

/*
 * the result is also available with the global variable 'connection'.
 */
PGconn *
reconnect_elevel(int elevel)
{
	disconnect();
	return connection = pgut_connect(elevel);
}

void
reconnect(void)
{
	reconnect_elevel(ERROR_PG_CONNECT);
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
pgut_execute(PGconn* conn, const char *query, int nParams, const char **params, int elevel)
{
	PGresult   *res;

	if (interrupted && !in_cleanup)
		elog(ERROR_INTERRUPTED, "interrupted");

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

	if (conn == NULL)
	{
		elog(elevel, "not connected");
		return NULL;
	}

	on_before_exec(conn);
	if (nParams == 0)
		res = PQexec(conn, query);
	else
		res = PQexecParams(conn, query, nParams, NULL, params, NULL, NULL, 0);
	on_after_exec();

	switch (PQresultStatus(res))
	{
		case PGRES_TUPLES_OK:
		case PGRES_COMMAND_OK:
		case PGRES_COPY_IN:
			break;
		default:
			elog(elevel, "query failed: %squery was: %s",
				PQerrorMessage(conn), query);
			break;
	}

	return res;
}

void
pgut_command(PGconn* conn, const char *query, int nParams, const char **params, int elevel)
{
	PQclear(pgut_execute(conn, query, nParams, params, elevel));
}

bool
pgut_send(PGconn* conn, const char *query, int nParams, const char **params, int elevel)
{
	int			res;

	if (interrupted && !in_cleanup)
		elog(ERROR_INTERRUPTED, "interrupted");

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

	if (conn == NULL)
	{
		elog(elevel, "not connected");
		return false;
	}

	if (nParams == 0)
		res = PQsendQuery(conn, query);
	else
		res = PQsendQueryParams(conn, query, nParams, NULL, params, NULL, NULL, 0);

	if (res != 1)
	{
		elog(elevel, "query failed: %squery was: %s",
			PQerrorMessage(conn), query);
		return false;
	}

	return true;
}

int
pgut_wait(int num, PGconn *connections[], struct timeval *timeout)
{
	/* all connections are busy. wait for finish */
	while (!interrupted)
	{
		int		i;
		fd_set	mask;
		int		maxsock;

		FD_ZERO(&mask);

		maxsock = -1;
		for (i = 0; i < num; i++)
		{
			int	sock;

			if (connections[i] == NULL)
				continue;
			sock = PQsocket(connections[i]);
			if (sock >= 0)
			{
				FD_SET(sock, &mask);
				if (maxsock < sock)
					maxsock = sock;
			}
		}

		if (maxsock == -1)
		{
			errno = ENOENT;
			return -1;
		}

		i = wait_for_sockets(maxsock + 1, &mask, timeout);
		if (i == 0)
			break;	/* timeout */

		for (i = 0; i < num; i++)
		{
			if (connections[i] && FD_ISSET(PQsocket(connections[i]), &mask))
			{
				PQconsumeInput(connections[i]);
				if (PQisBusy(connections[i]))
					continue;
				return i;
			}
		}
	}

	errno = EINTR;
	return -1;
}

PGresult *
execute_elevel(const char *query, int nParams, const char **params, int elevel)
{
	return pgut_execute(connection, query, nParams, params, elevel);
}

/*
 * execute - Execute a SQL and return the result, or exit_or_abort() if failed.
 */
PGresult *
execute(const char *query, int nParams, const char **params)
{
	return execute_elevel(query, nParams, params, ERROR_PG_COMMAND);
}

/*
 * command - Execute a SQL and discard the result, or exit_or_abort() if failed.
 */
void
command(const char *query, int nParams, const char **params)
{
	PQclear(execute(query, nParams, params));
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
	if (quiet && elevel < WARNING)
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
	case FATAL:
		fputs("FATAL: ", stderr);
		break;
	case PANIC:
		fputs("PANIC: ", stderr);
		break;
	default:
		if (elevel >= ERROR)
			fputs("ERROR: ", stderr);
		break;
	}

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fputc('\n', stderr);
	fflush(stderr);
	va_end(args);

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

	if (in_cleanup)
		return;	/* forbid cancel during cleanup */

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

	if (in_cleanup)
		return;	/* forbid cancel during cleanup */

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
	if (!in_cleanup && cancel_conn != NULL &&
		PQcancel(cancel_conn, errbuf, sizeof(errbuf)))
	{
		elog(WARNING, "Cancel request sent");
	}

	errno = save_errno;			/* just in case the write changed it */
}

typedef struct pgut_atexit_item pgut_atexit_item;
struct pgut_atexit_item
{
	pgut_atexit_callback	callback;
	void				   *userdata;
	pgut_atexit_item	   *next;
};

static pgut_atexit_item *pgut_atexit_stack = NULL;

void
pgut_atexit_push(pgut_atexit_callback callback, void *userdata)
{
	pgut_atexit_item *item;

	AssertArg(callback != NULL);

	item = pgut_new(pgut_atexit_item);
	item->callback = callback;
	item->userdata = userdata;
	item->next = pgut_atexit_stack;

	pgut_atexit_stack = item;
}

void
pgut_atexit_pop(pgut_atexit_callback callback, void *userdata)
{
	pgut_atexit_item  *item;
	pgut_atexit_item **prev;

	for (item = pgut_atexit_stack, prev = &pgut_atexit_stack;
		 item;
		 prev = &item->next, item = item->next)
	{
		if (item->callback == callback && item->userdata == userdata)
		{
			*prev = item->next;
			free(item);
			break;
		}
	}
}

static void
call_atexit_callbacks(bool fatal)
{
	pgut_atexit_item  *item;

	for (item = pgut_atexit_stack; item; item = item->next)
		item->callback(fatal, item->userdata);
}

static void
on_cleanup(void)
{
	in_cleanup = true;
	interrupted = false;
	call_atexit_callbacks(false);
	disconnect();
}

static void
exit_or_abort(int exitcode)
{
	if (in_cleanup)
	{
		/* oops, error in cleanup*/
		call_atexit_callbacks(true);
		abort();
	}
	else
	{
		/* normal exit */
		exit(exitcode);
	}
}

void
help(bool details)
{
	pgut_help(details);

	if (details)
	{
		printf("\nConnection options:\n");
		printf("  -d, --dbname=DBNAME       database to connect\n");
		printf("  -h, --host=HOSTNAME       database server host or socket directory\n");
		printf("  -p, --port=PORT           database server port\n");
		printf("  -U, --username=USERNAME   user name to connect as\n");
#ifndef PGUT_NO_PROMPT
		printf("  -w, --no-password         never prompt for password\n");
		printf("  -W, --password            force password prompt\n");
#endif
	}

	printf("\nGeneric options:\n");
	if (details)
	{
		printf("  -q, --quiet               don't write any messages\n");
		printf("  --debug                   debug mode\n");
	}
	printf("  --help                    show this help, then exit\n");
	printf("  --version                 output version information, then exit\n");

	if (details && (PROGRAM_URL || PROGRAM_EMAIL))
	{
		printf("\n");
		if (PROGRAM_URL)
			printf("Read the website for details. <%s>\n", PROGRAM_URL);
		if (PROGRAM_EMAIL)
			printf("Report bugs to <%s>.\n", PROGRAM_EMAIL);
	}
}

/*
 * Returns the current user name.
 */
static const char *
get_username(void)
{
	const char *ret;

#ifndef WIN32
	struct passwd *pw;

	pw = getpwuid(geteuid());
	ret = (pw ? pw->pw_name : NULL);
#else
	static char username[128];	/* remains after function exit */
	DWORD		len = sizeof(username) - 1;

	if (GetUserName(username, &len))
		ret = username;
	else
	{
		_dosmaperr(GetLastError());
		ret = NULL;
	}
#endif

	if (ret == NULL)
		elog(ERROR_SYSTEM, "%s: could not get current user name: %s",
				PROGRAM_NAME, strerror(errno));
	return ret;
}

int
appendStringInfoFile(StringInfo str, FILE *fp)
{
	AssertArg(str != NULL);
	AssertArg(fp != NULL);

	for (;;)
	{
		int		rc;

		if (str->maxlen - str->len < 2 && enlargeStringInfo(str, 1024) == 0)
			return errno = ENOMEM;

		rc = fread(str->data + str->len, 1, str->maxlen - str->len - 1, fp);
		if (rc == 0)
			break;
		else if (rc > 0)
		{
			str->len += rc;
			str->data[str->len] = '\0';
		}
		else if (ferror(fp) && errno != EINTR)
			return errno;
	}
	return 0;
}

int
appendStringInfoFd(StringInfo str, int fd)
{
	AssertArg(str != NULL);
	AssertArg(fd != -1);

	for (;;)
	{
		int		rc;

		if (str->maxlen - str->len < 2 && enlargeStringInfo(str, 1024) == 0)
			return errno = ENOMEM;

		rc = read(fd, str->data + str->len, str->maxlen - str->len - 1);
		if (rc == 0)
			break;
		else if (rc > 0)
		{
			str->len += rc;
			str->data[str->len] = '\0';
		}
		else if (errno != EINTR)
			return errno;
	}
	return 0;
}

void *
pgut_malloc(size_t size)
{
	char *ret;

	if ((ret = malloc(size)) == NULL)
		elog(ERROR_NOMEM, "could not allocate memory (%lu bytes): %s",
			(unsigned long) size, strerror(errno));
	return ret;
}

void *
pgut_realloc(void *p, size_t size)
{
	char *ret;

	if ((ret = realloc(p, size)) == NULL)
		elog(ERROR_NOMEM, "could not re-allocate memory (%lu bytes): %s",
			(unsigned long) size, strerror(errno));
	return ret;
}

char *
pgut_strdup(const char *str)
{
	char *ret;

	if (str == NULL)
		return NULL;

	if ((ret = strdup(str)) == NULL)
		elog(ERROR_NOMEM, "could not duplicate string \"%s\": %s",
			str, strerror(errno));
	return ret;
}

char *
strdup_with_len(const char *str, size_t len)
{
	char *r;

	if (str == NULL)
		return NULL;

	r = pgut_malloc(len + 1);
	memcpy(r, str, len);
	r[len] = '\0';
	return r;
}

/* strdup but trim whitespaces at head and tail */
char *
strdup_trim(const char *str)
{
	size_t	len;

	if (str == NULL)
		return NULL;

	while (IsSpace(str[0])) { str++; }
	len = strlen(str);
	while (len > 0 && IsSpace(str[len - 1])) { len--; }

	return strdup_with_len(str, len);
}

/* Try open file. Also create parent directries if open for writes. */
FILE *
pgut_fopen(const char *path, const char *mode, bool missing_ok)
{
	FILE *fp;

retry:
	if ((fp = fopen(path, mode)) == NULL)
	{
		if (errno == ENOENT)
		{
			if (missing_ok)
				return NULL;
			if (mode[0] == 'w' || mode[0] == 'a')
			{
				char	dir[MAXPGPATH];

				strlcpy(dir, path, MAXPGPATH);
				get_parent_directory(dir);
				pgut_mkdir(dir);
				goto retry;
			}
		}

		elog(ERROR_SYSTEM, "could not open file \"%s\": %s",
			path, strerror(errno));
	}

	return fp;
}

/*
 * this tries to build all the elements of a path to a directory a la mkdir -p
 * we assume the path is in canonical form, i.e. uses / as the separator.
 */
void
pgut_mkdir(const char *dirpath)
{
	struct stat sb;
	int			first,
				last,
				retval;
	char	   *path;
	char	   *p;

	Assert(dirpath != NULL);

	p = path = pgut_strdup(dirpath);
	retval = 0;

#ifdef WIN32
	/* skip network and drive specifiers for win32 */
	if (strlen(p) >= 2)
	{
		if (p[0] == '/' && p[1] == '/')
		{
			/* network drive */
			p = strstr(p + 2, "/");
			if (p == NULL)
				elog(ERROR_ARGS, "invalid path \"%s\"", dirpath);
		}
		else if (p[1] == ':' &&
				 ((p[0] >= 'a' && p[0] <= 'z') ||
				  (p[0] >= 'A' && p[0] <= 'Z')))
		{
			/* local drive */
			p += 2;
		}
	}
#endif

	if (p[0] == '/')			/* Skip leading '/'. */
		++p;
	for (first = 1, last = 0; !last; ++p)
	{
		if (p[0] == '\0')
			last = 1;
		else if (p[0] != '/')
			continue;
		*p = '\0';
		if (!last && p[1] == '\0')
			last = 1;
		if (first)
			first = 0;

retry:
		/* check for pre-existing directory; ok if it's a parent */
		if (stat(path, &sb) == 0)
		{
			if (!S_ISDIR(sb.st_mode))
			{
				if (last)
					errno = EEXIST;
				else
					errno = ENOTDIR;
				retval = 1;
				break;
			}
		}
		else if (mkdir(path, S_IRWXU) < 0)
		{
			if (errno == EEXIST)
				goto retry;	/* another thread might create the directory. */
			retval = 1;
			break;
		}
		if (!last)
			*p = '/';
	}

	if (retval != 0)
		elog(ERROR_SYSTEM, "could not create directory \"%s\": %s",
					 dirpath, strerror(errno));

	free(path);
}

#ifdef WIN32
static int select_win32(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timeval * timeout);
#define select		select_win32
#endif

int
wait_for_socket(int sock, struct timeval *timeout)
{
	fd_set		fds;

	FD_ZERO(&fds);
	FD_SET(sock, &fds);
	return wait_for_sockets(sock + 1, &fds, timeout);
}

int
wait_for_sockets(int nfds, fd_set *fds, struct timeval *timeout)
{
	int		i;

	for (;;)
	{
		i = select(nfds, fds, NULL, NULL, timeout);
		if (i < 0)
		{
			if (interrupted)
				elog(ERROR_INTERRUPTED, "interrupted");
			else if (errno != EINTR)
				elog(ERROR_SYSTEM, "select failed: %s", strerror(errno));
		}
		else
			return i;
	}
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

int
sleep(unsigned int seconds)
{
	Sleep(seconds * 1000);
	return 0;
}

int
usleep(unsigned int usec)
{
	Sleep((usec + 999) / 1000);	/* rounded up */
	return 0;
}

#undef select
static int
select_win32(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timeval * timeout)
{
	struct timeval	remain;

	if (timeout != NULL)
		remain = *timeout;
	else
	{
		remain.tv_usec = 0;
		remain.tv_sec = LONG_MAX;	/* infinite */
	}

	/* sleep only one second because Ctrl+C doesn't interrupt select. */
	while (remain.tv_sec > 0 || remain.tv_usec > 0)
	{
		int				ret;
		struct timeval	onesec;

		if (remain.tv_sec > 0)
		{
			onesec.tv_sec = 1;
			onesec.tv_usec = 0;
			remain.tv_sec -= 1;
		}
		else
		{
			onesec.tv_sec = 0;
			onesec.tv_usec = remain.tv_usec;
			remain.tv_usec = 0;
		}

		ret = select(nfds, readfds, writefds, exceptfds, &onesec);
		if (ret != 0)
		{
			/* succeeded or error */
			return ret;
		}
		else if (interrupted)
		{
			errno = EINTR;
			return 0;
		}
	}

	return 0;	/* timeout */
}

#endif   /* WIN32 */
