--
-- do repack
--

\! pg_repack --dbname=contrib_regression --table=tbl_cluster --error-on-invalid-index
\! pg_repack --dbname=contrib_regression --table=tbl_badindex --error-on-invalid-index
\! pg_repack --dbname=contrib_regression --error-on-invalid-index
