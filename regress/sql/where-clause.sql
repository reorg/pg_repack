SET client_min_messages = warning;

-- Create a test table with some data
CREATE TABLE tbl_where_clause (
    id int PRIMARY KEY,
    value text,
    active boolean,
    deleted_at timestamp
);

-- Insert some test data
INSERT INTO tbl_where_clause VALUES (1, 'one', true, NULL);
INSERT INTO tbl_where_clause VALUES (2, 'two', true, NULL);
INSERT INTO tbl_where_clause VALUES (3, 'three', false, NULL);
INSERT INTO tbl_where_clause VALUES (4, 'four', false, NULL);
INSERT INTO tbl_where_clause VALUES (5, 'five', true, NULL);

-- Check initial data
SELECT * FROM tbl_where_clause ORDER BY id;

-- Run pg_repack with where clause to only repack active rows
\! pg_repack --dbname=contrib_regression --table=tbl_where_clause --where-clause="active = true"

-- Verify data after repack
SELECT * FROM tbl_where_clause ORDER BY id;

-- Insert more data to verify the where clause worked correctly
INSERT INTO tbl_where_clause VALUES (6, 'six', true, NULL);
INSERT INTO tbl_where_clause VALUES (7, 'seven', false, NULL);

-- Run pg_repack with a different where clause
\! pg_repack --dbname=contrib_regression --table=tbl_where_clause --where-clause="id > 3"

-- Verify data after second repack
SELECT * FROM tbl_where_clause ORDER BY id;

-- Test with where clause and order-by together
\! pg_repack --dbname=contrib_regression --table=tbl_where_clause --where-clause="active = true" --order-by="value"

-- Verify data after repack with order-by
SELECT * FROM tbl_where_clause ORDER BY id;

INSERT INTO tbl_where_clause VALUES (8, 'eight', true, NULL);
INSERT INTO tbl_where_clause VALUES (9, 'nine', false, '2023-01-04 10:00:00');

-- Test with deleted_at is null where clause (keep only non-deleted rows)
\! pg_repack --dbname=contrib_regression --table=tbl_where_clause --where-clause="deleted_at IS NULL"

-- Verify data after repack with deleted_at IS NULL
SELECT * FROM tbl_where_clause ORDER BY id;

-- Test with non-existent column in where clause (should fail)
\! pg_repack --dbname=contrib_regression --table=tbl_where_clause --where-clause="non_existent_column = true"

-- Test with special character in column name without proper quoting (should fail)
\! pg_repack --dbname=contrib_regression --table=tbl_where_clause --where-clause="column with space = 'test'"

-- Test for repackaing whole database with a where clause (should fail)
\! pg_repack --dbname=contrib_regression --where-clause="id > 3"

-- Clean up
DROP TABLE tbl_where_clause; 