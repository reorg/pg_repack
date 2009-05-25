/*-------------------------------------------------------------------------
 *
 * pgut.h
 *
 * Copyright (c) 2009, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#ifndef PGUT_H
#define PGUT_H

#include "libpq-fe.h"
#include "pqexpbuffer.h"

#include <getopt.h>

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

/*
 * pgut client variables and functions
 */
extern const struct option	pgut_options[];

extern bool	pgut_argument(int c, const char *arg);
extern void	pgut_help(void);
extern void	pgut_cleanup(bool fatal);

/*
 * pgut framework variables and functions
 */

extern const char  *PROGRAM_NAME;
extern const char  *PROGRAM_VERSION;
extern const char  *PROGRAM_URL;
extern const char  *PROGRAM_EMAIL;

extern const char  *dbname;
extern const char  *host;
extern const char  *port;
extern const char  *username;
extern bool			password;
extern bool			debug;

extern PGconn	   *connection;
extern bool			interrupted;

extern void	parse_options(int argc, char **argv);
extern bool	assign_option(const char **value, int c, const char *arg);

extern void reconnect(void);
extern void disconnect(void);
extern PGresult *execute_elevel(const char *query, int nParams, const char **params, int elevel);
extern PGresult *execute(const char *query, int nParams, const char **params);
extern void command(const char *query, int nParams, const char **params);

#ifdef WIN32
extern unsigned int sleep(unsigned int seconds);
#endif

/*
 * elog
 */
#define LOG			(-4)
#define INFO		(-3)
#define NOTICE		(-2)
#define WARNING		(-1)
#define ERROR		1
#define HELP		2
#define FATAL		3
#define PANIC		4

#undef elog
extern void
elog(int elevel, const char *fmt, ...)
__attribute__((format(printf, 2, 3)));

/*
 * StringInfo
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

#endif   /* PGUT_H */
