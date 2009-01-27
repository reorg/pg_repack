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

--
-- insert data
--

INSERT INTO tbl_cluster VALUES(1, '2008-12-31 10:00:00', 'admin');
INSERT INTO tbl_cluster VALUES(2, '2008-01-01 00:00:00', 'king');
INSERT INTO tbl_cluster VALUES(3, '2008-03-04 12:00:00', 'joker');
INSERT INTO tbl_cluster VALUES(4, '2008-03-05 15:00:00', 'queen');
INSERT INTO tbl_cluster VALUES(5, '2008-01-01 00:30:00', sqrt(2::numeric(1000,999))::text || sqrt(3::numeric(1000,999))::text);

INSERT INTO tbl_only_pkey VALUES(1, 'abc');
INSERT INTO tbl_only_pkey VALUES(2, 'def');

INSERT INTO tbl_only_ckey VALUES(1, '2008-01-01 00:00:00', 'abc');
INSERT INTO tbl_only_ckey VALUES(2, '2008-02-01 00:00:00', 'def');

INSERT INTO tbl_gistkey VALUES(1, '<(1,2),3>');
INSERT INTO tbl_gistkey VALUES(2, '<(4,5),6>');

--
-- do reorg
--

\! pg_reorg --dbname=contrib_regression --no-order
\! pg_reorg --dbname=contrib_regression
\! pg_reorg --dbname=contrib_regression --table=tbl_cluster

--
-- results
--

\d tbl_cluster
\d tbl_gistkey
\d tbl_only_ckey
\d tbl_only_pkey

SELECT col1, to_char(col2, 'YYYY-MM-DD HH24:MI:SS'), ":-)" FROM tbl_cluster;
SELECT * FROM tbl_only_ckey ORDER BY 1;
SELECT * FROM tbl_only_pkey ORDER BY 1;
SELECT * FROM tbl_gistkey ORDER BY 1;

--
-- clean up
--

DROP TABLE tbl_cluster;
DROP TABLE tbl_only_pkey;
DROP TABLE tbl_only_ckey;
DROP TABLE tbl_gistkey;
RESET client_min_messages;
