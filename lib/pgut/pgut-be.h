/*-------------------------------------------------------------------------
 *
 * pgut-be.h
 *
 * Copyright (c) 2009, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#ifndef PGUT_BE_H
#define PGUT_BE_H

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
#define setNewRelfilenode(rel, xid) \
	setNewRelfilenode((rel))
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

typedef void *BulkInsertState;

extern char *text_to_cstring(const text *t);
extern text *cstring_to_text(const char *s);

#define CStringGetTextDatum(s)		PointerGetDatum(cstring_to_text(s))
#define TextDatumGetCString(d)		text_to_cstring((text *) DatumGetPointer(d))

#endif

#endif   /* PGUT_BE_H */
