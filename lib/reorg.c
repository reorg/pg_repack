/*
 * pg_reorg: lib/reorg.c
 *
 * Copyright (c) 2008-2010, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#include "postgres.h"

#include <unistd.h>

#include "access/genam.h"
#include "access/transam.h"
#include "access/xact.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_type.h"
#include "commands/tablecmds.h"
#include "commands/trigger.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/relcache.h"
#include "utils/syscache.h"

#include "pgut/pgut-spi.h"
#include "pgut/pgut-be.h"

PG_MODULE_MAGIC;

Datum reorg_version(PG_FUNCTION_ARGS);
Datum reorg_trigger(PG_FUNCTION_ARGS);
Datum reorg_apply(PG_FUNCTION_ARGS);
Datum reorg_get_index_keys(PG_FUNCTION_ARGS);
Datum reorg_indexdef(PG_FUNCTION_ARGS);
Datum reorg_swap(PG_FUNCTION_ARGS);
Datum reorg_drop(PG_FUNCTION_ARGS);
Datum reorg_disable_autovacuum(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(reorg_version);
PG_FUNCTION_INFO_V1(reorg_trigger);
PG_FUNCTION_INFO_V1(reorg_apply);
PG_FUNCTION_INFO_V1(reorg_get_index_keys);
PG_FUNCTION_INFO_V1(reorg_indexdef);
PG_FUNCTION_INFO_V1(reorg_swap);
PG_FUNCTION_INFO_V1(reorg_drop);
PG_FUNCTION_INFO_V1(reorg_disable_autovacuum);

static void	reorg_init(void);
static SPIPlanPtr reorg_prepare(const char *src, int nargs, Oid *argtypes);
static const char *get_quoted_relname(Oid oid);
static const char *get_quoted_nspname(Oid oid);
static void swap_heap_or_index_files(Oid r1, Oid r2);

#define copy_tuple(tuple, desc) \
	PointerGetDatum(SPI_returntuple((tuple), (desc)))

#define IsToken(c) \
	(IS_HIGHBIT_SET((c)) || isalnum((unsigned char) (c)) || (c) == '_')

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

Datum
reorg_version(PG_FUNCTION_ARGS)
{
	return CStringGetTextDatum("pg_reorg 1.0.7");
}

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
	bool			nulls[2] = { 0, 0 };
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
		nulls[0] = true;
		values[1] = copy_tuple(tuple, desc);
	}
	else if (TRIGGER_FIRED_BY_DELETE(trigdata->tg_event))
	{
		/* DELETE: (oldtup, NULL) */
		tuple = trigdata->tg_trigtuple;
		values[0] = copy_tuple(tuple, desc);
		nulls[1] = true;
	}
	else
	{
		/* UPDATE: (oldtup, newtup) */
		tuple = trigdata->tg_newtuple;
		values[0] = copy_tuple(trigdata->tg_trigtuple, desc);
		values[1] = copy_tuple(tuple, desc);
	}

	/* INSERT INTO reorg.log VALUES ($1, $2) */
	execute_with_args(SPI_OK_INSERT, sql, 2, argtypes, values, nulls);

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
#define DEFAULT_PEEK_COUNT	1000

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
	Oid				argtypes_peek[1] = { INT4OID };
	Datum			values_peek[1];
	bool			nulls_peek[1] = { 0 };

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
		TupleDesc		desc;
		Oid				argtypes[3];	/* id, pk, row */
		Datum			values[3];		/* id, pk, row */
		bool			nulls[3];		/* id, pk, row */

		/* peek tuple in log */
		if (count == 0)
			values_peek[0] = Int32GetDatum(DEFAULT_PEEK_COUNT);
		else
			values_peek[0] = Int32GetDatum(Min(count - n, DEFAULT_PEEK_COUNT));

		execute_plan(SPI_OK_SELECT, plan_peek, values_peek, nulls_peek);
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

			tuple = tuptable->vals[i];
			values[0] = SPI_getbinval(tuple, desc, 1, &nulls[0]);
			values[1] = SPI_getbinval(tuple, desc, 2, &nulls[1]);
			values[2] = SPI_getbinval(tuple, desc, 3, &nulls[2]);

			if (nulls[1])
			{
				/* INSERT */
				if (plan_insert == NULL)
					plan_insert = reorg_prepare(sql_insert, 1, &argtypes[2]);
				execute_plan(SPI_OK_INSERT, plan_insert, &values[2], &nulls[2]);
			}
			else if (nulls[2])
			{
				/* DELETE */
				if (plan_delete == NULL)
					plan_delete = reorg_prepare(sql_delete, 1, &argtypes[1]);
				execute_plan(SPI_OK_DELETE, plan_delete, &values[1], &nulls[1]);
			}
			else
			{
				/* UPDATE */
				if (plan_update == NULL)
					plan_update = reorg_prepare(sql_update, 2, &argtypes[1]);
				execute_plan(SPI_OK_UPDATE, plan_update, &values[1], &nulls[1]);
			}
		}

		/* delete tuple in log */
		if (plan_pop == NULL)
			plan_pop = reorg_prepare(sql_pop, 1, argtypes);
		execute_plan(SPI_OK_DELETE, plan_pop, values, nulls);

		SPI_freetuptable(tuptable);
	}

	SPI_finish();

	PG_RETURN_INT32(n);
}

/*
 * Parsed CREATE INDEX statement. You can rebuild sql using 
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
get_relation_name(Oid relid)
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
	elog(ERROR, "unexpected index definition: %s", pg_get_indexdef_string(index));
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
	while (*sql && isspace((unsigned char) *sql))
		sql++;

	if (*sql == '"')
	{
		sql++;
		for (;;)
		{
			char *end = strchr(sql, '"');
			if (end == NULL)
				return parse_error(index);
			else if (end[1] != '"')
			{
				end[1] = '\0';
				return end + 2;
			}
			else	/* escaped quote ("") */
				sql = end + 2;
		}
	}
	else
	{
		while (*sql && IsToken(*sql))
			sql++;
		*sql = '\0';
		return sql + 1;
	}

	/* error */
	return parse_error(index);
}

static char *
skip_until(Oid index, char *sql, char end)
{
	char	instr = 0;
	int		nopen = 0;

	sql++;
	for (; *sql && (nopen > 0 || instr != 0 || *sql != end); sql++)
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
				sql++;	/* next char is always string */
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

	if (nopen == 0 && instr == 0)
	{
		*sql = '\0';
		return sql + 1;
	}

	/* error */
	return parse_error(index);
}

static void
parse_indexdef(IndexDef *stmt, Oid index, Oid table)
{
	char *sql = pg_get_indexdef_string(index);
	const char *idxname = get_quoted_relname(index);
	const char *tblname = get_relation_name(table);

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
	stmt->type = sql;
	sql = skip_ident(index, sql);
	/* (columns) */
	if ((sql = strchr(sql, '(')) == NULL)
		parse_error(index);
	sql++;
	stmt->columns = sql;
	sql = skip_until(index, sql, ')');
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
 *
 * FIXME: this function is named get_index_keys, but actually returns
 * an expression for ORDER BY clause. get_order_by() might be a better name.
 */
Datum
reorg_get_index_keys(PG_FUNCTION_ARGS)
{
	Oid				index = PG_GETARG_OID(0);
	Oid				table = PG_GETARG_OID(1);
	IndexDef		stmt;
	char		   *token;
	char		   *next;
	StringInfoData	str;
	Relation		indexRel = NULL;
	int				nattr;

	parse_indexdef(&stmt, index, table);

	/*
	 * FIXME: this is very unreliable implementation but I don't want to
	 * re-implement customized versions of pg_get_indexdef_string...
	 *
	 * TODO: Support ASC/DESC and NULL FIRST/LAST.
	 */

	initStringInfo(&str);
	for (nattr = 0, next = stmt.columns; *next; nattr++)
	{
		char *opcname;

		token = next;
		next = skip_until(index, next, ',');

		opcname = token + strlen(token);
		if (opcname[-1] == '"')
		{
			opcname--;
			for (;;)
			{
				char *beg = strrchr(opcname, '"');
				if (beg == NULL)
					parse_error(index);
				else if (beg[-1] != '"')
					break;
				else	/* escaped quote ("") */
					opcname = beg - 1;
			}
		}
		else
		{
			while (opcname > token && IsToken(opcname[-1]))
				opcname--;
		}

		if (opcname > token && *opcname)
		{
			/* lookup default operator name from operator class */

			Oid				opclass;
			Oid				oprid;
			int16			strategy = BTLessStrategyNumber;
#if PG_VERSION_NUM >= 80300
			Oid				opcintype;
			Oid				opfamily;
			HeapTuple		tp;
			Form_pg_opclass	opclassTup;
#endif
			
			opclass = OpclassnameGetOpcid(BTREE_AM_OID, opcname);

#if PG_VERSION_NUM >= 80300
			/* Retrieve operator information. */
			tp = SearchSysCache(CLAOID, ObjectIdGetDatum(opclass), 0, 0, 0);
			if (!HeapTupleIsValid(tp))
				elog(ERROR, "cache lookup failed for opclass %u", opclass);
			opclassTup = (Form_pg_opclass) GETSTRUCT(tp);
			opfamily = opclassTup->opcfamily;
			opcintype = opclassTup->opcintype;
			ReleaseSysCache(tp);

			if (!OidIsValid(opcintype))
			{
				if (indexRel == NULL)
					indexRel = index_open(index, NoLock);

				opcintype = RelationGetDescr(indexRel)->attrs[nattr]->atttypid;
			}

			oprid = get_opfamily_member(opfamily, opcintype, opcintype, strategy);
			if (!OidIsValid(oprid))
				elog(ERROR, "missing operator %d(%u,%u) in opfamily %u",
					 strategy, opcintype, opcintype, opfamily);
#else
			oprid = get_opclass_member(opclass, 0, strategy);
			if (!OidIsValid(oprid))
				elog(ERROR, "missing operator %d for %s", strategy, opcname);
#endif

				opcname[-1] = '\0';
			appendStringInfo(&str, "%s USING %s", token, get_opname(oprid));
		}
		else
			appendStringInfoString(&str, token);
		if (*next)
			appendStringInfoChar(&str, ',');
	}

	if (indexRel != NULL)
		index_close(indexRel, NoLock);

	PG_RETURN_TEXT_P(cstring_to_text(str.data));
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

static int16
getint16(HeapTuple tuple, TupleDesc desc, int column)
{
	bool	isnull;
	Datum	datum = SPI_getbinval(tuple, desc, column, &isnull);
	return isnull ? 0 : DatumGetInt16(datum);
}

static void
remove_dropped_columns_and_adjust_attnum(Oid oid, int16 natts1, int16 natts2)
{
	/* delete dropped columns */
	execute_with_format(SPI_OK_DELETE,
		"DELETE FROM pg_catalog.pg_attribute"
		" WHERE attrelid = %u AND attisdropped",
		oid);
	if (SPI_processed != natts1 - natts2)
		elog(ERROR, "cannot remove %d dropped columns (%u columns removed)",
			natts2 - natts1, SPI_processed);

	/* renumber attnum */
#if PG_VERSION_NUM >= 80500
	execute_with_format(SPI_OK_UPDATE,
		"UPDATE pg_catalog.pg_attribute m"
		"   SET attnum = (SELECT count(*) FROM pg_attribute a"
		" WHERE m.attrelid = a.attrelid"
		"   AND m.attnum >= a.attnum"
		"   AND a.attnum > 0 AND NOT a.attisdropped)"
		" WHERE attrelid = %u AND attnum > 0 AND NOT attisdropped",
		oid);
	if (SPI_processed != natts2)
		elog(ERROR, "cannot update %d columns (%u columns updated)",
			natts2, SPI_processed);
#elif PG_VERSION_NUM >= 80300
	execute_with_format(SPI_OK_UPDATE,
		"UPDATE pg_catalog.pg_attribute"
		"   SET attnum = (SELECT count(*) FROM pg_attribute a"
		" WHERE pg_catalog.pg_attribute.attrelid = a.attrelid"
		"   AND pg_catalog.pg_attribute.attnum >= a.attnum"
		"   AND a.attnum > 0 AND NOT a.attisdropped)"
		" WHERE attrelid = %u AND attnum > 0 AND NOT attisdropped",
		oid);
	if (SPI_processed != natts2)
		elog(ERROR, "cannot update %d columns (%u columns updated)",
			natts2, SPI_processed);
#else
	/*
	 * Use count(*) in subquery because 8.2 doesn't support aggregates
	 * in UPDATE SET.
	 */
	do
	{
		uint32			i;
		uint32			ntuples;
		SPITupleTable  *tuptable;
		TupleDesc		desc;

		execute_with_format(SPI_OK_SELECT,
			"SELECT attnum FROM pg_catalog.pg_attribute"
			" WHERE attrelid = %u AND attnum > 0 AND NOT attisdropped"
			" ORDER BY attnum",
			oid);
		if (SPI_processed != natts2)
			elog(ERROR, "number of columns should be %d (%d returned)",
				natts2, SPI_processed);

		ntuples = SPI_processed;
		tuptable = SPI_tuptable;
		desc = tuptable->tupdesc;

		for (i = 0; i < ntuples; i++)
		{
			int		attnum;
			int		count;

			attnum = getint16(tuptable->vals[i], desc, 1);

			execute_with_format(SPI_OK_SELECT,
				"SELECT count(*)::smallint FROM pg_catalog.pg_attribute"
				" WHERE attrelid = %u AND attnum > 0 AND attnum <= %d",
				oid, attnum);
			if (SPI_processed != 1)
				elog(ERROR, "cannot adjust column %d", attnum);

			count = getint16(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1);

			execute_with_format(SPI_OK_UPDATE,
				"UPDATE pg_catalog.pg_attribute"
				"   SET attnum = %d"
				" WHERE attrelid = %u AND attnum = %d",
				count, oid, attnum);
			if (SPI_processed != 1)
				elog(ERROR, "cannot update column %d", attnum);
		}
	} while(0);
#endif

	/* adjust attribute number of the table */
	execute_with_format(SPI_OK_UPDATE,
		"UPDATE pg_catalog.pg_class SET relnatts = %d WHERE oid = %u",
		natts2, oid);
}

/**
 * @fn      Datum reorg_swap(PG_FUNCTION_ARGS)
 * @brief   Swapping relfilenode of tables and relation ids of toast tables
 *          and toast indexes.
 *
 * reorg_swap(oid, relname)
 *
 * TODO: remove useless CommandCounterIncrement().
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
	bool	 		nulls[1] = { 0 };
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
	int16			natts1;
	int16			natts2;

	/* authority check */
	must_be_superuser("reorg_swap");

	/* connect to SPI manager */
	reorg_init();

	/* swap relfilenode and dependencies for tables. */
	values[0] = ObjectIdGetDatum(oid);
	execute_with_args(SPI_OK_SELECT,
		"SELECT X.reltoastrelid, TX.reltoastidxid, X.relowner,"
		"       Y.oid, Y.reltoastrelid, TY.reltoastidxid, Y.relowner,"
		"       X.relnatts, Y.relnatts"
		"  FROM pg_catalog.pg_class X LEFT JOIN pg_catalog.pg_class TX"
		"         ON X.reltoastrelid = TX.oid,"
		"       pg_catalog.pg_class Y LEFT JOIN pg_catalog.pg_class TY"
		"         ON Y.reltoastrelid = TY.oid"
		" WHERE X.oid = $1"
		"   AND Y.oid = ('reorg.table_' || X.oid)::regclass",
		1, argtypes, values, nulls);

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
	natts1 = getint16(tuple, desc, 8);
	natts2 = getint16(tuple, desc, 9);;

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

	/* swap tables. */
	swap_heap_or_index_files(oid, oid2);
	CommandCounterIncrement();

	/* swap indexes. */
	values[0] = ObjectIdGetDatum(oid);
	execute_with_args(SPI_OK_SELECT,
		"SELECT X.oid, Y.oid"
		"  FROM pg_catalog.pg_index I,"
		"       pg_catalog.pg_class X,"
		"       pg_catalog.pg_class Y"
		" WHERE I.indrelid = $1"
		"   AND I.indexrelid = X.oid"
		"   AND Y.oid = ('reorg.index_' || X.oid)::regclass",
		1, argtypes, values, nulls);

	tuptable = SPI_tuptable;
	desc = tuptable->tupdesc;
	records = SPI_processed;

	for (i = 0; i < records; i++)
	{
		Oid		idx1, idx2;

		tuple = tuptable->vals[i];
		idx1 = getoid(tuple, desc, 1);
		idx2 = getoid(tuple, desc, 2);
		swap_heap_or_index_files(idx1, idx2);

		/* adjust key attnum if the target table has dropped columns */
		if (natts1 != natts2)
		{
#if PG_VERSION_NUM >= 80500
			execute_with_format(SPI_OK_UPDATE,
				"UPDATE pg_catalog.pg_index m SET indkey = n.indkey"
				"  FROM pg_catalog.pg_index n"
				" WHERE m.indexrelid = %u"
				"   AND n.indexrelid = 'reorg.index_%u'::regclass",
				idx1, idx1);
#else
			execute_with_format(SPI_OK_UPDATE,
				"UPDATE pg_catalog.pg_index SET indkey = n.indkey"
				"  FROM pg_catalog.pg_index n"
				" WHERE pg_catalog.pg_index.indexrelid = %u"
				"   AND n.indexrelid = 'reorg.index_%u'::regclass",
				idx1, idx1);
#endif
			if (SPI_processed != 1)
				elog(ERROR, "failed to update pg_index.indkey (%u rows updated)", SPI_processed);
		}
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

	/* adjust attribute numbers if the target table has dropped columns */
	if (natts1 != natts2)
		remove_dropped_columns_and_adjust_attnum(oid, natts1, natts2);

	/* drop reorg trigger */
	execute_with_format(
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
	execute_with_format(
		SPI_OK_UTILITY,
		"DROP TRIGGER IF EXISTS z_reorg_trigger ON %s.%s CASCADE",
		nspname, relname);

#if PG_VERSION_NUM < 80400
	/* delete autovacuum settings */
	execute_with_format(
		SPI_OK_DELETE,
		"DELETE FROM pg_catalog.pg_autovacuum v"
		" USING pg_class c, pg_namespace n"
		" WHERE relname IN ('log_%u', 'table_%u')"
		"   AND n.nspname = 'reorg'"
		"   AND c.relnamespace = n.oid"
		"   AND v.vacrelid = c.oid",
		oid, oid);
#endif

	/* drop log table */
	execute_with_format(
		SPI_OK_UTILITY,
		"DROP TABLE IF EXISTS reorg.log_%u CASCADE",
		oid);

	/* drop temp table */
	execute_with_format(
		SPI_OK_UTILITY,
		"DROP TABLE IF EXISTS reorg.table_%u CASCADE",
		oid);

	/* drop type for log table */
	execute_with_format(
		SPI_OK_UTILITY,
		"DROP TYPE IF EXISTS reorg.pk_%u CASCADE",
		oid);

	SPI_finish();

	PG_RETURN_VOID();
}

Datum
reorg_disable_autovacuum(PG_FUNCTION_ARGS)
{
	Oid			oid = PG_GETARG_OID(0);

	/* connect to SPI manager */
	reorg_init();

#if PG_VERSION_NUM >= 80400
	execute_with_format(
		SPI_OK_UTILITY,
		"ALTER TABLE %s SET (autovacuum_enabled = off)",
		get_relation_name(oid));
#else
	execute_with_format(
		SPI_OK_INSERT,
		"INSERT INTO pg_catalog.pg_autovacuum VALUES (%u, false, -1, -1, -1, -1, -1, -1, -1, -1)",
		oid);
#endif

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
