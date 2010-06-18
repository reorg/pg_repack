/*-------------------------------------------------------------------------
 *
 * pgut-fe.c
 *
 * Copyright (c) 2009-2010, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#define FRONTEND
#include "pgut-fe.h"

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
#include <getopt_long.h>
#endif

char	   *dbname = NULL;
char	   *host = NULL;
char	   *port = NULL;
char	   *username = NULL;
char	   *password = NULL;
YesNo		prompt_password = DEFAULT;

PGconn	   *connection = NULL;

static bool parse_pair(const char buffer[], char key[], char value[]);
static char *get_username(void);

/*
 * the result is also available with the global variable 'connection'.
 */
void
reconnect(int elevel)
{
	StringInfoData	buf;
	char		   *new_password;

	disconnect();
	initStringInfo(&buf);
	if (dbname && dbname[0])
		appendStringInfo(&buf, "dbname=%s ", dbname);
	if (host && host[0])
		appendStringInfo(&buf, "host=%s ", host);
	if (port && port[0])
		appendStringInfo(&buf, "port=%s ", port);
	if (username && username[0])
		appendStringInfo(&buf, "user=%s ", username);
	if (password && password[0])
		appendStringInfo(&buf, "password=%s ", password);

	connection = pgut_connect(buf.data, prompt_password, elevel);

	/* update password */
	if (connection)
	{
		new_password = PQpass(connection);
		if (new_password && new_password[0] &&
			(password == NULL || strcmp(new_password, password) != 0))
		{
			free(password);
			password = pgut_strdup(new_password);
		}
	}

	termStringInfo(&buf);
}

void
disconnect(void)
{
	if (connection)
	{
		pgut_disconnect(connection);
		connection = NULL;
	}
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

void
pgut_setopt(pgut_option *opt, const char *optarg, pgut_optsrc src)
{
	const char	  *message;

	if (opt == NULL)
	{
		fprintf(stderr, "Try \"%s --help\" for more information.\n", PROGRAM_NAME);
		exit(EINVAL);
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
				ereport(ERROR,
					(errcode(EINVAL),
					 errmsg("invalid option type: %c", opt->type)));
				return;	/* keep compiler quiet */
		}
	}

	if (isprint(opt->sname))
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("option -%c, --%s should be %s: '%s'",
				opt->sname, opt->lname, message, optarg)));
	else
		ereport(ERROR,
			(errcode(EINVAL),
			 errmsg("option --%s should be %s: '%s'",
				opt->lname, message, optarg)));
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

	if ((fp = pgut_fopen(path, "Rt")) == NULL)
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
	const char *s;
	size_t		i;
	size_t		j;

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

/*
 * execute - Execute a SQL and return the result.
 */
PGresult *
execute(const char *query, int nParams, const char **params)
{
	return pgut_execute(connection, query, nParams, params);
}

PGresult *
execute_elevel(const char *query, int nParams, const char **params, int elevel)
{
	return pgut_execute_elevel(connection, query, nParams, params, elevel);
}

/*
 * command - Execute a SQL and discard the result.
 */
ExecStatusType
command(const char *query, int nParams, const char **params)
{
	return pgut_command(connection, query, nParams, params);
}

static void
set_elevel(pgut_option *opt, const char *arg)
{
	pgut_log_level = parse_elevel(arg);
}

static pgut_option default_options[] =
{
	{ 'b', 'e', "echo"			, &pgut_echo },
	{ 'f', 'E', "elevel"		, set_elevel },
	{ 's', 'd', "dbname"		, &dbname },
	{ 's', 'h', "host"			, &host },
	{ 's', 'p', "port"			, &port },
	{ 's', 'U', "username"		, &username },
	{ 'Y', 'w', "no-password"	, &prompt_password },
	{ 'y', 'W', "password"		, &prompt_password },
	{ 0 }
};

static size_t
option_length(const pgut_option opts[])
{
	size_t	len;
	for (len = 0; opts && opts[len].type; len++) { }
	return len;
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

/*
 * Returns the current user name.
 */
static char *
get_username(void)
{
	char *ret;

#ifndef WIN32
	struct passwd *pw;

	pw = getpwuid(geteuid());
	ret = (pw ? pw->pw_name : NULL);
#else
	static char username[128];	/* remains after function execute */
	DWORD		len = sizeof(username) - 1;

	if (GetUserNameA(username, &len))
		ret = username;
	else
	{
		_dosmaperr(GetLastError());
		ret = NULL;
	}
#endif

	if (ret == NULL)
		ereport(ERROR,
			(errcode_errno(),
			 errmsg("could not get current user name: ")));
	return ret;
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

int
pgut_getopt(int argc, char **argv, pgut_option options[])
{
	int					c;
	int					optindex = 0;
	char			   *optstring;
	struct option	   *longopts;
	pgut_option		   *opt;

	pgut_init(argc, argv);

	/* Help message and version are handled at first. */
	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0)
		{
			help(true);
			exit(1);
		}
		if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)
		{
			fprintf(stderr, "%s %s\n", PROGRAM_NAME, PROGRAM_VERSION);
			exit(1);
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

	return optind;
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
		printf("  -w, --no-password         never prompt for password\n");
		printf("  -W, --password            force password prompt\n");
	}

	printf("\nGeneric options:\n");
	if (details)
	{
		printf("  -e, --echo                echo queries\n");
		printf("  -E, --elevel=LEVEL        set output message level\n");
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
