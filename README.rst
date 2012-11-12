pg_repack -- Reorganize tables in PostgreSQL databases without any locks
========================================================================

- Homepage: http://reorg.github.com/pg_repack
- Development: https://github.com/reorg/pg_repack
- Bug Report: https://github.com/reorg/pg_reorg/issues
- Mailing List: http://pgfoundry.org/mailman/listinfo/reorg-general

pg_repack_ is an utility program to reorganize tables in PostgreSQL databases.
Unlike clusterdb_, it doesn't block any selections and updates during
reorganization.

Please check the documentation (in the ``doc`` directory or online_) for
installation and usage instructions.

.. _pg_repack: http://reorg.github.com/pg_repack
.. _clusterdb: http://www.postgresql.org/docs/current/static/app-clusterdb.html
.. _online: pg_repack_


What about pg_reorg?
--------------------

pg_repack is a fork of the pg_reorg_ project, which has proven hugely
successful; unfortunately its development has somewhat stagnated after the
release 1.1.7, with several issues still open (support for PostgreSQL 9.2,
EXTENSION, and several bugs to be fixed).  After initial consultation with the
pg_reorg authors, who showed interest in adopting more up-to-date development
tools to receive and encourage collaboration, no further news has been
received from them.  In the meantime we are releasing pg_repack 1.1.8 as a
drop-in replacement for pg_reorg, addressing the pg_reorg 1.1.7 bugs and
shortcomings and developing new features to be release in a future 1.2
version.  pg_repack may be an interim solution, should the pg_reorg project
come back to activity.

In the meantime, not having received further news from the pg_reorg authors,
we wish everything is fine with them, we thank them for the quality code they
have released to the community, and wish them good luck, in the hope to hear
from them again.

.. _pg_reorg: http://reorg.projects.pgfoundry.org/
