SET client_min_messages = warning;

-- Create a test table with some data
CREATE TABLE tbl_where_clause (
    id int PRIMARY KEY,
    value text,
    active boolean,
    "column with space" text
);

-- Insert some test data
INSERT INTO tbl_where_clause VALUES (1, 'one', true, 'one');
INSERT INTO tbl_where_clause VALUES (2, 'two', true, 'two');
INSERT INTO tbl_where_clause VALUES (3, 'three', false, 'three');
INSERT INTO tbl_where_clause VALUES (4, 'four', false, 'four');
INSERT INTO tbl_where_clause VALUES (5, 'five', true, 'five');

-- Check initial data
SELECT * FROM tbl_where_clause ORDER BY id;

-- Run pg_repack with where clause to only repack active rows
\! pg_repack --dbname=contrib_regression --table=tbl_where_clause --where-clause="active = true"

-- Verify data after repack
SELECT * FROM tbl_where_clause ORDER BY id;

-- Insert more data to verify the where clause worked correctly
INSERT INTO tbl_where_clause VALUES (6, 'six', true, 'six');
INSERT INTO tbl_where_clause VALUES (7, 'seven', false, 'seven');

-- Run pg_repack with a different where clause
\! pg_repack --dbname=contrib_regression --table=tbl_where_clause --where-clause="id > 3"

-- Verify data after second repack
SELECT * FROM tbl_where_clause ORDER BY id;

-- Test with where clause and order-by together
\! pg_repack --dbname=contrib_regression --table=tbl_where_clause --where-clause="active = true" --order-by="value"

-- Verify data after repack with order-by
SELECT * FROM tbl_where_clause ORDER BY id;

-- Test with invalid where clause (should fail gracefully)
\! pg_repack --dbname=contrib_regression --table=tbl_where_clause --where-clause="invalid_column = true"

-- Test 1: Syntax error in where clause
\! pg_repack --dbname=contrib_regression --table=tbl_where_clause --where-clause="id === 3"

-- Test 2: Unclosed quotes in where clause
\! pg_repack --dbname=contrib_regression --table=tbl_where_clause --where-clause="value = 'unclosed"

-- Test 3: Type mismatch error in where clause
\! pg_repack --dbname=contrib_regression --table=tbl_where_clause --where-clause="id = 'not a number'"

-- Test 4: Column with space in name
\! pg_repack --dbname=contrib_regression --table=tbl_where_clause --where-clause="\"column with space\" = 'one'"

-- Clean up
DROP TABLE tbl_where_clause; 