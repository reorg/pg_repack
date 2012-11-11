pg_repack -- Reorganize tables in PostgreSQL databases without any locks
========================================================================

- Homepage: https://github.com/reorg/pg_repack

pg_repack is an utility program to reorganize tables in PostgreSQL databases.
Unlike clusterdb_, it doesn't block any selections and updates during
reorganization.

pg_repack is a fork of the previous pg_reorg_ project. It was founded to
gather the bug fixes and new development ideas that the slow pace of
development of pg_reorg was struggling to satisfy.

.. _clusterdb: http://www.postgresql.org/docs/current/static/app-clusterdb.html
.. _pg_reorg: http://reorg.projects.pgfoundry.org/

Please check the documentation (in the ``doc`` directory) for installation and
usage instructions.
