pg_repack -- Reorganize tables in PostgreSQL databases without any locks
========================================================================

- Homepage: http://reorg.github.com/pg_repack
- Development: https://github.com/reorg/pg_repack
- Bug Report: https://github.com/reorg/pg_reorg/issues
- Mailing List: http://pgfoundry.org/mailman/listinfo/reorg-general

pg_repack_ is an utility program to reorganize tables in PostgreSQL databases.
Unlike clusterdb_, it doesn't block any selections and updates during
reorganization.

pg_repack is a fork of the previous pg_reorg_ project. It was founded to
gather the bug fixes and new development ideas that the slow pace of
development of pg_reorg was struggling to satisfy.

.. _pg_repack: http://reorg.github.com/pg_repack
.. _clusterdb: http://www.postgresql.org/docs/current/static/app-clusterdb.html
.. _pg_reorg: http://reorg.projects.pgfoundry.org/

Please check the documentation (in the ``doc`` directory or online_) for
installation and usage instructions.

.. _online: pg_repack_
