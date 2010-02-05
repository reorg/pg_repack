/*-------------------------------------------------------------------------
 *
 * pgut.h
 *
 * Copyright (c) 2009-2010, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#ifndef PGUT_H
#define PGUT_H

#include "c.h"
#include "libpq-fe.h"
#include "pqexpbuffer.h"

#include <assert.h>
#include <sys/time.h>

#if !defined(C_H) && !defined(__cplusplus)
#ifndef bool
typedef char bool;
#endif
#ifndef true
#define true	((bool) 1)
#endif
#ifndef false
#define false	((bool) 0)
#endif
#endif

#define INFINITE_STR		"INFINITE"

typedef enum YesNo
{
	DEFAULT,
	NO,
	YES
} YesNo;

typedef enum pgut_optsrc
{
	SOURCE_DEFAULT,
	SOURCE_ENV,
	SOURCE_FILE,
	SOURCE_CMDLINE,
	SOURCE_CONST
} pgut_optsrc;

/*
 * type:
 *	b: bool (true)
 *	B: bool (false)
 *  f: pgut_optfn
 *	i: 32bit signed integer
 *	u: 32bit unsigned integer
 *	I: 64bit signed integer
 *	U: 64bit unsigned integer
 *	s: string
 *  t: time_t
 *	y: YesNo (YES)
 *	Y: YesNo (NO)
 */
typedef struct pgut_option
{
	char		type;
	char		sname;		/* short name */
	const char *lname;		/* long name */
	void	   *var;		/* pointer to variable */
	pgut_optsrc	allowed;	/* allowed source */
	pgut_optsrc	source;		/* actual source */
} pgut_option;

typedef void (*pgut_optfn) (pgut_option *opt, const char *arg);
typedef void (*pgut_atexit_callback)(bool fatal, void *userdata);

/*
 * pgut client variables and functions
 */
extern const char  *PROGRAM_NAME;
extern const char  *PROGRAM_VERSION;
extern const char  *PROGRAM_URL;
extern const char  *PROGRAM_EMAIL;

extern void	pgut_help(bool details);

/*
 * pgut framework variables and functions
 */
extern const char  *dbname;
extern const char  *host;
extern const char  *port;
extern const char  *username;
extern char		   *password;
extern bool			debug;
extern bool			quiet;

#ifndef PGUT_NO_PROMPT
extern YesNo	prompt_password;
#endif

extern PGconn	   *connection;
extern bool			interrupted;

extern void help(bool details);
extern int pgut_getopt(int argc, char **argv, pgut_option options[]);
extern void pgut_readopt(const char *path, pgut_option options[], int elevel);
extern void pgut_setopt(pgut_option *opt, const char *optarg, pgut_optsrc src);
extern bool pgut_keyeq(const char *lhs, const char *rhs);
extern void pgut_atexit_push(pgut_atexit_callback callback, void *userdata);
extern void pgut_atexit_pop(pgut_atexit_callback callback, void *userdata);

/*
 * Database connections
 */
extern PGconn *pgut_connect(int elevel);
extern void pgut_disconnect(PGconn *conn);
extern PGresult *pgut_execute(PGconn* conn, const char *query, int nParams, const char **params, int elevel);
extern void pgut_command(PGconn* conn, const char *query, int nParams, const char **params, int elevel);
extern bool pgut_send(PGconn* conn, const char *query, int nParams, const char **params, int elevel);
extern int pgut_wait(int num, PGconn *connections[], struct timeval *timeout);

extern PGconn *reconnect_elevel(int elevel);
extern void reconnect(void);
extern void disconnect(void);
extern PGresult *execute_elevel(const char *query, int nParams, const char **params, int elevel);
extern PGresult *execute(const char *query, int nParams, const char **params);
extern void command(const char *query, int nParams, const char **params);

/*
 * memory allocators
 */
extern void *pgut_malloc(size_t size);
extern void *pgut_realloc(void *p, size_t size);
extern char *pgut_strdup(const char *str);
extern char *strdup_with_len(const char *str, size_t len);
extern char *strdup_trim(const char *str);

#define pgut_new(type)			((type *) pgut_malloc(sizeof(type)))
#define pgut_newarray(type, n)	((type *) pgut_malloc(sizeof(type) * (n)))

/*
 * file operations
 */
extern FILE *pgut_fopen(const char *path, const char *mode, bool missing_ok);
extern void pgut_mkdir(const char *path);

/*
 * elog
 */
#define LOG			(-4)
#define INFO		(-3)
#define NOTICE		(-2)
#define WARNING		(-1)
#define HELP		1
#define ERROR		2
#define FATAL		3
#define PANIC		4

#define ERROR_SYSTEM			10	/* I/O or system error */
#define ERROR_NOMEM				11	/* memory exhausted */
#define ERROR_ARGS				12	/* some configurations are invalid */
#define ERROR_INTERRUPTED		13	/* interrupted by signal */
#define ERROR_PG_COMMAND		14	/* PostgreSQL query or command error */
#define ERROR_PG_CONNECT		15	/* PostgreSQL connection error */

#undef elog
extern void
elog(int elevel, const char *fmt, ...)
__attribute__((format(printf, 2, 3)));

/*
 * Assert
 */
#undef Assert
#undef AssertArg
#undef AssertMacro

#ifdef USE_ASSERT_CHECKING
#define Assert(x)		assert(x)
#define AssertArg(x)	assert(x)
#define AssertMacro(x)	assert(x)
#else
#define Assert(x)		((void) 0)
#define AssertArg(x)	((void) 0)
#define AssertMacro(x)	((void) 0)
#endif

/*
 * StringInfo and string operations
 */
#define STRINGINFO_H

#define StringInfoData			PQExpBufferData
#define StringInfo				PQExpBuffer
#define makeStringInfo			createPQExpBuffer
#define initStringInfo			initPQExpBuffer
#define freeStringInfo			destroyPQExpBuffer
#define termStringInfo			termPQExpBuffer
#define resetStringInfo			resetPQExpBuffer
#define enlargeStringInfo		enlargePQExpBuffer
#define printfStringInfo		printfPQExpBuffer	/* reset + append */
#define appendStringInfo		appendPQExpBuffer
#define appendStringInfoString	appendPQExpBufferStr
#define appendStringInfoChar	appendPQExpBufferChar
#define appendBinaryStringInfo	appendBinaryPQExpBuffer

extern int appendStringInfoFile(StringInfo str, FILE *fp);
extern int appendStringInfoFd(StringInfo str, int fd);

extern bool parse_bool(const char *value, bool *result);
extern bool parse_bool_with_len(const char *value, size_t len, bool *result);
extern bool parse_int32(const char *value, int32 *result);
extern bool parse_uint32(const char *value, uint32 *result);
extern bool parse_int64(const char *value, int64 *result);
extern bool parse_uint64(const char *value, uint64 *result);
extern bool parse_time(const char *value, time_t *time);

#define IsSpace(c)		(isspace((unsigned char)(c)))
#define IsAlpha(c)		(isalpha((unsigned char)(c)))
#define IsAlnum(c)		(isalnum((unsigned char)(c)))
#define IsIdentHead(c)	(IsAlpha(c) || (c) == '_')
#define IsIdentBody(c)	(IsAlnum(c) || (c) == '_')
#define ToLower(c)		(tolower((unsigned char)(c)))
#define ToUpper(c)		(toupper((unsigned char)(c)))

/*
 * socket operations
 */
extern int wait_for_socket(int sock, struct timeval *timeout);
extern int wait_for_sockets(int nfds, fd_set *fds, struct timeval *timeout);

/*
 * import from postgres.h and catalog/genbki.h in 8.4
 */
#if PG_VERSION_NUM < 80400

typedef unsigned long Datum;
typedef struct MemoryContextData *MemoryContext;

#define CATALOG(name,oid)	typedef struct CppConcat(FormData_,name)
#define BKI_BOOTSTRAP
#define BKI_SHARED_RELATION
#define BKI_WITHOUT_OIDS
#define DATA(x)   extern int no_such_variable
#define DESCR(x)  extern int no_such_variable
#define SHDESCR(x) extern int no_such_variable
typedef int aclitem;

#endif

#ifdef WIN32
extern int sleep(unsigned int seconds);
extern int usleep(unsigned int usec);
#endif

#endif   /* PGUT_H */
