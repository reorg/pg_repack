--
-- pg_repack must exit with an error when a requested index does not exist
-- or is not an index (issue #468)
--
SET client_min_messages = warning;

CREATE TABLE tbl_missing_idx (id integer PRIMARY KEY, t text);
CREATE INDEX tbl_missing_idx_t ON tbl_missing_idx (t);

-- => ERROR: a non-existent index
\! pg_repack --dbname=contrib_regression --index=idx_does_not_exist || echo "pg_repack exited with an error"
-- => ERROR: a valid index together with a non-existent one
\! pg_repack --dbname=contrib_regression --index=tbl_missing_idx_t --index=idx_does_not_exist || echo "pg_repack exited with an error"
-- => ERROR: a table given instead of an index
\! pg_repack --dbname=contrib_regression --index=tbl_missing_idx || echo "pg_repack exited with an error"
-- => OK: an existing index
\! pg_repack --dbname=contrib_regression --index=tbl_missing_idx_t && echo "pg_repack succeeded"

DROP TABLE tbl_missing_idx;
