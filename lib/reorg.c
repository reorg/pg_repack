/*
 * pg_reorg: lib/reorg.c
 *
 * Copyright (c) 2008-2009, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

/**
 * @brief Core Modules
 */

#include "postgres.h"

#include <unistd.h>

#include "access/transam.h"
#include "access/xact.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_type.h"
#include "commands/tablecmds.h"
#include "commands/trigger.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/relcache.h"
#include "utils/syscache.h"

#include "pgut/pgut-be.h"

PG_MODULE_MAGIC;

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
static void reorg_execd(const char *src, int nargs, Oid *argtypes, Datum *values, const char *nulls, int expected);
static const char *get_quoted_relname(Oid oid);
static const char *get_quoted_nspname(Oid oid);
static void swap_heap_or_index_files(Oid r1, Oid r2);

#define copy_tuple(tuple, desc) \
	PointerGetDatum(SPI_returntuple((tuple), (desc)))

/* check access authority */
static void
must_be_superuser(const char *func)
{
	if (!superuser())
		elog(ERROR, "must be superuser to use %s function", func);
}

#if PG_VERSION_NUM < 80400
static void RenameRelationInternal(Oid myrelid, const char *newrelname, Oid namespaceId);
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
	TupleDesc		desc;
	HeapTuple		tuple;
	Datum			values[2];
	char			nulls[2] = { ' ', ' ' };
	Oid				argtypes[2];
	const char	   *sql;

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
	desc = RelationGetDescr(trigdata->tg_relation);
	argtypes[0] = argtypes[1] = trigdata->tg_relation->rd_rel->reltype;

	/* connect to SPI manager */
	reorg_init();

	if (TRIGGER_FIRED_BY_INSERT(trigdata->tg_event))
	{
		/* INSERT: (NULL, newtup) */
		tuple = trigdata->tg_trigtuple;
		nulls[0] = 'n';
		values[1] = copy_tuple(tuple, desc);
	}
	else if (TRIGGER_FIRED_BY_DELETE(trigdata->tg_event))
	{
		/* DELETE: (oldtup, NULL) */
		tuple = trigdata->tg_trigtuple;
		values[0] = copy_tuple(tuple, desc);
		nulls[1] = 'n';
	}
	else
	{
		/* UPDATE: (oldtup, newtup) */
		tuple = trigdata->tg_newtuple;
		values[0] = copy_tuple(trigdata->tg_trigtuple, desc);
		values[1] = copy_tuple(tuple, desc);
	}

	/* INSERT INTO reorg.log VALUES ($1, $2) */
	reorg_execd(sql, 2, argtypes, values, nulls, SPI_OK_INSERT);

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
	TupleDesc		desc;
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
		desc = tuptable->tupdesc;
		argtypes[0] = SPI_gettypeid(desc, 1);	/* id */
		argtypes[1] = SPI_gettypeid(desc, 2);	/* pk */
		argtypes[2] = SPI_gettypeid(desc, 3);	/* row */

		for (i = 0; i < ntuples; i++, n++)
		{
			HeapTuple	tuple;
			bool		isnull;

			tuple = tuptable->vals[i];
			values[0] = SPI_getbinval(tuple, desc, 1, &isnull);
			nulls[0] = ' ';
			values[1] = SPI_getbinval(tuple, desc, 2, &isnull);
			nulls[1] = (isnull ? 'n' : ' ');
			values[2] = SPI_getbinval(tuple, desc, 3, &isnull);
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
	const char *idxname = get_quoted_relname(index);
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

static Oid
getoid(HeapTuple tuple, TupleDesc desc, int column)
{
	bool	isnull;
	Datum	datum = SPI_getbinval(tuple, desc, column, &isnull);
	return isnull ? InvalidOid : DatumGetObjectId(datum);
}

/**
 * @fn      Datum reorg_swap(PG_FUNCTION_ARGS)
 * @brief   Swapping relfilenode of tables and relation ids of toast tables
 *          and toast indexes.
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
	const char	   *relname = get_quoted_relname(oid);
	const char	   *nspname = get_quoted_nspname(oid);
	Oid 			argtypes[1] = { OIDOID };
	char	 		nulls[1] = { ' ' };
	Datum	 		values[1];
	SPITupleTable  *tuptable;
	TupleDesc		desc;
	HeapTuple		tuple;
	uint32			records;
	uint32			i;

	Oid				reltoastrelid1;
	Oid				reltoastidxid1;
	Oid				oid2;
	Oid				reltoastrelid2;
	Oid				reltoastidxid2;
	Oid				owner1;
	Oid				owner2;

	/* authority check */
	must_be_superuser("reorg_swap");

	/* connect to SPI manager */
	reorg_init();

	/* swap relfilenode and dependencies for tables. */
	values[0] = ObjectIdGetDatum(oid);
	reorg_execd(
		"SELECT X.reltoastrelid, TX.reltoastidxid, X.relowner,"
		"       Y.oid, Y.reltoastrelid, TY.reltoastidxid, Y.relowner"
		"  FROM pg_catalog.pg_class X LEFT JOIN pg_catalog.pg_class TX"
		"         ON X.reltoastrelid = TX.oid,"
		"       pg_catalog.pg_class Y LEFT JOIN pg_catalog.pg_class TY"
		"         ON Y.reltoastrelid = TY.oid"
		" WHERE X.oid = $1"
		"   AND Y.oid = ('reorg.table_' || X.oid)::regclass",
		1, argtypes, values, nulls, SPI_OK_SELECT);

	tuptable = SPI_tuptable;
	desc = tuptable->tupdesc;
	records = SPI_processed;

	if (records == 0)
		elog(ERROR, "reorg_swap : no swap target");

	tuple = tuptable->vals[0];

	reltoastrelid1 = getoid(tuple, desc, 1);
	reltoastidxid1 = getoid(tuple, desc, 2);
	owner1 = getoid(tuple, desc, 3);
	oid2 = getoid(tuple, desc, 4);
	reltoastrelid2 = getoid(tuple, desc, 5);
	reltoastidxid2 = getoid(tuple, desc, 6);
	owner2 = getoid(tuple, desc, 7);

	/* should be all-or-nothing */
	if ((reltoastrelid1 == InvalidOid || reltoastidxid1 == InvalidOid ||
		 reltoastrelid2 == InvalidOid || reltoastidxid2 == InvalidOid) &&
		(reltoastrelid1 != InvalidOid || reltoastidxid1 != InvalidOid ||
		 reltoastrelid2 != InvalidOid || reltoastidxid2 != InvalidOid))
	{
		elog(ERROR, "reorg_swap : unexpected toast relations (T1=%u, I1=%u, T2=%u, I2=%u",
			reltoastrelid1, reltoastidxid1, reltoastrelid2, reltoastidxid2);
	}

	/* change owner of new relation to original owner */
	if (owner1 != owner2)
	{
		ATExecChangeOwner(oid2, owner1, true);
		CommandCounterIncrement();
	}

	/* swap heap and index files */
	swap_heap_or_index_files(oid, oid2);
	CommandCounterIncrement();

	/* swap relfilenode and dependencies for indxes. */
	values[0] = ObjectIdGetDatum(oid);
	reorg_execd(
		"SELECT X.oid, Y.oid"
		"  FROM pg_catalog.pg_index I,"
		"       pg_catalog.pg_class X,"
		"       pg_catalog.pg_class Y"
		" WHERE I.indrelid = $1"
		"   AND I.indexrelid = X.oid"
		"   AND Y.oid = ('reorg.index_' || X.oid)::regclass",
		1, argtypes, values, nulls, SPI_OK_SELECT);

	tuptable = SPI_tuptable;
	desc = tuptable->tupdesc;
	records = SPI_processed;

	for (i = 0; i < records; i++)
	{
		tuple = tuptable->vals[i];
		swap_heap_or_index_files(
			getoid(tuple, desc, 1),
			getoid(tuple, desc, 2));
		CommandCounterIncrement();
	}

	/* swap names for toast tables and toast indexes */
	if (reltoastrelid1 != InvalidOid)
	{
		char	name[NAMEDATALEN];
		int		pid = getpid();

		/* rename X to TEMP */
		snprintf(name, NAMEDATALEN, "pg_toast_pid%d", pid);
		RenameRelationInternal(reltoastrelid1, name, PG_TOAST_NAMESPACE);
		snprintf(name, NAMEDATALEN, "pg_toast_pid%d_index", pid);
		RenameRelationInternal(reltoastidxid1, name, PG_TOAST_NAMESPACE);
		CommandCounterIncrement();

		/* rename Y to X */
		snprintf(name, NAMEDATALEN, "pg_toast_%u", oid);
		RenameRelationInternal(reltoastrelid2, name, PG_TOAST_NAMESPACE);
		snprintf(name, NAMEDATALEN, "pg_toast_%u_index", oid);
		RenameRelationInternal(reltoastidxid2, name, PG_TOAST_NAMESPACE);
		CommandCounterIncrement();

		/* rename TEMP to Y */
		snprintf(name, NAMEDATALEN, "pg_toast_%u", oid2);
		RenameRelationInternal(reltoastrelid1, name, PG_TOAST_NAMESPACE);
		snprintf(name, NAMEDATALEN, "pg_toast_%u_index", oid2);
		RenameRelationInternal(reltoastidxid1, name, PG_TOAST_NAMESPACE);
		CommandCounterIncrement();
	}

	/* drop reorg trigger */
	reorg_execf(
		SPI_OK_UTILITY,
		"DROP TRIGGER IF EXISTS z_reorg_trigger ON %s.%s CASCADE",
		nspname, relname);

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
	const char *relname = get_quoted_relname(oid);
	const char *nspname = get_quoted_nspname(oid);

	/* authority check */
	must_be_superuser("reorg_drop");

	/* connect to SPI manager */
	reorg_init();

	/*
	 * drop reorg trigger: We have already dropped the trigger in normal
	 * cases, but it can be left on error.
	 */
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

/* execute a query */
static void
reorg_execd(const char *src, int nargs, Oid *argtypes, Datum *values, const char *nulls, int expected)
{
	int ret = SPI_execute_with_args(src, nargs, argtypes, values, nulls, expected == SPI_OK_SELECT, 0);
	if (ret != expected)
		elog(ERROR, "pg_reorg: reorg_execd failed (sql=%s, code=%d, expected=%d)", src, ret, expected);
}

static const char *
get_quoted_relname(Oid oid)
{
	return quote_identifier(get_rel_name(oid));
}

static const char *
get_quoted_nspname(Oid oid)
{
	return quote_identifier(get_namespace_name(get_rel_namespace(oid)));
}

/*
 * This is a copy of swap_relation_files in cluster.c, but it also swaps
 * relfrozenxid.
 */
static void
swap_heap_or_index_files(Oid r1, Oid r2)
{
	Relation	relRelation;
	HeapTuple	reltup1,
				reltup2;
	Form_pg_class relform1,
				relform2;
	Oid			swaptemp;
	CatalogIndexState indstate;

	/* We need writable copies of both pg_class tuples. */
	relRelation = heap_open(RelationRelationId, RowExclusiveLock);

	reltup1 = SearchSysCacheCopy(RELOID,
								 ObjectIdGetDatum(r1),
								 0, 0, 0);
	if (!HeapTupleIsValid(reltup1))
		elog(ERROR, "cache lookup failed for relation %u", r1);
	relform1 = (Form_pg_class) GETSTRUCT(reltup1);

	reltup2 = SearchSysCacheCopy(RELOID,
								 ObjectIdGetDatum(r2),
								 0, 0, 0);
	if (!HeapTupleIsValid(reltup2))
		elog(ERROR, "cache lookup failed for relation %u", r2);
	relform2 = (Form_pg_class) GETSTRUCT(reltup2);

	Assert(relform1->relkind == relform2->relkind);

	/*
	 * Actually swap the fields in the two tuples
	 */
	swaptemp = relform1->relfilenode;
	relform1->relfilenode = relform2->relfilenode;
	relform2->relfilenode = swaptemp;

	swaptemp = relform1->reltablespace;
	relform1->reltablespace = relform2->reltablespace;
	relform2->reltablespace = swaptemp;

	swaptemp = relform1->reltoastrelid;
	relform1->reltoastrelid = relform2->reltoastrelid;
	relform2->reltoastrelid = swaptemp;

	/* set rel1's frozen Xid to larger one */
	if (TransactionIdIsNormal(relform1->relfrozenxid))
	{
		if (TransactionIdFollows(relform1->relfrozenxid,
								 relform2->relfrozenxid))
			relform1->relfrozenxid = relform2->relfrozenxid;
		else
			relform2->relfrozenxid = relform1->relfrozenxid;
	}

	/* swap size statistics too, since new rel has freshly-updated stats */
	{
		int4		swap_pages;
		float4		swap_tuples;

		swap_pages = relform1->relpages;
		relform1->relpages = relform2->relpages;
		relform2->relpages = swap_pages;

		swap_tuples = relform1->reltuples;
		relform1->reltuples = relform2->reltuples;
		relform2->reltuples = swap_tuples;
	}

	/* Update the tuples in pg_class */
	simple_heap_update(relRelation, &reltup1->t_self, reltup1);
	simple_heap_update(relRelation, &reltup2->t_self, reltup2);

	/* Keep system catalogs current */
	indstate = CatalogOpenIndexes(relRelation);
	CatalogIndexInsert(indstate, reltup1);
	CatalogIndexInsert(indstate, reltup2);
	CatalogCloseIndexes(indstate);

	/*
	 * If we have toast tables associated with the relations being swapped,
	 * change their dependency links to re-associate them with their new
	 * owning relations.  Otherwise the wrong one will get dropped ...
	 *
	 * NOTE: it is possible that only one table has a toast table; this can
	 * happen in CLUSTER if there were dropped columns in the old table, and
	 * in ALTER TABLE when adding or changing type of columns.
	 *
	 * NOTE: at present, a TOAST table's only dependency is the one on its
	 * owning table.  If more are ever created, we'd need to use something
	 * more selective than deleteDependencyRecordsFor() to get rid of only the
	 * link we want.
	 */
	if (relform1->reltoastrelid || relform2->reltoastrelid)
	{
		ObjectAddress baseobject,
					toastobject;
		long		count;

		/* Delete old dependencies */
		if (relform1->reltoastrelid)
		{
			count = deleteDependencyRecordsFor(RelationRelationId,
											   relform1->reltoastrelid);
			if (count != 1)
				elog(ERROR, "expected one dependency record for TOAST table, found %ld",
					 count);
		}
		if (relform2->reltoastrelid)
		{
			count = deleteDependencyRecordsFor(RelationRelationId,
											   relform2->reltoastrelid);
			if (count != 1)
				elog(ERROR, "expected one dependency record for TOAST table, found %ld",
					 count);
		}

		/* Register new dependencies */
		baseobject.classId = RelationRelationId;
		baseobject.objectSubId = 0;
		toastobject.classId = RelationRelationId;
		toastobject.objectSubId = 0;

		if (relform1->reltoastrelid)
		{
			baseobject.objectId = r1;
			toastobject.objectId = relform1->reltoastrelid;
			recordDependencyOn(&toastobject, &baseobject, DEPENDENCY_INTERNAL);
		}

		if (relform2->reltoastrelid)
		{
			baseobject.objectId = r2;
			toastobject.objectId = relform2->reltoastrelid;
			recordDependencyOn(&toastobject, &baseobject, DEPENDENCY_INTERNAL);
		}
	}

	/*
	 * Blow away the old relcache entries now.	We need this kluge because
	 * relcache.c keeps a link to the smgr relation for the physical file, and
	 * that will be out of date as soon as we do CommandCounterIncrement.
	 * Whichever of the rels is the second to be cleared during cache
	 * invalidation will have a dangling reference to an already-deleted smgr
	 * relation.  Rather than trying to avoid this by ordering operations just
	 * so, it's easiest to not have the relcache entries there at all.
	 * (Fortunately, since one of the entries is local in our transaction,
	 * it's sufficient to clear out our own relcache this way; the problem
	 * cannot arise for other backends when they see our update on the
	 * non-local relation.)
	 */
	RelationForgetRelation(r1);
	RelationForgetRelation(r2);

	/* Clean up. */
	heap_freetuple(reltup1);
	heap_freetuple(reltup2);

	heap_close(relRelation, RowExclusiveLock);
}

#if PG_VERSION_NUM < 80400

extern PGDLLIMPORT bool allowSystemTableMods;

static void
RenameRelationInternal(Oid myrelid, const char *newrelname, Oid namespaceId)
{
	bool	save_allowSystemTableMods = allowSystemTableMods;

	allowSystemTableMods = true;
	PG_TRY();
	{
		renamerel(myrelid, newrelname
#if PG_VERSION_NUM >= 80300
			, OBJECT_TABLE
#endif
			);
		allowSystemTableMods = save_allowSystemTableMods;
	}
	PG_CATCH();
	{
		allowSystemTableMods = save_allowSystemTableMods;
		PG_RE_THROW();
	}
	PG_END_TRY();
}
#endif
