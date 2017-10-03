-- Test output file identifier.
SELECT CASE
	WHEN split_part(version(), ' ', 2) ~ '^(10)'
		THEN 'repack_2.out'
	WHEN split_part(version(), ' ', 2) ~ '^(9\.6|9\.5)'
		THEN 'repack.out'
	WHEN split_part(version(), ' ', 2) ~ '^(9\.4|9\.3|9\.2|9\.1)'
		THEN 'repack_1.out'
	ELSE version()
END AS testfile;

SET client_min_messages = warning;
--
-- create table.
--
CREATE TABLE tbl_cluster (
	col1 int,
	"time" timestamp,
	","")" text,
	PRIMARY KEY (","")", col1) WITH (fillfactor = 75)
) WITH (fillfactor = 70);

CREATE INDEX ","") cluster" ON tbl_cluster ("time", length(","")"), ","")" text_pattern_ops) WITH (fillfactor = 75);
ALTER TABLE tbl_cluster CLUSTER ON ","") cluster";

CREATE TABLE tbl_only_pkey (
	col1 int PRIMARY KEY,
	","")" text
);

CREATE TABLE tbl_only_ckey (
	col1 int,
	col2 timestamp,
	","")" text
) WITH (fillfactor = 70);

CREATE INDEX cidx_only_ckey ON tbl_only_ckey (col2, ","")");
ALTER TABLE tbl_only_ckey CLUSTER ON cidx_only_ckey;

CREATE TABLE tbl_gistkey (
	id integer PRIMARY KEY,
	c circle
);

CREATE INDEX cidx_circle ON tbl_gistkey USING gist (c);
ALTER TABLE tbl_gistkey CLUSTER ON cidx_circle;

CREATE TABLE tbl_with_dropped_column (
	d1 text,
	c1 text,
	id integer PRIMARY KEY,
	d2 text,
	c2 text,
	d3 text
);
ALTER INDEX tbl_with_dropped_column_pkey SET (fillfactor = 75);
ALTER TABLE tbl_with_dropped_column CLUSTER ON tbl_with_dropped_column_pkey;
CREATE INDEX idx_c1c2 ON tbl_with_dropped_column (c1, c2) WITH (fillfactor = 75);
CREATE INDEX idx_c2c1 ON tbl_with_dropped_column (c2, c1);

CREATE TABLE tbl_with_dropped_toast (
	i integer,
	j integer,
	t text,
	PRIMARY KEY (i, j)
);
ALTER TABLE tbl_with_dropped_toast CLUSTER ON tbl_with_dropped_toast_pkey;

CREATE TABLE tbl_badindex (
	id integer PRIMARY KEY,
	n integer
);

CREATE TABLE tbl_idxopts (
       i integer PRIMARY KEY,
       t text
);
CREATE INDEX idxopts_t ON tbl_idxopts (t DESC NULLS LAST) WHERE (t != 'aaa');

-- Use this table to play with attribute options too
ALTER TABLE tbl_idxopts ALTER i SET STATISTICS 1;
ALTER TABLE tbl_idxopts ALTER t SET (n_distinct = -0.5);
CREATE TABLE tbl_with_toast (
      i integer PRIMARY KEY,
      c text
);
ALTER TABLE tbl_with_toast SET (AUTOVACUUM_VACUUM_SCALE_FACTOR = 30, AUTOVACUUM_VACUUM_THRESHOLD = 300);
ALTER TABLE tbl_with_toast SET (TOAST.AUTOVACUUM_VACUUM_SCALE_FACTOR = 40, TOAST.AUTOVACUUM_VACUUM_THRESHOLD = 400);
CREATE TABLE tbl_with_mod_column_storage (
	id integer PRIMARY KEY,
	c text
);
ALTER TABLE tbl_with_mod_column_storage ALTER c SET STORAGE MAIN;

CREATE TABLE tbl_order (c int primary key);
--
-- insert data
--

INSERT INTO tbl_cluster VALUES(1, '2008-12-31 10:00:00', 'admin');
INSERT INTO tbl_cluster VALUES(2, '2008-01-01 00:00:00', 'king');
INSERT INTO tbl_cluster VALUES(3, '2008-03-04 12:00:00', 'joker');
INSERT INTO tbl_cluster VALUES(4, '2008-03-05 15:00:00', 'queen');
INSERT INTO tbl_cluster VALUES(5, '2008-01-01 00:30:00', sqrt(2::numeric(1000,999))::text || sqrt(3::numeric(1000,999))::text);

INSERT INTO tbl_only_pkey VALUES(1, 'abc');
INSERT INTO tbl_only_pkey VALUES(2, 'def');

INSERT INTO tbl_only_ckey VALUES(1, '2008-01-01 00:00:00', 'abc');
INSERT INTO tbl_only_ckey VALUES(2, '2008-02-01 00:00:00', 'def');

INSERT INTO tbl_gistkey VALUES(1, '<(1,2),3>');
INSERT INTO tbl_gistkey VALUES(2, '<(4,5),6>');

INSERT INTO tbl_with_dropped_column VALUES('d1', 'c1', 2, 'd2', 'c2', 'd3');
INSERT INTO tbl_with_dropped_column VALUES('d1', 'c1', 1, 'd2', 'c2', 'd3');
ALTER TABLE tbl_with_dropped_column DROP COLUMN d1;
ALTER TABLE tbl_with_dropped_column DROP COLUMN d2;
ALTER TABLE tbl_with_dropped_column DROP COLUMN d3;
ALTER TABLE tbl_with_dropped_column ADD COLUMN c3 text;
CREATE VIEW view_for_dropped_column AS
	SELECT * FROM tbl_with_dropped_column;

INSERT INTO tbl_with_dropped_toast VALUES(1, 10, 'abc');
INSERT INTO tbl_with_dropped_toast VALUES(2, 20, sqrt(2::numeric(1000,999))::text || sqrt(3::numeric(1000,999))::text);
ALTER TABLE tbl_with_dropped_toast DROP COLUMN t;

INSERT INTO tbl_badindex VALUES(1, 10);
INSERT INTO tbl_badindex VALUES(2, 10);

-- insert data that is always stored into the toast table if column type is extended.
SELECT setseed(0); INSERT INTO tbl_with_mod_column_storage SELECT 1, array_to_string(ARRAY(SELECT chr((random() * (127 - 32) + 32)::int) FROM generate_series(1, 3 * 1024) code), '');

-- This will fail. Silence the message as it's different across PG versions.
SET client_min_messages = fatal;
CREATE UNIQUE INDEX CONCURRENTLY idx_badindex_n ON tbl_badindex (n);
SET client_min_messages = warning;

INSERT INTO tbl_idxopts VALUES (0, 'abc'), (1, 'aaa'), (2, NULL), (3, 'bbb');

-- Insert no-ordered data
INSERT INTO tbl_order SELECT generate_series(100, 51, -1);
CLUSTER tbl_order USING tbl_order_pkey;
INSERT INTO tbl_order SELECT generate_series(50, 1, -1);
--
-- before
--

SELECT * FROM tbl_with_dropped_column;
SELECT * FROM view_for_dropped_column;
SELECT * FROM tbl_with_dropped_toast;

--
-- do repack
--

\! pg_repack --dbname=contrib_regression --table=tbl_cluster
\! pg_repack --dbname=contrib_regression --table=tbl_badindex
\! pg_repack --dbname=contrib_regression

--
-- after
--

\d tbl_cluster
\d tbl_gistkey
\d tbl_only_ckey
\d tbl_only_pkey
\d tbl_with_dropped_column
\d tbl_with_dropped_toast
\d tbl_idxopts

SELECT col1, to_char("time", 'YYYY-MM-DD HH24:MI:SS'), ","")" FROM tbl_cluster ORDER BY 1, 2;
SELECT * FROM tbl_only_ckey ORDER BY 1;
SELECT * FROM tbl_only_pkey ORDER BY 1;
SELECT * FROM tbl_gistkey ORDER BY 1;

SET enable_seqscan = on;
SET enable_indexscan = off;
SELECT * FROM tbl_with_dropped_column ;
SELECT * FROM view_for_dropped_column ORDER BY 1, 2;
SELECT * FROM tbl_with_dropped_toast;
SET enable_seqscan = off;
SET enable_indexscan = on;
SELECT * FROM tbl_with_dropped_column ORDER BY 1, 2;
SELECT * FROM view_for_dropped_column;
SELECT * FROM tbl_with_dropped_toast;
RESET enable_seqscan;
RESET enable_indexscan;
-- check if storage option for both table and TOAST table didn't go away.
SELECT CASE relkind
       WHEN 'r' THEN relname
       WHEN 't' THEN 'toast_table'
       END as table,
       reloptions
FROM pg_class
WHERE relname = 'tbl_with_toast' OR relname = 'pg_toast_' || 'tbl_with_toast'::regclass::oid
ORDER BY 1;
SELECT pg_relation_size(reltoastrelid) = 0 as check_toast_rel_size FROM pg_class WHERE relname = 'tbl_with_mod_column_storage';

--
-- check broken links or orphan toast relations
--
SELECT oid, relname
  FROM pg_class
 WHERE relkind = 't'
   AND oid NOT IN (SELECT reltoastrelid FROM pg_class WHERE relkind = 'r');

SELECT oid, relname
  FROM pg_class
 WHERE relkind = 'r'
   AND reltoastrelid <> 0
   AND reltoastrelid NOT IN (SELECT oid FROM pg_class WHERE relkind = 't');

-- check columns options
SELECT attname, attstattarget, attoptions
FROM pg_attribute
WHERE attrelid = 'tbl_idxopts'::regclass
AND attnum > 0
ORDER BY attnum;

--
-- NOT NULL UNIQUE
--
CREATE TABLE tbl_nn    (col1 int NOT NULL, col2 int NOT NULL);
CREATE TABLE tbl_uk    (col1 int NOT NULL, col2 int         , UNIQUE(col1, col2));
CREATE TABLE tbl_nn_uk (col1 int NOT NULL, col2 int NOT NULL, UNIQUE(col1, col2));
CREATE TABLE tbl_pk_uk (col1 int NOT NULL, col2 int NOT NULL, PRIMARY KEY(col1, col2), UNIQUE(col2, col1));
CREATE TABLE tbl_nn_puk (col1 int NOT NULL, col2 int NOT NULL);
CREATE UNIQUE INDEX tbl_nn_puk_pcol1_idx ON tbl_nn_puk(col1) WHERE col1 < 10;
\! pg_repack --dbname=contrib_regression --table=tbl_nn
-- => WARNING
\! pg_repack --dbname=contrib_regression --table=tbl_uk
-- => WARNING
\! pg_repack --dbname=contrib_regression --table=tbl_nn_uk
-- => OK
\! pg_repack --dbname=contrib_regression --table=tbl_pk_uk
-- => OK
\! pg_repack --dbname=contrib_regression --table=tbl_pk_uk --only-indexes
-- => OK
\! pg_repack --dbname=contrib_regression --table=tbl_nn_puk
-- => WARNING

--
-- Triggers handling
--
CREATE FUNCTION trgtest() RETURNS trigger AS
$$BEGIN RETURN NEW; END$$
LANGUAGE plpgsql;
CREATE TABLE trg1 (id integer PRIMARY KEY);
CREATE TRIGGER repack_trigger_1 AFTER UPDATE ON trg1 FOR EACH ROW EXECUTE PROCEDURE trgtest();
\! pg_repack --dbname=contrib_regression --table=trg1
CREATE TABLE trg2 (id integer PRIMARY KEY);
CREATE TRIGGER repack_trigger AFTER UPDATE ON trg2 FOR EACH ROW EXECUTE PROCEDURE trgtest();
\! pg_repack --dbname=contrib_regression --table=trg2
CREATE TABLE trg3 (id integer PRIMARY KEY);
CREATE TRIGGER repack_trigger_1 BEFORE UPDATE ON trg3 FOR EACH ROW EXECUTE PROCEDURE trgtest();
\! pg_repack --dbname=contrib_regression --table=trg3

--
-- Table re-organization using specific column
--

-- reorganize table using cluster key. Sort in ascending order.
\! pg_repack --dbname=contrib_regression --table=tbl_order
SELECT ctid, c FROM tbl_order WHERE ctid <= '(0,10)';

-- reorganize table using specific column order. Sort in descending order.
\! pg_repack --dbname=contrib_regression --table=tbl_order -o "c DESC"
SELECT ctid, c FROM tbl_order WHERE ctid <= '(0,10)';


--
-- Dry run
--
\! pg_repack --dbname=contrib_regression --table=tbl_cluster --dry-run

-- Test --schema
--
CREATE SCHEMA test_schema1;
CREATE TABLE test_schema1.tbl1 (id INTEGER PRIMARY KEY);
CREATE TABLE test_schema1.tbl2 (id INTEGER PRIMARY KEY);
CREATE SCHEMA test_schema2;
CREATE TABLE test_schema2.tbl1 (id INTEGER PRIMARY KEY);
CREATE TABLE test_schema2.tbl2 (id INTEGER PRIMARY KEY);
-- => OK
\! pg_repack --dbname=contrib_regression --schema=test_schema1
-- => OK
\! pg_repack --dbname=contrib_regression --schema=test_schema1 --schema=test_schema2
-- => ERROR
\! pg_repack --dbname=contrib_regression --schema=test_schema1 --table=tbl1
-- => ERROR
\! pg_repack --dbname=contrib_regression --all --schema=test_schema1

--
-- don't kill backend
--
\! pg_repack --dbname=contrib_regression --table=tbl_cluster --no-kill-backend

--
-- no superuser check
--
DROP ROLE IF EXISTS nosuper;
CREATE ROLE nosuper WITH LOGIN;
-- => OK
\! pg_repack --dbname=contrib_regression --table=tbl_cluster --no-superuser-check
-- => ERROR
\! pg_repack --dbname=contrib_regression --table=tbl_cluster --username=nosuper
-- => ERROR
\! pg_repack --dbname=contrib_regression --table=tbl_cluster --username=nosuper --no-superuser-check
DROP ROLE IF EXISTS nosuper;

--
-- exclude extension check
--
CREATE SCHEMA exclude_extension_schema;
CREATE TABLE exclude_extension_schema.tbl(val integer primary key);
-- => ERROR
\! pg_repack --dbname=contrib_regression --table=dummy_table --exclude-extension=dummy_extension
-- => ERROR
\! pg_repack --dbname=contrib_regression --table=dummy_table --exclude-extension=dummy_extension -x
-- => ERROR
\! pg_repack --dbname=contrib_regression --index=dummy_index --exclude-extension=dummy_extension
-- => OK
\! pg_repack --dbname=contrib_regression --schema=exclude_extension_schema --exclude-extension=dummy_extension
-- => OK
\! pg_repack --dbname=contrib_regression --schema=exclude_extension_schema --exclude-extension=dummy_extension --exclude-extension=dummy_extension

--
-- table inheritance check
--
CREATE TABLE parent_a(val integer primary key);
CREATE TABLE child_a_1(val integer primary key) INHERITS(parent_a);
CREATE TABLE child_a_2(val integer primary key) INHERITS(parent_a);
CREATE TABLE parent_b(val integer primary key);
CREATE TABLE child_b_1(val integer primary key) INHERITS(parent_b);
CREATE TABLE child_b_2(val integer primary key) INHERITS(parent_b);
-- => ERROR
\! pg_repack --dbname=contrib_regression --parent-table=dummy_table
-- => ERROR
\! pg_repack --dbname=contrib_regression --parent-table=dummy_index --index=dummy_index
-- => ERROR
\! pg_repack --dbname=contrib_regression --parent-table=dummy_table --schema=dummy_schema
-- => ERROR
\! pg_repack --dbname=contrib_regression --parent-table=dummy_table --all
-- => OK
\! pg_repack --dbname=contrib_regression --table=parent_a --parent-table=parent_b
-- => OK
\! pg_repack --dbname=contrib_regression --parent-table=parent_a --parent-table=parent_b
-- => OK
\! pg_repack --dbname=contrib_regression --table=parent_a --parent-table=parent_b --only-indexes
-- => OK
\! pg_repack --dbname=contrib_regression --parent-table=parent_a --parent-table=parent_b --only-indexes
