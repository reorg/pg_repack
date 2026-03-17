--
-- repack will conflict with publication FOR ALL TABLES
--

create publication test for all tables ;

\! pg_repack --dbname=contrib_regression --table=tbl_cluster
\! pg_repack --dbname=contrib_regression --table=tbl_cluster --no-error-on-publication

drop publication test;
