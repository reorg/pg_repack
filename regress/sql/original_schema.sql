--
-- Test repack by storing temporary objects in original schemas of target tables
--

-- Test that temporary objects are create in the original schema

CREATE SCHEMA test_orig;

CREATE TABLE test_orig.tbl (id integer PRIMARY KEY);
INSERT INTO test_orig.tbl VALUES (1), (2), (3);

-- Setup event trigger to verify where temporary objects are created
CREATE TABLE public.audit_log (schema_name text, obj_ident text);

CREATE OR REPLACE FUNCTION public.trg_audit_ddl()
RETURNS event_trigger AS $$
DECLARE
    obj record;
BEGIN
    FOR obj IN
        SELECT * FROM pg_event_trigger_ddl_commands()
        WHERE command_tag IN ('CREATE TABLE', 'CREATE TABLE AS', 'CREATE TYPE')
    LOOP
        IF obj.object_identity ~ '(log|table|pk)_' THEN
            INSERT INTO public.audit_log
            VALUES (obj.schema_name, regexp_replace(obj.object_identity, '\d+', 'OID', 'g'));
        END IF;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

CREATE EVENT TRIGGER audit_ddl ON ddl_command_end
    WHEN TAG IN ('CREATE TABLE', 'CREATE TABLE AS', 'CREATE TYPE')
    EXECUTE PROCEDURE public.trg_audit_ddl();

\! pg_repack --dbname=contrib_regression --table=test_orig.tbl --use-original-schema

SELECT * FROM test_orig.tbl ORDER BY id;

SELECT * FROM public.audit_log ORDER BY obj_ident;

-- Check that temporary objects were cleaned up
SELECT relname
FROM pg_class
WHERE relnamespace = 'test_orig'::regnamespace AND relname ~ '^(log|table)_';

SELECT typname
FROM pg_type
WHERE typnamespace = 'test_orig'::regnamespace AND typname ~ '^pk_';

-- Cleanup
DROP EVENT TRIGGER audit_ddl;
DROP FUNCTION public.trg_audit_ddl();
DROP TABLE public.audit_log;

-- Test that --only-indexes doesn't support --use-original-schema

\! pg_repack --dbname=contrib_regression --table=test_orig.tbl --use-original-schema --only-indexes

