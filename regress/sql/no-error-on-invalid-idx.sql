--
-- do repack
--

\! pg_repack --dbname=contrib_regression --table=tbl_cluster --no-error-on-invalid-index
\! pg_repack --dbname=contrib_regression --table=tbl_badindex --no-error-on-invalid-index
\! pg_repack --dbname=contrib_regression --no-error-on-invalid-index
