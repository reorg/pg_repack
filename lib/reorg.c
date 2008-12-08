/*
 * pg_reorg: lib/reorg.c
 *
 * Copyright (c) 2008, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

/**
 * @brief Core Modules
 */

#include "postgres.h"
#include "catalog/namespace.h"
#include "commands/trigger.h"
#include "executor/spi.h"
#include "miscadmin.h"
#include "utils/lsyscache.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#if PG_VERSION_NUM < 80300
#if PG_VERSION_NUM < 80300
#define SET_VARSIZE(PTR, len)	(VARATT_SIZEP((PTR)) = (len))
typedef void *SPIPlanPtr;
#endif
#endif


Datum reorg_trigger(PG_FUNCTION_ARGS);
Datum reorg_apply(PG_FUNCTION_ARGS);
Datum reorg_get_index_keys(PG_FUNCTION_ARGS);
Datum reorg_indexdef(PG_FUNCTION_ARGS);
Datum reorg_swap(PG_FUNCTION_ARGS);
Datum reorg_drop(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(reorg_trigger);
PG_FUNCTION_INFO_V1(reorg_apply);
PG_FUNCTION_INFO_V1(reorg_get_index_keys);
PG_FUNCTION_INFO_V1(reorg_indexdef);
PG_FUNCTION_INFO_V1(reorg_swap);
PG_FUNCTION_INFO_V1(reorg_drop);

static void	reorg_init(void);
static SPIPlanPtr reorg_prepare(const char *src, int nargs, Oid *argtypes);
static void reorg_execp(SPIPlanPtr plan, Datum *values, const char *nulls, int expected);
static void reorg_execf(int expexted, const char *format, ...)
__attribute__((format(printf, 2, 3)));

#define copy_tuple(tuple, tupdesc) \
	PointerGetDatum(SPI_returntuple((tuple), (tupdesc)))

/* check access authority */
static void
must_be_superuser(const char *func)
{
	if (!superuser())
		elog(ERROR, "must be superuser to use %s function", func);
}

#if PG_VERSION_NUM < 80400
static int
SPI_execute_with_args(const char *src,
					  int nargs, Oid *argtypes,
					  Datum *values, const char *nulls,
					  bool read_only, long tcount)
{
	SPIPlanPtr	plan;
	int			ret;

	plan = SPI_prepare(src, nargs, argtypes);
	if (plan == NULL)
		return SPI_result;
	ret = SPI_execute_plan(plan, values, nulls, read_only, tcount);
	SPI_freeplan(plan);
	return ret;
}

static text *
cstring_to_text(const char * s)
{
	int			len = strlen(s);
	text	   *result = palloc(len + VARHDRSZ);

	SET_VARSIZE(result, len + VARHDRSZ);
	memcpy(VARDATA(result), s, len);

	return result;
}
#endif

/**
 * @fn      Datum reorg_trigger(PG_FUNCTION_ARGS)
 * @brief   Insert a operation log into log-table.
 *
 * reorg_trigger(sql)
 *
 * @param	sql	SQL to insert a operation log into log-table.
 */
Datum
reorg_trigger(PG_FUNCTION_ARGS)
{
	TriggerData	   *trigdata = (TriggerData *) fcinfo->context;
	TupleDesc		tupdesc;
	HeapTuple		tuple;
	Datum			values[2];
	char			nulls[2] = { ' ', ' ' };
	Oid				argtypes[2];
	const char	   *sql;
	int				ret;

	/* authority check */
	must_be_superuser("reorg_trigger");

	/* make sure it's called as a trigger at all */
	if (!CALLED_AS_TRIGGER(fcinfo) ||
		!TRIGGER_FIRED_BEFORE(trigdata->tg_event) ||
		!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event) ||
		trigdata->tg_trigger->tgnargs != 1)
		elog(ERROR, "reorg_trigger: invalid trigger call");

	/* retrieve parameters */
	sql = trigdata->tg_trigger->tgargs[0];
	tupdesc = RelationGetDescr(trigdata->tg_relation);
	argtypes[0] = argtypes[1] = trigdata->tg_relation->rd_rel->reltype;

	/* connect to SPI manager */
	reorg_init();

	if (TRIGGER_FIRED_BY_INSERT(trigdata->tg_event))
	{
		/* INSERT: (NULL, newtup) */
		tuple = trigdata->tg_trigtuple;
		nulls[0] = 'n';
		values[1] = copy_tuple(tuple, tupdesc);
	}
	else if (TRIGGER_FIRED_BY_DELETE(trigdata->tg_event))
	{
		/* DELETE: (oldtup, NULL) */
		tuple = trigdata->tg_trigtuple;
		values[0] = copy_tuple(tuple, tupdesc);
		nulls[1] = 'n';
	}
	else
	{
		/* UPDATE: (oldtup, newtup) */
		tuple = trigdata->tg_newtuple;
		values[0] = copy_tuple(trigdata->tg_trigtuple, tupdesc);
		values[1] = copy_tuple(tuple, tupdesc);
	}

	/* INSERT INTO reorg.log VALUES ($1, $2) */
	ret = SPI_execute_with_args(sql, 2, argtypes, values, nulls, false, 1);
	if (ret < 0)
		elog(ERROR, "reorg_trigger: SPI_execp returned %d", ret);

	SPI_finish();

	PG_RETURN_POINTER(tuple);
}

/**
 * @fn      Datum reorg_apply(PG_FUNCTION_ARGS)
 * @brief   Apply operations in log table into temp table.
 *
 * reorg_apply(sql_peek, sql_insert, sql_delete, sql_update, sql_pop,  count)
 *
 * @param	sql_peek	SQL to pop tuple from log table.
 * @param	sql_insert	SQL to insert into temp table.
 * @param	sql_delete	SQL to delete from temp table.
 * @param	sql_update	SQL to update temp table.
 * @param	sql_pop	SQL to delete tuple from log table.
 * @param	count		Max number of operations, or no count iff <=0.
 * @retval				Number of performed operations.
 */
Datum
reorg_apply(PG_FUNCTION_ARGS)
{
#define NUMBER_OF_PROCESSING	1000

	const char *sql_peek = PG_GETARG_CSTRING(0);
	const char *sql_insert = PG_GETARG_CSTRING(1);
	const char *sql_delete = PG_GETARG_CSTRING(2);
	const char *sql_update = PG_GETARG_CSTRING(3);
	const char *sql_pop = PG_GETARG_CSTRING(4);
	int32		count = PG_GETARG_INT32(5);

	SPIPlanPtr		plan_peek = NULL;
	SPIPlanPtr		plan_insert = NULL;
	SPIPlanPtr		plan_delete = NULL;
	SPIPlanPtr		plan_update = NULL;
	SPIPlanPtr		plan_pop = NULL;
	uint32			n, i;
	TupleDesc		tupdesc;
	Oid				argtypes[3];	/* id, pk, row */
	Datum			values[3];		/* id, pk, row */
	char			nulls[3];		/* id, pk, row */
	Oid				argtypes_peek[1] = { INT4OID };
	Datum			values_peek[1];
	char			nulls_peek[1] = { ' ' };

	/* authority check */
	must_be_superuser("reorg_apply");

	/* connect to SPI manager */
	reorg_init();

	/* peek tuple in log */
	plan_peek = reorg_prepare(sql_peek, 1, argtypes_peek);

	for (n = 0;;)
	{
		int				ntuples;
		SPITupleTable  *tuptable;

		/* peek tuple in log */
		if (count == 0)
			values_peek[0] = Int32GetDatum(NUMBER_OF_PROCESSING);
		else
			values_peek[0] = Int32GetDatum(Min(count - n, NUMBER_OF_PROCESSING));
		
		reorg_execp(plan_peek, values_peek, nulls_peek, SPI_OK_SELECT);
		if (SPI_processed <= 0)
			break;

		/* copy tuptable because we will call other sqls. */
		ntuples = SPI_processed;
		tuptable = SPI_tuptable;
		tupdesc = tuptable->tupdesc;
		argtypes[0] = SPI_gettypeid(tupdesc, 1);	/* id */
		argtypes[1] = SPI_gettypeid(tupdesc, 2);	/* pk */
		argtypes[2] = SPI_gettypeid(tupdesc, 3);	/* row */

		for (i = 0; i < ntuples; i++, n++)
		{
			HeapTuple	tuple;
			bool		isnull;
		
			tuple = tuptable->vals[i];
			values[0] = SPI_getbinval(tuple, tupdesc, 1, &isnull);
			nulls[0] = ' ';
			values[1] = SPI_getbinval(tuple, tupdesc, 2, &isnull);
			nulls[1] = (isnull ? 'n' : ' ');
			values[2] = SPI_getbinval(tuple, tupdesc, 3, &isnull);
			nulls[2] = (isnull ? 'n' : ' ');
		
			if (nulls[1] == 'n')
			{
				/* INSERT */
				if (plan_insert == NULL)
					plan_insert = reorg_prepare(sql_insert, 1, &argtypes[2]);
				reorg_execp(plan_insert, &values[2], &nulls[2], SPI_OK_INSERT);
			}
			else if (nulls[2] == 'n')
			{
				/* DELETE */
				if (plan_delete == NULL)
					plan_delete = reorg_prepare(sql_delete, 1, &argtypes[1]);
				reorg_execp(plan_delete, &values[1], &nulls[1], SPI_OK_DELETE);
			}
			else
			{
				/* UPDATE */
				if (plan_update == NULL)
					plan_update = reorg_prepare(sql_update, 2, &argtypes[1]);
				reorg_execp(plan_update, &values[1], &nulls[1], SPI_OK_UPDATE);
			}
		}

		/* delete tuple in log */
		if (plan_pop == NULL)
			plan_pop = reorg_prepare(sql_pop, 1, argtypes);
		reorg_execp(plan_pop, values, nulls, SPI_OK_DELETE);

		SPI_freetuptable(tuptable);
	}

	SPI_finish();

	PG_RETURN_INT32(n);
}

/*
 * Deparsed create index sql. You can rebuild sql using 
 * sprintf(buf, "%s %s ON %s USING %s (%s)%s",
 *		create, index, table type, columns, options)
 */
typedef struct IndexDef
{
	char *create;	/* CREATE INDEX or CREATE UNIQUE INDEX */
	char *index;	/* index name including schema */
	char *table;	/* table name including schema */
	char *type;		/* btree, hash, gist or gin */
	char *columns;	/* column definition */
	char *options;	/* options after columns. WITH, TABLESPACE and WHERE */
} IndexDef;

static char *
generate_relation_name(Oid relid)
{
	Oid		nsp = get_rel_namespace(relid);
	char   *nspname;

	/* Qualify the name if not visible in search path */
	if (RelationIsVisible(relid))
		nspname = NULL;
	else
		nspname = get_namespace_name(nsp);

	return quote_qualified_identifier(nspname, get_rel_name(relid));
}

static char *
parse_error(Oid index)
{
	elog(ERROR, "Unexpected indexdef: %s", pg_get_indexdef_string(index));
	return NULL;
}

static char *
skip_const(Oid index, char *sql, const char *arg1, const char *arg2)
{
	size_t	len;

	if ((arg1 && strncmp(sql, arg1, (len = strlen(arg1))) == 0) ||
		(arg2 && strncmp(sql, arg2, (len = strlen(arg2))) == 0))
	{
		sql[len] = '\0';
		return sql + len + 1;
	}

	/* error */
	return parse_error(index);
}

static char *
skip_ident(Oid index, char *sql)
{
	sql = strchr(sql, ' ');
	if (sql)
	{
		*sql = '\0';
		return sql + 2;
	}

	/* error */
	return parse_error(index);
}

static char *
skip_columns(Oid index, char *sql)
{
	char	instr = 0;
	int		nopen = 1;

	for (; *sql && nopen > 0; sql++)
	{
		if (instr)
		{
			if (sql[0] == instr)
			{
				if (sql[1] == instr)
					sql++;
				else
					instr = 0;
			}
			else if (sql[0] == '\\')
			{
				sql++;	// next char is always string
			}
		}
		else
		{
			switch (sql[0])
			{
				case '(':
					nopen++;
					break;
				case ')':
					nopen--;
					break;
				case '\'':
				case '"':
					instr = sql[0];
					break;
			}
		}
	}

	if (nopen == 0)
	{
		sql[-1] = '\0';
		return sql;
	}

	/* error */
	return parse_error(index);
}

static void
parse_indexdef(IndexDef *stmt, Oid index, Oid table)
{
	char *sql = pg_get_indexdef_string(index);
	const char *idxname = quote_identifier(get_rel_name(index));
	const char *tblname = generate_relation_name(table);

	/* CREATE [UNIQUE] INDEX */
	stmt->create = sql;
	sql = skip_const(index, sql, "CREATE INDEX", "CREATE UNIQUE INDEX");
	/* index */
	stmt->index = sql;
	sql = skip_const(index, sql, idxname, NULL);
	/* ON */
	sql = skip_const(index, sql, "ON", NULL);
	/* table */
	stmt->table = sql;
	sql = skip_const(index, sql, tblname, NULL);
	/* USING */
	sql = skip_const(index, sql, "USING", NULL);
	/* type */
	stmt->type= sql;
	sql = skip_ident(index, sql);
	/* (columns) */
	stmt->columns = sql;
	sql = skip_columns(index, sql);
	/* options */
	stmt->options = sql;
}

/**
 * @fn      Datum reorg_get_index_keys(PG_FUNCTION_ARGS)
 * @brief   Get key definition of the index.
 *
 * reorg_get_index_keys(index, table)
 *
 * @param	index	Oid of target index.
 * @param	table	Oid of table of the index.
 * @retval			Create index DDL for temp table.
 */
Datum
reorg_get_index_keys(PG_FUNCTION_ARGS)
{
	Oid				index = PG_GETARG_OID(0);
	Oid				table = PG_GETARG_OID(1);
	IndexDef		stmt;

	parse_indexdef(&stmt, index, table);

	PG_RETURN_TEXT_P(cstring_to_text(stmt.columns));
}

/**
 * @fn      Datum reorg_indexdef(PG_FUNCTION_ARGS)
 * @brief   Reproduce DDL that create index at the temp table.
 *
 * reorg_indexdef(index, table)
 *
 * @param	index	Oid of target index.
 * @param	table	Oid of table of the index.
 * @retval			Create index DDL for temp table.
 */
Datum
reorg_indexdef(PG_FUNCTION_ARGS)
{
	Oid				index = PG_GETARG_OID(0);
	Oid				table = PG_GETARG_OID(1);
	IndexDef		stmt;
	StringInfoData	str;

	parse_indexdef(&stmt, index, table);
	initStringInfo(&str);
	appendStringInfo(&str, "%s index_%u ON reorg.table_%u USING %s (%s)%s",
		stmt.create, index, table, stmt.type, stmt.columns, stmt.options);

	PG_RETURN_TEXT_P(cstring_to_text(str.data));
}

#define SQL_GET_SWAPINFO "\
SELECT X.oid, X.relfilenode, X.relfrozenxid, Y.oid, Y.relfilenode, Y.relfrozenxid \
FROM pg_class X, pg_class Y \
WHERE X.oid = $1\
 AND Y.oid = ('reorg.table_' || X.oid)::regclass \
UNION ALL \
SELECT T.oid, T.relfilenode, T.relfrozenxid, U.oid, U.relfilenode, U.relfrozenxid \
FROM pg_class X, pg_class T, pg_class Y, pg_class U \
WHERE X.oid = $1\
 AND T.oid = X.reltoastrelid\
 AND Y.oid = ('reorg.table_' || X.oid)::regclass\
 AND U.oid = Y.reltoastrelid \
UNION ALL \
SELECT I.oid, I.relfilenode, I.relfrozenxid, J.oid, J.relfilenode, J.relfrozenxid \
FROM pg_class X, pg_class T, pg_class I, pg_class Y, pg_class U, pg_class J \
WHERE X.oid = $1\
 AND T.oid = X.reltoastrelid\
 AND I.oid = T.reltoastidxid\
 AND Y.oid = ('reorg.table_' || X.oid)::regclass\
 AND U.oid = Y.reltoastrelid\
 AND J.oid = U.reltoastidxid \
UNION ALL \
SELECT X.oid, X.relfilenode, X.relfrozenxid, Y.oid, Y.relfilenode, Y.relfrozenxid \
FROM pg_index I, pg_class X, pg_class Y \
WHERE I.indrelid = $1\
 AND I.indexrelid = X.oid\
 AND Y.oid = ('reorg.index_' || X.oid)::regclass\
 ORDER BY 1\
"

/**
 * @fn      Datum reorg_swap(PG_FUNCTION_ARGS)
 * @brief   Swapping relfilenode of table, toast, toast index
 *          and table indexes on target table and temp table mutually.
 *
 * reorg_swap(oid, relname)
 *
 * @param	oid		Oid of table of target.
 * @retval			None.
 */
Datum
reorg_swap(PG_FUNCTION_ARGS)
{
	Oid				oid = PG_GETARG_OID(0);

	SPIPlanPtr		plan_swapinfo;
	SPIPlanPtr		plan_swap;
	Oid 			argtypes[3] = { OIDOID, OIDOID, XIDOID };
	char 			nulls[3] = { ' ', ' ', ' ' };
	Datum 			values[3];
	int				record;

	/* authority check */
	must_be_superuser("reorg_swap");

	/* connect to SPI manager */
	reorg_init();

	/* parepare */
	plan_swapinfo = reorg_prepare(
		SQL_GET_SWAPINFO,
		1, argtypes);
	plan_swap = reorg_prepare(
		"UPDATE pg_class SET relfilenode = $2, relfrozenxid = $3 WHERE oid = $1",
		3, argtypes);

	/* swap relfilenode */
	values[0] = ObjectIdGetDatum(oid);
	reorg_execp(plan_swapinfo, values, nulls, SPI_OK_SELECT);
	
	record = SPI_processed;

	if (record > 0)
	{
		SPITupleTable	*tuptable;
		TupleDesc		tupdesc;
		HeapTuple		tuple;
		char			isnull;
		int				i;
	
		tuptable = SPI_tuptable;
		tupdesc = tuptable->tupdesc;
	
		for (i = 0; i < record; i++)
		{
			tuple = tuptable->vals[i];
		
			/* target -> temp */
			values[0] = SPI_getbinval(tuple, tupdesc, 4, &isnull);
			values[1] = SPI_getbinval(tuple, tupdesc, 2, &isnull);
			values[2] = SPI_getbinval(tuple, tupdesc, 3, &isnull);
			reorg_execp(plan_swap, values, nulls, SPI_OK_UPDATE);
			/* temp -> target */
			values[0] = SPI_getbinval(tuple, tupdesc, 1, &isnull);
			values[1] = SPI_getbinval(tuple, tupdesc, 5, &isnull);
			values[2] = SPI_getbinval(tuple, tupdesc, 6, &isnull);
			reorg_execp(plan_swap, values, nulls, SPI_OK_UPDATE);
		}
	}

	SPI_finish();

	PG_RETURN_VOID();
}

/**
 * @fn      Datum reorg_drop(PG_FUNCTION_ARGS)
 * @brief   Delete temporarily objects.
 *
 * reorg_drop(oid, relname)
 *
 * @param	oid		Oid of target table.
 * @retval			None.
 */
Datum
reorg_drop(PG_FUNCTION_ARGS)
{
	Oid			oid = PG_GETARG_OID(0);
	const char *relname = quote_identifier(get_rel_name(oid));
	const char *nspname = quote_identifier(get_namespace_name(get_rel_namespace(oid)));

	/* authority check */
	must_be_superuser("reorg_drop");

	/* connect to SPI manager */
	reorg_init();

	/* drop reorg trigger */
	reorg_execf(
		SPI_OK_UTILITY,
		"DROP TRIGGER IF EXISTS z_reorg_trigger ON %s.%s CASCADE",
		nspname, relname);

	/* drop log table */
	reorg_execf(
		SPI_OK_UTILITY,
		"DROP TABLE IF EXISTS reorg.log_%u CASCADE",
		oid);

	/* drop temp table */
	reorg_execf(
		SPI_OK_UTILITY,
		"DROP TABLE IF EXISTS reorg.table_%u CASCADE",
		oid);

	/* drop type for log table */
	reorg_execf(
		SPI_OK_UTILITY,
		"DROP TYPE IF EXISTS reorg.pk_%u CASCADE",
		oid);

	SPI_finish();

	PG_RETURN_VOID();
}

/* init SPI */
static void
reorg_init(void)
{
	int		ret = SPI_connect();
	if (ret != SPI_OK_CONNECT)
		elog(ERROR, "pg_reorg: SPI_connect returned %d", ret);
}

/* prepare plan */
static SPIPlanPtr
reorg_prepare(const char *src, int nargs, Oid *argtypes)
{
	SPIPlanPtr	plan = SPI_prepare(src, nargs, argtypes);
	if (plan == NULL)
		elog(ERROR, "pg_reorg: reorg_prepare failed (code=%d, query=%s)", SPI_result, src);
	return plan;
}

/* execute prepared plan */
static void
reorg_execp(SPIPlanPtr plan, Datum *values, const char *nulls, int expected)
{
	int	ret = SPI_execute_plan(plan, values, nulls, false, 0);
	if (ret != expected)
		elog(ERROR, "pg_reorg: reorg_execp failed (code=%d, expected=%d)", ret, expected);
}

/* execute sql with format */
static void
reorg_execf(int expected, const char *format, ...)
{
	va_list			ap;
	StringInfoData	sql;
	int				ret;

	initStringInfo(&sql);
	va_start(ap, format);
	appendStringInfoVA(&sql, format, ap);
	va_end(ap);

	if ((ret = SPI_exec(sql.data, 0)) != expected)
		elog(ERROR, "pg_reorg: reorg_execf failed (sql=%s, code=%d, expected=%d)", sql.data, ret, expected);
}
