SET client_min_messages = warning;

--
-- Tablespace features tests
--
-- Note: in order to pass this test you must create a tablespace called 'testts'
--

SELECT spcname FROM pg_tablespace WHERE spcname = 'testts';
-- If the query above failed you must create the 'testts' tablespace;

CREATE TABLE testts1 (id serial primary key, data text);
CREATE INDEX testts1_partial_idx on testts1 (id) where (id > 0);
CREATE INDEX testts1_with_idx on testts1 (id) with (fillfactor=80);
INSERT INTO testts1 (data) values ('a');
INSERT INTO testts1 (data) values ('b');
INSERT INTO testts1 (data) values ('c');

-- check the indexes definitions
SELECT regexp_replace(
    repack.repack_indexdef(indexrelid, 'testts1'::regclass, NULL, false),
    '_[0-9]+', '_OID', 'g')
FROM pg_index i join pg_class c ON c.oid = indexrelid
WHERE indrelid = 'testts1'::regclass ORDER BY relname;

SELECT regexp_replace(
    repack.repack_indexdef(indexrelid, 'testts1'::regclass, 'foo', false),
    '_[0-9]+', '_OID', 'g')
FROM pg_index i join pg_class c ON c.oid = indexrelid
WHERE indrelid = 'testts1'::regclass ORDER BY relname;

SELECT regexp_replace(
    repack.repack_indexdef(indexrelid, 'testts1'::regclass, NULL, true),
    '_[0-9]+', '_OID', 'g')
FROM pg_index i join pg_class c ON c.oid = indexrelid
WHERE indrelid = 'testts1'::regclass ORDER BY relname;

SELECT regexp_replace(
    repack.repack_indexdef(indexrelid, 'testts1'::regclass, 'foo', true),
    '_[0-9]+', '_OID', 'g')
FROM pg_index i join pg_class c ON c.oid = indexrelid
WHERE indrelid = 'testts1'::regclass ORDER BY relname;

-- can move the tablespace from default
\! pg_repack --dbname=contrib_regression --no-order --table=testts1 --tablespace testts

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1'
ORDER BY relname;

SELECT * from testts1 order by id;

-- tablespace stays where it is
\! pg_repack --dbname=contrib_regression --no-order --table=testts1

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1'
ORDER BY relname;

-- can move the ts back to default
\! pg_repack --dbname=contrib_regression --no-order --table=testts1 -s pg_default

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1'
ORDER BY relname;

-- can move the table together with the indexes
\! pg_repack --dbname=contrib_regression --no-order --table=testts1 --tablespace testts --moveidx

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1'
ORDER BY relname;

-- can't specify --moveidx without --tablespace
\! pg_repack --dbname=contrib_regression --no-order --table=testts1 --moveidx
\! pg_repack --dbname=contrib_regression --no-order --table=testts1 -S

-- not broken with order
\! pg_repack --dbname=contrib_regression -o id --table=testts1 --tablespace pg_default --moveidx

--move all indexes of the table to a tablespace
\! pg_repack --dbname=contrib_regression --table=testts1 --only-indexes --tablespace=testts

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1'
ORDER BY relname;

--all indexes of tablespace remain in same tablespace
\! pg_repack --dbname=contrib_regression --table=testts1 --only-indexes

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1'
ORDER BY relname;

--move all indexes of the table to pg_default
\! pg_repack --dbname=contrib_regression --table=testts1 --only-indexes --tablespace=pg_default

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1'
ORDER BY relname;

--move one index to a tablespace
\! pg_repack --dbname=contrib_regression --index=testts1_pkey --tablespace=testts

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1'
ORDER BY relname;

--index tablespace stays as is
\! pg_repack --dbname=contrib_regression --index=testts1_pkey

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1'
ORDER BY relname;

--move index to pg_default
\! pg_repack --dbname=contrib_regression --index=testts1_pkey --tablespace=pg_default

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1'
ORDER BY relname;

--using multiple --index option
\! pg_repack --dbname=contrib_regression --index=testts1_pkey --index=testts1_with_idx --tablespace=testts

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1'
ORDER BY relname;

--using --indexes-only and --index option together
\! pg_repack --dbname=contrib_regression --table=testts1 --only-indexes --index=testts1_pkey
