/* pg_reorg/pg_reorg--unpackaged--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_reorg" to load this file. \quit

ALTER EXTENSION pg_reorg ADD FUNCTION reorg.version();
ALTER EXTENSION pg_reorg ADD AGGREGATE reorg.array_accum (array_append, anyelement, anyarray,'{}');
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.oid2text(oid);
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.get_index_columns(oid, text);
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.get_index_keys(oid, oid);
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.get_create_index_type(oid, name);
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.get_create_trigger(relid oid, pkid oid);
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.get_assign(oid, text);
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.get_compare_pkey(oid, text);
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.get_columns_for_create_as(oid);
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.get_drop_columns(oid, text);
ALTER EXTENSION pg_reorg ADD VIEW reorg.primary_keys;
ALTER EXTENSION pg_reorg ADD VIEW reorg.tables;
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.reorg_indexdef(oid, oid);
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.reorg_trigger();
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.conflicted_triggers(oid);
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.disable_autovacuum(regclass);
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.reorg_apply((cstring,cstring,cstring,cstring,cstring,integer);
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.reorg_swap(oid);
ALTER EXTENSION pg_reorg ADD FUNCTION reorg.reorg_drop(oid);
