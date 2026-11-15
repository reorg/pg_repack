#!/bin/sh
#
# Regression helper for issue #456: when the connection of pg_repack is lost
# while the data copy is still running on the server, the cleanup must not be
# blocked by pg_repack's own (still running) backend, even with
# --no-kill-backend.
#
# Prints "skipped" if socat or timeout is not available, otherwise prints the
# number of repack objects left behind (expected: 0).

DB=contrib_regression
SRV_PORT=${PGPORT:-5432}
SRV_HOST=${PGHOST:-127.0.0.1}
case "$SRV_HOST" in /*) SRV_HOST=127.0.0.1 ;; esac
PROXY_PORT=$((SRV_PORT + 1000))

command -v socat >/dev/null 2>&1 && command -v timeout >/dev/null 2>&1 || { echo skipped; exit 0; }

psql -X -q -d $DB <<'SQL' >/dev/null 2>&1
DROP TABLE IF EXISTS lost_conn_t;
CREATE TABLE lost_conn_t (id int PRIMARY KEY, v text);
INSERT INTO lost_conn_t SELECT g, md5(g::text) || md5((g+1)::text) FROM generate_series(1, 12000000) g;
SQL

socat -d0 TCP-LISTEN:$PROXY_PORT,fork,reuseaddr TCP:$SRV_HOST:$SRV_PORT &
SOCAT=$!
sleep 1

timeout 120 pg_repack --dbname=$DB --host=127.0.0.1 --port=$PROXY_PORT \
	--table=lost_conn_t --no-kill-backend --wait-timeout=3 >/dev/null 2>&1 &
RP=$!

# wait for the data copy to start, then cut the established connections
for i in $(seq 1 30); do
	n=$(psql -X -Atq -d $DB -c "SELECT count(*) FROM pg_stat_activity WHERE query LIKE 'INSERT INTO repack.table_%'")
	[ "$n" -ge 1 ] && break
	sleep 1
done
pkill -P $SOCAT
wait $RP

# let the server notice that the copy ended, then count leftovers
for i in $(seq 1 60); do
	n=$(psql -X -Atq -d $DB -c "SELECT count(*) FROM pg_stat_activity WHERE query LIKE 'INSERT INTO repack.table_%' AND state = 'active'")
	[ "$n" -eq 0 ] && break
	sleep 1
done
psql -X -Atq -d $DB -c "SELECT (SELECT count(*) FROM pg_trigger WHERE tgname = 'repack_trigger' AND tgrelid = 'lost_conn_t'::regclass) + (SELECT count(*) FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace WHERE n.nspname = 'repack' AND c.relname IN ('table_' || 'lost_conn_t'::regclass::oid, 'log_' || 'lost_conn_t'::regclass::oid))"

kill $SOCAT 2>/dev/null
psql -X -q -d $DB -c "DROP TABLE IF EXISTS lost_conn_t" >/dev/null 2>&1
