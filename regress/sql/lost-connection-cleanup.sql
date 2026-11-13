--
-- lost connection during the data copy: cleanup must not be blocked
-- by pg_repack's own backend (issue #456)
--
\! sh ./lost-connection-cleanup.sh
