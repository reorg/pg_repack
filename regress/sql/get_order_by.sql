--
-- pg_repack issue #3
--
CREATE TABLE issue3_1 (col1 int NOT NULL, col2 text NOT NULL);
CREATE UNIQUE INDEX issue3_1_idx ON issue3_1 (col1, col2 DESC);
SELECT repack.get_order_by('issue3_1_idx'::regclass::oid, 'issue3_1'::regclass::oid);
\! pg_repack --dbname=contrib_regression --table=issue3_1

CREATE TABLE issue3_2 (col1 int NOT NULL, col2 text NOT NULL);
CREATE UNIQUE INDEX issue3_2_idx ON issue3_2 (col1 DESC, col2 text_pattern_ops);
SELECT repack.get_order_by('issue3_2_idx'::regclass::oid, 'issue3_2'::regclass::oid);
\! pg_repack --dbname=contrib_regression --table=issue3_2

CREATE TABLE issue3_3 (col1 int NOT NULL, col2 text NOT NULL);
CREATE UNIQUE INDEX issue3_3_idx ON issue3_3 (col1 DESC, col2 DESC);
SELECT repack.get_order_by('issue3_3_idx'::regclass::oid, 'issue3_3'::regclass::oid);
\! pg_repack --dbname=contrib_regression --table=issue3_3

CREATE TABLE issue3_4 (col1 int NOT NULL, col2 text NOT NULL);
CREATE UNIQUE INDEX issue3_4_idx ON issue3_4 (col1 NULLS FIRST, col2 text_pattern_ops DESC NULLS LAST);
SELECT repack.get_order_by('issue3_4_idx'::regclass::oid, 'issue3_4'::regclass::oid);
\! pg_repack --dbname=contrib_regression --table=issue3_4

CREATE TABLE issue3_5 (col1 int NOT NULL, col2 text NOT NULL);
CREATE UNIQUE INDEX issue3_5_idx ON issue3_5 (col1 DESC NULLS FIRST, col2 COLLATE "POSIX" DESC);
SELECT repack.get_order_by('issue3_5_idx'::regclass::oid, 'issue3_5'::regclass::oid);
\! pg_repack --dbname=contrib_regression --table=issue3_5

--
-- pg_repack issue #321
--
CREATE TABLE issue321 (col1 int NOT NULL, col2 text NOT NULL);
CREATE UNIQUE INDEX issue321_idx ON issue321 (col1);
SELECT repack.get_order_by('issue321_idx'::regclass::oid, 1);
SELECT repack.get_order_by(1, 1);

--
-- A non-default operator class in a schema that is not in the function search_path
--
CREATE OPERATOR CLASS public.opclass_int4_ops FOR TYPE int4 USING btree AS
  OPERATOR 1 <, OPERATOR 2 <=, OPERATOR 3 =, OPERATOR 4 >=, OPERATOR 5 >,
  FUNCTION 1 btint4cmp(int4, int4);
CREATE TABLE opclass_tbl (col1 int NOT NULL, col2 int NOT NULL);
CREATE UNIQUE INDEX opclass_tbl_idx ON opclass_tbl (col1 public.opclass_int4_ops, col2 DESC);
SELECT repack.get_order_by('opclass_tbl_idx'::regclass::oid, 'opclass_tbl'::regclass::oid);
CLUSTER opclass_tbl USING opclass_tbl_idx;
\! pg_repack --dbname=contrib_regression --table=opclass_tbl

--
-- DESC with a non-default operator class must not produce "DESC USING"
--
CREATE TABLE desc_opclass_tbl (col1 int NOT NULL, col2 text NOT NULL);
CREATE UNIQUE INDEX desc_opclass_tbl_idx ON desc_opclass_tbl (col1, col2 text_pattern_ops DESC NULLS LAST);
INSERT INTO desc_opclass_tbl VALUES (2, 'a'), (1, 'b'), (1, 'c');
CLUSTER desc_opclass_tbl USING desc_opclass_tbl_idx;
SELECT repack.get_order_by('desc_opclass_tbl_idx'::regclass::oid, 'desc_opclass_tbl'::regclass::oid);
\! pg_repack --dbname=contrib_regression --table=desc_opclass_tbl
SELECT col1, col2 FROM desc_opclass_tbl ORDER BY ctid;

-- Quoted schema name in generated SQL
CREATE SCHEMA "q""s";
CREATE TABLE "q""s".qt (id int PRIMARY KEY, v text) WITH (fillfactor = 70, toast.autovacuum_enabled = false);
CREATE INDEX qt_idx ON "q""s".qt (v);
INSERT INTO "q""s".qt SELECT i, 'v' || i FROM generate_series(1, 5) i;
\! pg_repack --dbname=contrib_regression --table='"q""s".qt'
\! pg_repack --dbname=contrib_regression --only-indexes --table='"q""s".qt'
SELECT (SELECT count(*) FROM pg_class WHERE relnamespace = '"q""s"'::regnamespace AND relname LIKE 'index\_%') AS leftover,
       c.reloptions, t.reloptions AS toast_reloptions, (SELECT count(*) FROM "q""s".qt) AS n
FROM pg_class c JOIN pg_class t ON t.oid = c.reltoastrelid WHERE c.oid = '"q""s".qt'::regclass;
DROP SCHEMA "q""s" CASCADE;
