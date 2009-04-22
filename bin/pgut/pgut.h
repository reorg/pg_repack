/*
 * pgut.h
 *
 * Copyright (c) 2009, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#ifndef PGUT_H
#define PGUT_H

#include "libpq-fe.h"
#include <getopt.h>

/*
 * pgut client variables and functions
 */
extern const char		   *pgut_optstring;
extern const struct option	pgut_longopts[];

extern pqbool	pgut_argument(int c, const char *arg);
extern int		pgut_help(void);
extern int		pgut_version(void);
extern void		pgut_cleanup(pqbool fatal);

/*
 * exit codes
 */

#define EXITCODE_OK		0	/**< normal exit */
#define EXITCODE_ERROR	1	/**< normal error */
#define EXITCODE_HELP	2	/**< help and version mode */
#define EXITCODE_FATAL	3	/**< fatal error */

/*
 * pgut framework variables and functions
 */

#ifndef true
#define true	1
#endif
#ifndef false
#define false	0
#endif

extern const char  *progname;
extern const char  *dbname;
extern char		   *host;
extern char		   *port;
extern char		   *username;
extern pqbool		password;
extern pqbool		interrupted;
extern PGconn	   *current_conn;

extern int	pgut_getopt(int argc, char **argv);

extern void reconnect(void);
extern void disconnect(void);
extern PGresult *execute_nothrow(const char *query, int nParams, const char **params);
extern PGresult *execute(const char *query, int nParams, const char **params);
extern void command(const char *query, int nParams, const char **params);

#ifdef WIN32
extern unsigned int sleep(unsigned int seconds);
#endif

#endif   /* PGUT_H */
