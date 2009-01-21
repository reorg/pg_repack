SET client_min_messages = warning;
--
-- create table.
--
CREATE TABLE tbl_cluster (
	col1 int,
	col2 timestamp,
	":-)" text,
	primary key(":-)", col1)
) WITH (fillfactor = 70);

CREATE INDEX cidx_cluster ON tbl_cluster (col2, length(":-)"));
ALTER TABLE tbl_cluster CLUSTER ON cidx_cluster;

CREATE TABLE tbl_only_pkey (
	col1 int PRIMARY KEY,
	":-)" text
);

CREATE TABLE tbl_only_ckey (
	col1 int,
	col2 timestamp,
	":-)" text
) WITH (fillfactor = 70);

CREATE INDEX cidx_only_ckey ON tbl_only_ckey (col2, ":-)");
ALTER TABLE tbl_only_ckey CLUSTER ON cidx_only_ckey;

CREATE TABLE tbl_gistkey (
	id integer PRIMARY KEY,
	c circle
);

CREATE INDEX cidx_circle ON tbl_gistkey USING gist (c);
ALTER TABLE tbl_gistkey CLUSTER ON cidx_circle;

INSERT INTO tbl_cluster VALUES(1, '2008-12-31 10:00:00', 'admin');
INSERT INTO tbl_cluster VALUES(2, '2008-01-01 00:00:00', 'king');
INSERT INTO tbl_cluster VALUES(3, '2008-03-04 12:00:00', 'joker');
INSERT INTO tbl_cluster VALUES(4, '2008-03-05 15:00:00', 'queen');
INSERT INTO tbl_cluster VALUES(5, '2008-01-01 00:30:00', sqrt(2::numeric(1000,999))::text || sqrt(3::numeric(1000,999))::text);

--
-- do reorg
--

\! pg_reorg --dbname=contrib_regression --table=tbl_cluster

--
-- results
--

\d+ tbl_cluster
\d+ tbl_gistkey
\d+ tbl_only_ckey
\d+ tbl_only_pkey

SET synchronize_seqscans = off;
SELECT col1, to_char(col2, 'YYYY-MM-DD HH24:MI:SS'), ":-)" FROM tbl_cluster;
SELECT * FROM tbl_gistkey;
SELECT * FROM tbl_only_ckey;
SELECT * FROM tbl_only_pkey;
RESET synchronize_seqscans;

--
-- clean up
--

DROP TABLE tbl_cluster;
DROP TABLE tbl_gistkey;
DROP TABLE tbl_only_pkey;
DROP TABLE tbl_only_ckey;
RESET client_min_messages;
