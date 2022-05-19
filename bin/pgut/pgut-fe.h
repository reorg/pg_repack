/*-------------------------------------------------------------------------
 * pgut-fe.h
 *
 * Portions Copyright (c) 2008-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 * Portions Copyright (c) 2011, Itagaki Takahiro
 * Portions Copyright (c) 2012-2020, The Reorg Development Team
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
 *	f: pgut_optfn
 *	i: 32bit signed integer
 *	l: StringList
 *	u: 32bit unsigned integer
 *	I: 64bit signed integer
 *	U: 64bit unsigned integer
 *	s: string
 *	t: time_t
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

typedef struct worker_conns
{
    int      max_num_workers;
    int      num_workers;
    PGconn **conns;
} worker_conns;



extern const char  *dbname;
extern char	   *host;
extern char	   *port;
extern char	   *username;
extern char	   *password;
extern YesNo	prompt_password;

extern PGconn	   *connection;
extern PGconn      *conn2;
extern worker_conns workers;

extern void	pgut_help(bool details);
extern void help(bool details);

extern void disconnect(void);
extern void reconnect(int elevel);
extern void setup_workers(int num_workers);
extern void disconnect_workers(void);
extern PGresult *execute(const char *query, int nParams, const char **params);
extern PGresult *execute_elevel(const char *query, int nParams, const char **params, int elevel);
extern ExecStatusType command(const char *query, int nParams, const char **params);

extern int pgut_getopt(int argc, char **argv, pgut_option options[]);
extern void pgut_readopt(const char *path, pgut_option options[], int elevel);
extern void pgut_setopt(pgut_option *opt, const char *optarg, pgut_optsrc src);
extern bool pgut_keyeq(const char *lhs, const char *rhs);

/* So we don't need to fret over multiple calls to PQclear(), e.g.
 * in cleanup labels.
 */
#define CLEARPGRES(pgres)  do { PQclear(pgres); pgres = NULL; } while (0)

#endif   /* PGUT_FE_H */
