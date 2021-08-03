/*-------------------------------------------------------------------------
 * pgut-spi.h
 *
 * Portions Copyright (c) 2008-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 * Portions Copyright (c) 2011, Itagaki Takahiro
 * Portions Copyright (c) 2012-2020, The Reorg Development Team
 *-------------------------------------------------------------------------
 */

#ifndef PGUT_SPI_H
#define PGUT_SPI_H

#include "executor/spi.h"

#ifdef _MSC_VER
#define __attribute__(x)
#endif

extern void execute(int expected, const char *sql);
extern void execute_plan(int expected, SPIPlanPtr plan, Datum *values, const char *nulls);
extern void execute_with_format(int expected, const char *format, ...)
__attribute__((format(printf, 2, 3)));
extern void execute_with_args(int expected, const char *src, int nargs, Oid argtypes[], Datum values[], const bool nulls[]);
extern void execute_with_format_args(int expected, const char *format, int nargs, Oid argtypes[], Datum values[], const bool nulls[], ...)
__attribute__((format(printf, 2, 7)));

#endif   /* PGUT_SPI_H */
