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

GRANT ALL ON ALL TABLES IN SCHEMA repack TO nosuper;
GRANT USAGE ON SCHEMA repack TO nosuper;

-- => ERROR
\! pg_repack --dbname=contrib_regression --table=tbl_cluster --username=nosuper --no-superuser-check

REVOKE ALL ON ALL TABLES IN SCHEMA repack FROM nosuper;
REVOKE USAGE ON SCHEMA repack FROM nosuper;
DROP ROLE IF EXISTS nosuper;

--
-- A non-superuser must not lose rows hidden by row-level security
--
CREATE ROLE rls_owner WITH LOGIN;
GRANT USAGE, CREATE ON SCHEMA repack TO rls_owner;
GRANT ALL ON ALL TABLES IN SCHEMA repack TO rls_owner;

CREATE TABLE tbl_rls (id int PRIMARY KEY);
INSERT INTO tbl_rls SELECT generate_series(1, 6);

ALTER TABLE tbl_rls OWNER TO rls_owner;
ALTER TABLE tbl_rls ENABLE ROW LEVEL SECURITY;
ALTER TABLE tbl_rls FORCE ROW LEVEL SECURITY;

CREATE POLICY p ON tbl_rls USING (id <= 3);

-- => ERROR (query would be affected by row-level security policy)
\! pg_repack --dbname=contrib_regression --table=tbl_rls --username=rls_owner --no-superuser-check 2>&1 | grep -v 'DETAIL\|CONTEXT'

SELECT count(*) FROM tbl_rls;  -- all 6 rows are still there

DROP TABLE tbl_rls;
REVOKE ALL ON ALL TABLES IN SCHEMA repack FROM rls_owner;
REVOKE ALL ON SCHEMA repack FROM rls_owner;
DROP ROLE rls_owner;

--
-- A non-superuser must own the table (or be a member of its owner role);
-- otherwise pg_repack fails before it creates any temporary object
--
CREATE ROLE tbl_owner;
CREATE ROLE tbl_other WITH LOGIN;
CREATE ROLE tbl_member WITH LOGIN;

GRANT tbl_owner TO tbl_member;
GRANT USAGE, CREATE ON SCHEMA repack TO tbl_other, tbl_member;
GRANT ALL ON ALL TABLES IN SCHEMA repack TO tbl_other, tbl_member;

CREATE TABLE tbl_owned (id int PRIMARY KEY);
INSERT INTO tbl_owned SELECT generate_series(1, 6);
ALTER TABLE tbl_owned OWNER TO tbl_owner;

-- enough privileges to get past the table lock and CREATE TRIGGER, but not the owner
GRANT SELECT, TRIGGER, UPDATE, DELETE, TRUNCATE ON tbl_owned TO tbl_other;

-- => ERROR (before any temporary object is created)
\! pg_repack --dbname=contrib_regression --table=tbl_owned --username=tbl_other --no-superuser-check 2>&1 | grep -v DETAIL

SELECT count(*) AS leftovers FROM pg_class WHERE relnamespace = 'repack'::regnamespace AND relname ~ '^(log|table)_[0-9]+$';
SELECT count(*) AS leftover_triggers FROM pg_trigger WHERE tgrelid = 'tbl_owned'::regclass AND NOT tgisinternal;

-- => OK (a member of the owner role)
\! pg_repack --dbname=contrib_regression --table=tbl_owned --username=tbl_member --no-superuser-check

SELECT count(*) FROM tbl_owned;
SELECT relowner::regrole AS table_owner FROM pg_class WHERE oid = 'tbl_owned'::regclass;

DROP TABLE tbl_owned;
REVOKE ALL ON ALL TABLES IN SCHEMA repack FROM tbl_other, tbl_member;
REVOKE ALL ON SCHEMA repack FROM tbl_other, tbl_member;
DROP ROLE tbl_member, tbl_other, tbl_owner;
