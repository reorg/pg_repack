/*-------------------------------------------------------------------------
 * pgut-spi.c
 *
 * Portions Copyright (c) 2008-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 * Portions Copyright (c) 2011, Itagaki Takahiro
 * Portions Copyright (c) 2012-2020, The Reorg Development Team
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "pgut-spi.h"
#include "lib/stringinfo.h"

#define EXEC_FAILED(ret, expected) \
	(((expected) > 0 && (ret) != (expected)) || (ret) < 0)

static void
appendStringInfoVA_s(StringInfo str, const char *fmt, va_list args)
__attribute__((format(printf, 2, 0)));

static void
termStringInfo(StringInfo str)
{
	if (str && str->data)
		pfree(str->data);
}

/* appendStringInfoVA + automatic buffer extension */
static void
appendStringInfoVA_s(StringInfo str, const char *fmt, va_list args)
{
	int needed;
	while ((needed = appendStringInfoVA(str, fmt, args)) > 0)
	{
		/* Double the buffer size and try again. */
		enlargeStringInfo(str, needed);
	}
}

/* simple execute */
void
execute(int expected, const char *sql)
{
	int ret = SPI_execute(sql, false, 0);
	if EXEC_FAILED(ret, expected)
		elog(ERROR, "query failed: (sql=%s, code=%d, expected=%d)", sql, ret, expected);
}

/* execute prepared plan */
void
execute_plan(int expected, SPIPlanPtr plan, Datum *values, const char *nulls)
{
	int	ret = SPI_execute_plan(plan, values, nulls, false, 0);
	if EXEC_FAILED(ret, expected)
		elog(ERROR, "query failed: (code=%d, expected=%d)", ret, expected);
}

/* execute sql with format */
void
execute_with_format(int expected, const char *format, ...)
{
	va_list			ap;
	StringInfoData	sql;
	int				ret;

	initStringInfo(&sql);
	va_start(ap, format);
	appendStringInfoVA_s(&sql, format, ap);
	va_end(ap);

	if (sql.len == 0)
		elog(WARNING, "execute_with_format(%s)", format);
	ret = SPI_exec(sql.data, 0);
	if EXEC_FAILED(ret, expected)
		elog(ERROR, "query failed: (sql=%s, code=%d, expected=%d)", sql.data, ret, expected);

	termStringInfo(&sql);
}

void
execute_with_args(int expected, const char *src, int nargs, Oid argtypes[], Datum values[], const bool nulls[])
{
	int		ret;
	int		i;
	char	c_nulls[FUNC_MAX_ARGS];

    	memset(c_nulls, 0, sizeof(c_nulls));

	for (i = 0; i < nargs; i++)
		c_nulls[i] = (nulls[i] ? 'n' : ' ');

	ret = SPI_execute_with_args(src, nargs, argtypes, values, c_nulls, false, 0);
	if EXEC_FAILED(ret, expected)
		elog(ERROR, "query failed: (sql=%s, code=%d, expected=%d)", src, ret, expected);
}

void
execute_with_format_args(int expected, const char *format, int nargs, Oid argtypes[], Datum values[], const bool nulls[], ...)
{
	va_list			ap;
	StringInfoData	sql;

	initStringInfo(&sql);
	va_start(ap, nulls);
	appendStringInfoVA_s(&sql, format, ap);
	va_end(ap);

	execute_with_args(expected, sql.data, nargs, argtypes, values, nulls);

	termStringInfo(&sql);
}
