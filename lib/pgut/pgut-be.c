/*-------------------------------------------------------------------------
 *
 * pgut-be.c
 *
 * Copyright (c) 2009-2010, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "access/heapam.h"
#include "pgut-be.h"

#if PG_VERSION_NUM < 80400

char *
text_to_cstring(const text *t)
{
	text	   *tunpacked = pg_detoast_datum_packed((struct varlena *) t);
	int			len = VARSIZE_ANY_EXHDR(tunpacked);
	char	   *result;

	result = (char *) palloc(len + 1);
	memcpy(result, VARDATA_ANY(tunpacked), len);
	result[len] = '\0';

	if (tunpacked != t)
		pfree(tunpacked);
	
	return result;
}

text *
cstring_to_text(const char *s)
{
	int			len = strlen(s);
	text	   *result = palloc(len + VARHDRSZ);

	SET_VARSIZE(result, len + VARHDRSZ);
	memcpy(VARDATA(result), s, len);

	return result;
}

void
tuplestore_putvalues(Tuplestorestate *state, TupleDesc tdesc,
					 Datum *values, bool *isnull)
{
	tuplestore_puttuple(state, heap_form_tuple(tdesc, values, isnull));
}

#endif
