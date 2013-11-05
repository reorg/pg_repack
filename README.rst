pg_repack -- Reorganize tables in PostgreSQL databases with minimal locks
=========================================================================

- Homepage: http://reorg.github.com/pg_repack
- Download: http://pgxn.org/dist/pg_repack/
- Development: https://github.com/reorg/pg_repack
- Bug Report: https://github.com/reorg/pg_repack/issues
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
successful. Unfortunately development appears to have stopped after the
release of the version 1.1.7, around August 2011.

pg_repack 1.1.8 was released as a drop-in replacement for pg_reorg, addressing
some of the shortcomings of the last pg_reorg version (such as support for
PostgreSQL 9.2 and EXTENSION packaging) and known bugs. Shortly after the
first pg_repack release, pg_reorg 1.1.8 was released too, merging all the
pg_repack changes. Version 1.1.8 is the last pg_reorg release at the time of
writing.

pg_repack 1.2 is a new development line based on the original pg_reorg
codebase and offering new features. Its behaviour may be different from the
1.1.x release so it shouldn't be considered a drop-in replacement: you are
advised to check the documentation__ before upgrading from previous versions.

.. __: pg_repack_
.. _pg_reorg: http://reorg.projects.pgfoundry.org/
