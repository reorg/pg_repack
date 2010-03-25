/*-------------------------------------------------------------------------
 *
 * pgut-fe.h
 *
 * Copyright (c) 2009-2010, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#ifndef PGUT_FE_H
#define PGUT_FE_H

#include "pgut.h"

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


extern char	   *dbname;
extern char	   *host;
extern char	   *port;
extern char	   *username;
extern char	   *password;
extern YesNo	prompt_password;

extern PGconn	   *connection;

extern void	pgut_help(bool details);
extern void help(bool details);

extern void disconnect(void);
extern void reconnect(int elevel);
extern PGresult *execute(const char *query, int nParams, const char **params);
extern PGresult *execute_elevel(const char *query, int nParams, const char **params, int elevel);
extern ExecStatusType command(const char *query, int nParams, const char **params);

extern int pgut_getopt(int argc, char **argv, pgut_option options[]);
extern void pgut_readopt(const char *path, pgut_option options[], int elevel);
extern void pgut_setopt(pgut_option *opt, const char *optarg, pgut_optsrc src);
extern bool pgut_keyeq(const char *lhs, const char *rhs);

#endif   /* PGUT_FE_H */
