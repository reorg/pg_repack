/*-------------------------------------------------------------------------
 *
 * pgut-be.h
 *
 * Copyright (c) 2009-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 * Portions Copyright (c) 2012-2020, The Reorg Development Team
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

#endif   /* PGUT_BE_H */
