--
-- repack.repack_trigger tests
--

CREATE TABLE trigger_t1 (a int, b int, primary key (a, b));
CREATE INDEX trigger_t1_idx ON trigger_t1 (a, b);

SELECT create_trigger FROM repack.tables WHERE relname = 'public.trigger_t1';

SELECT oid AS t1_oid FROM pg_catalog.pg_class WHERE relname = 'trigger_t1'
\gset

CREATE TYPE repack.pk_:t1_oid AS (a integer, b integer);
CREATE TABLE repack.log_:t1_oid (id bigserial PRIMARY KEY, pk repack.pk_:t1_oid, row public.trigger_t1);
CREATE TRIGGER repack_trigger AFTER INSERT OR DELETE OR UPDATE ON trigger_t1
    FOR EACH ROW EXECUTE PROCEDURE repack.repack_trigger('a', 'b');

INSERT INTO trigger_t1 VALUES (111, 222);
UPDATE trigger_t1 SET a=333, b=444 WHERE a = 111;
DELETE FROM trigger_t1 WHERE a = 333;
SELECT * FROM repack.log_:t1_oid;
