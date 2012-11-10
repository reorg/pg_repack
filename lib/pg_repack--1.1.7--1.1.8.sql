/* Create the pg_repack extension from a loose set of objects */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_repack FROM 1.1.7" to load this file. \quit

ALTER EXTENSION pg_repack ADD FUNCTION repack.version();
ALTER EXTENSION pg_repack ADD AGGREGATE repack.array_accum(anyelement);
ALTER EXTENSION pg_repack ADD FUNCTION repack.oid2text(oid);
ALTER EXTENSION pg_repack ADD FUNCTION repack.get_index_columns(oid, text);
ALTER EXTENSION pg_repack ADD FUNCTION repack.get_index_keys(oid, oid);
ALTER EXTENSION pg_repack ADD FUNCTION repack.get_create_index_type(oid, name);
ALTER EXTENSION pg_repack ADD FUNCTION repack.get_create_trigger(relid oid, pkid oid);
ALTER EXTENSION pg_repack ADD FUNCTION repack.get_assign(oid, text);
ALTER EXTENSION pg_repack ADD FUNCTION repack.get_compare_pkey(oid, text);
ALTER EXTENSION pg_repack ADD FUNCTION repack.get_columns_for_create_as(oid);
ALTER EXTENSION pg_repack ADD FUNCTION repack.get_drop_columns(oid, text);
ALTER EXTENSION pg_repack ADD VIEW repack.primary_keys;
ALTER EXTENSION pg_repack ADD VIEW repack.tables;
ALTER EXTENSION pg_repack ADD FUNCTION repack.repack_indexdef(oid, oid);
ALTER EXTENSION pg_repack ADD FUNCTION repack.repack_trigger();
ALTER EXTENSION pg_repack ADD FUNCTION repack.conflicted_triggers(oid);
ALTER EXTENSION pg_repack ADD FUNCTION repack.disable_autovacuum(regclass);
ALTER EXTENSION pg_repack ADD FUNCTION repack.repack_apply(cstring,cstring,cstring,cstring,cstring,integer);
ALTER EXTENSION pg_repack ADD FUNCTION repack.repack_swap(oid);
ALTER EXTENSION pg_repack ADD FUNCTION repack.repack_drop(oid);
