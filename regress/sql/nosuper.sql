--
-- no superuser check
--
SET client_min_messages = error;
DROP ROLE IF EXISTS nosuper;
SET client_min_messages = warning;
CREATE ROLE nosuper WITH LOGIN;
-- => OK
\! pg_repack --dbname=contrib_regression --table=tbl_cluster --no-superuser-check
-- => ERROR
\! pg_repack --dbname=contrib_regression --table=tbl_cluster --username=nosuper
-- => ERROR
\! pg_repack --dbname=contrib_regression --table=tbl_cluster --username=nosuper --no-superuser-check

CREATE SCHEMA nosuper;
GRANT ALL ON SCHEMA nosuper TO nosuper;
SET SESSION AUTHORIZATION nosuper;
CREATE TABLE nosuper.nosuper_test (id SERIAL PRIMARY KEY, data TEXT);
INSERT INTO nosuper.nosuper_test (data) VALUES ('row1'), ('row2');
RESET SESSION AUTHORIZATION;

-- => OK
\! pg_repack --dbname=contrib_regression --table=nosuper.nosuper_test --username=nosuper --no-superuser-check

DROP TABLE IF EXISTS nosuper.nosuper_test;
DROP SCHEMA IF EXISTS nosuper;
DROP ROLE IF EXISTS nosuper;
