--
-- do repack
--

\! pg_repack --dbname=contrib_regression --table=tbl_cluster
\! pg_repack --dbname=contrib_regression --table=tbl_badindex
\! pg_repack --dbname=contrib_regression
