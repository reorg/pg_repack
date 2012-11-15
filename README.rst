pg_repack -- Reorganize tables in PostgreSQL databases with minimal locks
=========================================================================

- Homepage: http://reorg.github.com/pg_repack
- Download: http://pgxn.org/dist/pg_repack/
- Development: https://github.com/reorg/pg_repack
- Bug Report: https://github.com/reorg/pg_reorg/issues
- Mailing List: http://pgfoundry.org/mailman/listinfo/reorg-general

pg_repack_ is a PostgreSQL extension which lets you remove bloat from
tables and indexes, and optionally restore the physical order of clustered
indexes. Unlike CLUSTER_ and `VACUUM FULL`_ it works online, without
holding an exclusive lock on the processed tables during processing.
pg_repack is efficient to boot, with performance comparable to using
CLUSTER directly.

Please check the documentation (in the ``doc`` directory or online_) for
installation and usage instructions.

.. _pg_repack: http://reorg.github.com/pg_repack
.. _CLUSTER: http://www.postgresql.org/docs/current/static/sql-cluster.html
.. _VACUUM FULL: VACUUM_
.. _VACUUM: http://www.postgresql.org/docs/current/static/sql-vacuum.html
.. _online: pg_repack_


What about pg_reorg?
--------------------

pg_repack is a fork of the pg_reorg_ project, which has proven hugely
successful.  Unfortunately the last version of pg_reorg was 1.1.7, released
in Aug. 2011, and development of the project has stagnated since then.  The
first release of pg_repack contains many key improvements which have been
missing from pg_reorg (e.g. support for PostgreSQL 9.2, and EXTENSION
packaging).

In an effort to make the transition for existing pg_reorg users simple, and
facilitate a possible merge back with pg_reorg, we are releasing
pg_repack 1.1.8 as a drop-in replacement for pg_reorg, addressing the
pg_reorg 1.1.7 bugs and shortcomings. We are also developing new features
to be released in an upcoming 1.2 version.  pg_repack may be an interim
solution, should the pg_reorg project come back to life.

In the meantime, we thank the original pg_reorg authors for the quality code
they have released to the community, wish them good luck, and hope to
collaborate further in the future.

.. _pg_reorg: http://reorg.projects.pgfoundry.org/
