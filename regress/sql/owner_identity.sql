--
-- When a superuser runs pg_repack, code of the table owner (index expressions)
-- must run as the owner, not as the superuser.
--
\set VERBOSITY terse

CREATE ROLE repack_owner;
GRANT CREATE ON SCHEMA public TO repack_owner;

-- audit() records who calls it; owner_f() is used in indexes of the table
CREATE TABLE who_ran (who name, su boolean);
GRANT INSERT ON who_ran TO PUBLIC;

CREATE FUNCTION audit() RETURNS void LANGUAGE sql VOLATILE AS
$$ INSERT INTO public.who_ran SELECT current_user, rolsuper FROM pg_roles WHERE rolname = current_user $$;
GRANT EXECUTE ON FUNCTION audit() TO PUBLIC;

SET ROLE repack_owner;
CREATE TABLE owner_t (id int PRIMARY KEY, v int);

CREATE FUNCTION owner_f(int) RETURNS int LANGUAGE plpgsql IMMUTABLE AS
$$ BEGIN PERFORM public.audit(); RETURN $1; END $$;

INSERT INTO owner_t SELECT i, i FROM generate_series(1, 5) i;
CREATE INDEX ON owner_t (owner_f(v));

RESET ROLE;
TRUNCATE who_ran;

-- Index builds and the copy: only the owner ran owner_f()
\! pg_repack --dbname=contrib_regression --table=owner_t
SELECT who, su FROM who_ran GROUP BY who, su;

--
-- The same steps by hand, to cover repack_apply, copy_data and create_table
--
SELECT 'owner_t'::regclass::oid AS oid, indexrelid AS pkid
  FROM pg_index WHERE indrelid = 'owner_t'::regclass AND indisprimary
\gset
SELECT format('INSERT INTO repack.table_%s SELECT * FROM ONLY public.owner_t', :oid) AS ins
\gset
SELECT repack.create_index_type(:pkid, :oid);
SELECT repack.create_log_table(:oid);

-- create_table: the new table belongs to the owner
SELECT repack.create_table(:oid, 'pg_default');
SELECT relowner::regrole FROM pg_class WHERE oid = ('repack.table_' || :oid)::regclass;

-- copy_data: runs a single INSERT into the new table
SELECT repack.copy_data(:oid, 'SELECT 1');
SELECT repack.copy_data(:oid, :'ins' || '; DELETE FROM public.owner_t');
SELECT repack.copy_data(:oid, 'INSERT INTO public.owner_t SELECT * FROM public.owner_t');
SELECT repack.copy_data(:oid, 'WITH d AS (DELETE FROM public.owner_t RETURNING *) ' || :'ins');
SELECT repack.copy_data(:oid, :'ins');

-- repack_apply
CREATE INDEX ON repack.table_:oid (public.owner_f(v));
SELECT sql_peek, sql_insert, sql_delete, sql_update, sql_pop
  FROM repack.tables WHERE relname = 'public.owner_t'
\gset
INSERT INTO repack.log_:oid (pk, row) VALUES
  (NULL, ROW(6, 6)::public.owner_t),
  (ROW(1)::repack.pk_:oid, ROW(1, 7)::public.owner_t),
  (ROW(2)::repack.pk_:oid, NULL);
TRUNCATE who_ran;

SELECT repack.repack_apply(:'sql_peek', :'sql_insert', :'sql_delete', :'sql_update', :'sql_pop', 100, :oid);
SELECT who, su FROM who_ran GROUP BY who, su;
SELECT * FROM repack.table_:oid ORDER BY id;

-- Only the owner (or a superuser) may use these functions
CREATE ROLE repack_other;
GRANT USAGE ON SCHEMA repack TO repack_other;

SET ROLE repack_other;
SELECT repack.copy_data(:oid, :'ins');
SELECT repack.repack_apply(:'sql_peek', :'sql_insert', :'sql_delete', :'sql_update', :'sql_pop', 100, :oid);
RESET ROLE;

-- Cleanup
REVOKE USAGE ON SCHEMA repack FROM repack_other;
DROP ROLE repack_other;
DROP TABLE repack.log_:oid, repack.table_:oid;
DROP TYPE repack.pk_:oid;
DROP TABLE owner_t, who_ran;
DROP FUNCTION owner_f(int);
DROP FUNCTION audit();
REVOKE CREATE ON SCHEMA public FROM repack_owner;
DROP ROLE repack_owner;
