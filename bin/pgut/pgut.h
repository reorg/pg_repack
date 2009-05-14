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
extern const char		   *pgut_optstring;
extern const struct option	pgut_longopts[];

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

extern PGconn	   *connection;

extern void	parse_options(int argc, char **argv);
extern bool	assign_option(const char **value, int c, const char *arg);

extern void reconnect(void);
extern void disconnect(void);
extern PGresult *execute_nothrow(const char *query, int nParams, const char **params);
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
#define StringInfoData			PQExpBufferData
#define StringInfo				PQExpBuffer
#define makeStringInfo			createPQExpBuffer
#define initStringInfo			initPQExpBuffer
#define freeStringInfo			destroyPQExpBuffer
#define termStringInfo			termPQExpBuffer
#define resetStringInfo			resetPQExpBuffer
#define enlargeStringInfo		enlargePQExpBuffer
/*
#define printfPQExpBuffer		= resetStringInfo + appendStringInfo
*/
#define appendStringInfo		appendPQExpBuffer
#define appendStringInfoString	appendPQExpBufferStr
#define appendStringInfoChar	appendPQExpBufferChar
#define appendBinaryStringInfo	appendBinaryPQExpBuffer

#endif   /* PGUT_H */
