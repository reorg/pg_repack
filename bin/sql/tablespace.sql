SET client_min_messages = warning;

--
-- Tablespace features tests
--
-- Note: in order to pass this test you must create a tablespace called 'testts'
--

SELECT spcname FROM pg_tablespace WHERE spcname = 'testts';
-- If the query above failed you must create the 'testts' tablespace;

CREATE TABLE testts1 (id serial primary key, data text);
INSERT INTO testts1 (data) values ('a');
INSERT INTO testts1 (data) values ('b');
INSERT INTO testts1 (data) values ('c');

-- can move the tablespace from default
\! pg_repack --dbname=contrib_regression --no-order --table=testts1 --tablespace testts

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1';

SELECT * from testts1 order by id;

-- tablespace stays where it is
\! pg_repack --dbname=contrib_regression --no-order --table=testts1

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1';

-- can move the ts back to default
\! pg_repack --dbname=contrib_regression --no-order --table=testts1 -s pg_default

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1';

-- can move the table together with the indexes
\! pg_repack --dbname=contrib_regression --no-order --table=testts1 --tablespace pg_default --moveidx

SELECT relname, spcname
FROM pg_class JOIN pg_tablespace ts ON ts.oid = reltablespace
WHERE relname ~ '^testts1';

-- can't specify --moveidx without --tablespace
\! pg_repack --dbname=contrib_regression --no-order --table=testts1 --moveidx
\! pg_repack --dbname=contrib_regression --no-order --table=testts1 -S

