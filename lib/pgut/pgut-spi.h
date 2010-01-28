/*-------------------------------------------------------------------------
 *
 * pgut-spi.h
 *
 * Copyright (c) 2009-2010, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#ifndef PGUT_SPI_H
#define PGUT_SPI_H

#include "executor/spi.h"

#if PG_VERSION_NUM < 80300

typedef void *SPIPlanPtr;

#endif

#if PG_VERSION_NUM < 80400

extern int SPI_execute_with_args(const char *src, int nargs, Oid *argtypes,
	Datum *values, const char *nulls, bool read_only, long tcount);

#endif

extern void execute(int expected, const char *sql);
extern void execute_plan(int expected, SPIPlanPtr plan, Datum *values, const char *nulls);
extern void execute_with_format(int expected, const char *format, ...)
__attribute__((format(printf, 2, 3)));
extern void execute_with_args(int expected, const char *src, int nargs, Oid argtypes[], Datum values[], const bool nulls[]);
extern void execute_with_format_args(int expected, const char *format, int nargs, Oid argtypes[], Datum values[], const bool nulls[], ...)
__attribute__((format(printf, 2, 7)));

#endif   /* PGUT_SPI_H */
