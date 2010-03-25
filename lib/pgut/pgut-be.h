/*-------------------------------------------------------------------------
 *
 * pgut-be.h
 *
 * Copyright (c) 2009-2010, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#ifndef PGUT_BE_H
#define PGUT_BE_H

#include "fmgr.h"
#include "utils/tuplestore.h"

#ifndef WIN32

#define PGUT_EXPORT

#else

#define PGUT_EXPORT		__declspec(dllexport)

/*
 * PG_MODULE_MAGIC and PG_FUNCTION_INFO_V1 macros seems to be broken.
 * It uses PGDLLIMPORT, but those objects are not imported from postgres
 * and exported from the user module. So, it should be always dllexported.
 */

#undef PG_MODULE_MAGIC
#define PG_MODULE_MAGIC \
extern PGUT_EXPORT const Pg_magic_struct *PG_MAGIC_FUNCTION_NAME(void); \
const Pg_magic_struct * \
PG_MAGIC_FUNCTION_NAME(void) \
{ \
	static const Pg_magic_struct Pg_magic_data = PG_MODULE_MAGIC_DATA; \
	return &Pg_magic_data; \
} \
extern int no_such_variable

#undef PG_FUNCTION_INFO_V1
#define PG_FUNCTION_INFO_V1(funcname) \
extern PGUT_EXPORT const Pg_finfo_record * CppConcat(pg_finfo_,funcname)(void); \
const Pg_finfo_record * \
CppConcat(pg_finfo_,funcname) (void) \
{ \
	static const Pg_finfo_record my_finfo = { 1 }; \
	return &my_finfo; \
} \
extern int no_such_variable

#endif


#if PG_VERSION_NUM < 80300

#define PGDLLIMPORT					DLLIMPORT
#define SK_BT_DESC					0	/* Always ASC */
#define SK_BT_NULLS_FIRST			0	/* Always NULLS LAST */
#define MaxHeapTupleSize			MaxTupleSize

#define PG_GETARG_TEXT_PP(n)		PG_GETARG_TEXT_P(n)
#define VARSIZE_ANY_EXHDR(v)		(VARSIZE(v) - VARHDRSZ)
#define VARDATA_ANY(v)				VARDATA(v)
#define SET_VARSIZE(v, sz)			(VARATT_SIZEP(v) = (sz))
#define pg_detoast_datum_packed(v)	pg_detoast_datum(v)
#define DatumGetTextPP(v)			DatumGetTextP(v)
#define ItemIdIsNormal(v)			ItemIdIsUsed(v)
#define IndexBuildHeapScan(heap, index, info, sync, callback, state) \
	IndexBuildHeapScan((heap), (index), (info), (callback), (state))
#define planner_rt_fetch(rti, root) \
	 rt_fetch(rti, (root)->parse->rtable)
#define heap_sync(rel)				((void)0)
#define ItemIdIsDead(itemId)		ItemIdDeleted(itemId)
#define GetCurrentCommandId(used)	GetCurrentCommandId()
#define stringToQualifiedNameList(str) \
    stringToQualifiedNameList((str), "pg_bulkload")
#define PageAddItem(page, item, size, offnum, overwrite, is_heap) \
	PageAddItem((page), (item), (size), (offnum), LP_USED)

#endif

#if PG_VERSION_NUM < 80400

#define MAIN_FORKNUM				0
#define HEAP_INSERT_SKIP_WAL	0x0001
#define HEAP_INSERT_SKIP_FSM	0x0002

#define relpath(rnode, forknum)		relpath((rnode))
#define smgrimmedsync(reln, forknum)	smgrimmedsync((reln))
#define smgrread(reln, forknum, blocknum, buffer) \
	smgrread((reln), (blocknum), (buffer))
#define mdclose(reln, forknum)			mdclose((reln))
#define heap_insert(relation, tup, cid, options, bistate) \
	heap_insert((relation), (tup), (cid), true, true)
#define GetBulkInsertState()			(NULL)
#define FreeBulkInsertState(bistate)	((void)0)
#define FreeExprContext(econtext, isCommit)		FreeExprContext((econtext))
#define pgstat_init_function_usage(fcinfo, fcu)		((void)0)
#define pgstat_end_function_usage(fcu, finalize)	((void)0)
#define makeRangeVar(schemaname, relname, location) \
	makeRangeVar((schemaname), (relname))
#define pgstat_track_activity_query_size	PGBE_ACTIVITY_SIZE
typedef void *BulkInsertState;

#define DefineCustomBoolVariable(name, short_desc, long_desc, valueAddr, bootValue, context, flags, assign_hook, show_hook) \
	do { \
		*(valueAddr) = (bootValue); \
		DefineCustomBoolVariable((name), (short_desc), (long_desc), (valueAddr), (context), (assign_hook), (show_hook)); \
	} while(0)
#define DefineCustomIntVariable(name, short_desc, long_desc, valueAddr, bootValue, minValue, maxValue, context, flags, assign_hook, show_hook) \
	do { \
		*(valueAddr) = (bootValue); \
		DefineCustomIntVariable((name), (short_desc), (long_desc), (valueAddr), (minValue), (maxValue), (context), (assign_hook), (show_hook)); \
	} while(0)
#define DefineCustomRealVariable(name, short_desc, long_desc, valueAddr, bootValue, minValue, maxValue, context, flags, assign_hook, show_hook) \
	do { \
		*(valueAddr) = (bootValue); \
		DefineCustomRealVariable((name), (short_desc), (long_desc), (valueAddr), (minValue), (maxValue), (context), (assign_hook), (show_hook)); \
	} while(0)
#define DefineCustomStringVariable(name, short_desc, long_desc, valueAddr, bootValue, context, flags, assign_hook, show_hook) \
	do { \
		*(valueAddr) = (char *) (bootValue); \
		DefineCustomStringVariable((name), (short_desc), (long_desc), (valueAddr), (context), (assign_hook), (show_hook)); \
	} while(0)

struct config_enum_entry
{
	const char *name;
	int			val;
	bool		hidden;
};

extern char *text_to_cstring(const text *t);
extern text *cstring_to_text(const char *s);
extern void tuplestore_putvalues(Tuplestorestate *state, TupleDesc tdesc,
					 Datum *values, bool *isnull);

#define CStringGetTextDatum(s)		PointerGetDatum(cstring_to_text(s))
#define TextDatumGetCString(d)		text_to_cstring((text *) DatumGetPointer(d))

#endif

#if PG_VERSION_NUM < 90000

#define reindex_index(indexId, skip_constraint_checks) \
	reindex_index((indexId))
#define func_signature_string(funcname, nargs, argnames, argtypes) \
	func_signature_string((funcname), (nargs), (argtypes))
#define GetConfigOption(name, restrict_superuser)	GetConfigOption((name))

#endif

#if PG_VERSION_NUM < 80300
#define RelationSetNewRelfilenode(rel, xid) \
	setNewRelfilenode((rel))
#elif PG_VERSION_NUM < 90000
#define RelationSetNewRelfilenode(rel, xid) \
	setNewRelfilenode((rel), (xid))
#endif

#if PG_VERSION_NUM < 80400
#define FuncnameGetCandidates(names, nargs, argnames, variadic, defaults) \
	FuncnameGetCandidates((names), (nargs))
#elif PG_VERSION_NUM < 90000
#define FuncnameGetCandidates(names, nargs, argnames, variadic, defaults) \
	FuncnameGetCandidates((names), (nargs), (variadic), (defaults))
#endif

#endif   /* PGUT_BE_H */
