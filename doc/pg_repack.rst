pg_repack -- Reorganize tables in PostgreSQL databases with minimal locks
=========================================================================

.. contents::
    :depth: 1
    :backlinks: none

pg_repack_ is a PostgreSQL extension which lets you remove bloat from
tables and indexes, and optionally restore the physical order of clustered
indexes. Unlike CLUSTER_ and `VACUUM FULL`_ it works online, without
holding an exclusive lock on the processed tables during processing.
pg_repack is efficient to boot, with performance comparable to using
CLUSTER directly.

pg_repack is a fork of the previous pg_reorg_ project. Please check the
`project page`_ for bug report and development information.

You can choose one of the following methods to reorganize:

* Online CLUSTER (ordered by cluster index)
* Ordered by specified columns
* Online VACUUM FULL (packing rows only)
* Rebuild or relocate only the indexes of a table

NOTICE:

* Only superusers can use the utility.
* Target table must have a PRIMARY KEY, or at least a UNIQUE total index on a
  NOT NULL column.

.. _pg_repack: https://reorg.github.io/pg_repack
.. _CLUSTER: http://www.postgresql.org/docs/current/static/sql-cluster.html
.. _VACUUM FULL: VACUUM_
.. _VACUUM: http://www.postgresql.org/docs/current/static/sql-vacuum.html
.. _project page: https://github.com/reorg/pg_repack
.. _pg_reorg: https://github.com/reorg/pg_reorg


Requirements
------------

PostgreSQL versions
    PostgreSQL 9.4, 9.5, 9.6, 10, 11, 12, 13, 14, 15

Disks
    Performing a full-table repack requires free disk space about twice as
    large as the target table(s) and its indexes. For example, if the total
    size of the tables and indexes to be reorganized is 1GB, an additional 2GB
    of disk space is required.


Download
--------

You can `download pg_repack`__ from the PGXN website. Unpack the archive and
follow the installation_ instructions.

.. __: http://pgxn.org/dist/pg_repack/

Alternatively you can use the `PGXN Client`_ to download, compile and install
the package; use::

    $ pgxn install pg_repack

Check the `pgxn install documentation`__ for the options available.

.. _PGXN Client: https://pgxn.github.io/pgxnclient/
.. __: https://pgxn.github.io/pgxnclient/usage.html#pgxn-install


Installation
------------

pg_repack can be built with ``make`` on UNIX or Linux. The PGXS build
framework is used automatically. Before building, you might need to install
the PostgreSQL development packages (``postgresql-devel``, etc.) and add the
directory containing ``pg_config`` to your ``$PATH``. Then you can run::

    $ cd pg_repack
    $ make
    $ sudo make install

You can also use Microsoft Visual C++ 2010 to build the program on Windows.
There are project files in the ``msvc`` folder.

After installation, load the pg_repack extension in the database you want to
process. pg_repack is packaged as an extension, so you can execute::

    $ psql -c "CREATE EXTENSION pg_repack" -d your_database

You can remove pg_repack using ``DROP EXTENSION pg_repack`` or just dropping
the ``repack`` schema.

If you are upgrading from a previous version of pg_repack or pg_reorg, just
drop the old version from the database as explained above and install the new
version.


Usage
-----

::

    pg_repack [OPTION]... [DBNAME]

The following options can be specified in ``OPTIONS``.

Options:
  -a, --all                 repack all databases
  -t, --table=TABLE         repack specific table only
  -I, --parent-table=TABLE  repack specific parent table and its inheritors
  -c, --schema=SCHEMA       repack tables in specific schema only
  -s, --tablespace=TBLSPC   move repacked tables to a new tablespace
  -S, --moveidx             move repacked indexes to *TBLSPC* too
  -o, --order-by=COLUMNS    order by columns instead of cluster keys
  -n, --no-order            do vacuum full instead of cluster
  -N, --dry-run             print what would have been repacked and exit
  -j, --jobs=NUM            Use this many parallel jobs for each table
  -i, --index=INDEX         move only the specified index
  -x, --only-indexes        move only indexes of the specified table
  -T, --wait-timeout=SECS   timeout to cancel other backends on conflict
  -D, --no-kill-backend     don't kill other backends when timed out
  -Z, --no-analyze          don't analyze at end
  -k, --no-superuser-check  skip superuser checks in client
  -C, --exclude-extension   don't repack tables which belong to specific extension

Connection options:
  -d, --dbname=DBNAME       database to connect
  -h, --host=HOSTNAME       database server host or socket directory
  -p, --port=PORT           database server port
  -U, --username=USERNAME   user name to connect as
  -w, --no-password         never prompt for password
  -W, --password            force password prompt

Generic options:
  -e, --echo                echo queries
  -E, --elevel=LEVEL        set output message level
  --help                    show this help, then exit
  --version                 output version information, then exit


Reorg Options
^^^^^^^^^^^^^

``-a``, ``--all``
    Attempt to repack all the databases of the cluster. Databases where the
    ``pg_repack`` extension is not installed will be skipped.

``-t TABLE``, ``--table=TABLE``
    Reorganize the specified table(s) only. Multiple tables may be
    reorganized by writing multiple ``-t`` switches. By default, all eligible
    tables in the target databases are reorganized.

``-I TABLE``, ``--parent-table=TABLE``
    Reorganize both the specified table(s) and its inheritors. Multiple
    table hierarchies may be reorganized by writing multiple ``-I`` switches.

``-c``, ``--schema``
    Repack the tables in the specified schema(s) only. Multiple schemas may
    be repacked by writing multiple ``-c`` switches. May be used in
    conjunction with ``--tablespace`` to move tables to a different tablespace.

``-o COLUMNS [,...]``, ``--order-by=COLUMNS [,...]``
    Perform an online CLUSTER ordered by the specified columns.

``-n``, ``--no-order``
    Perform an online VACUUM FULL.  Since version 1.2 this is the default for
    non-clustered tables.

``-N``, ``--dry-run``
    List what would be repacked and exit.

``-j``, ``--jobs``
    Create the specified number of extra connections to PostgreSQL, and
    use these extra connections to parallelize the rebuild of indexes
    on each table. Parallel index builds are only supported for full-table
    repacks, not with ``--index`` or ``--only-indexes`` options. If your
    PostgreSQL server has extra cores and disk I/O available, this can be a
    useful way to speed up pg_repack.

``-s TBLSPC``, ``--tablespace=TBLSPC``
    Move the repacked tables to the specified tablespace: essentially an
    online version of ``ALTER TABLE ... SET TABLESPACE``. The tables' indexes
    are left in the original tablespace unless ``--moveidx`` is specified too.

``-S``, ``--moveidx``
    Also move the indexes of the repacked tables to the tablespace specified
    by the ``--tablespace`` option.

``-i``, ``--index``
    Repack the specified index(es) only. Multiple indexes may be repacked
    by writing multiple ``-i`` switches. May be used in conjunction with
    ``--tablespace`` to move the index to a different tablespace.

``-x``, ``--only-indexes``
    Repack only the indexes of the specified table(s), which must be specified
    with the ``--table`` or ``--parent-table`` options.

``-T SECS``, ``--wait-timeout=SECS``
    pg_repack needs to take an exclusive lock at the end of the
    reorganization.  This setting controls how many seconds pg_repack will
    wait to acquire this lock. If the lock cannot be taken after this duration
    and ``--no-kill-backend`` option is not specified, pg_repack will forcibly
    cancel the conflicting queries. If you are using PostgreSQL version 8.4
    or newer, pg_repack will fall back to using pg_terminate_backend() to
    disconnect any remaining backends after twice this timeout has passed.
    The default is 60 seconds.

``-D``, ``--no-kill-backend``
    Skip to repack table if the lock cannot be taken for duration specified
    ``--wait-timeout``, instead of cancelling conflicting queries. The default
    is false.

``-Z``, ``--no-analyze``
    Disable ANALYZE after a full-table reorganization. If not specified, run
    ANALYZE after the reorganization.

``-k``, ``--no-superuser-check``
    Skip the superuser checks in the client.  This setting is useful for using
    pg_repack on platforms that support running it as non-superusers.

``-C``, ``--exclude-extension``
    Skip tables that belong to the specified extension(s). Some extensions
    may heavily depend on such tables at planning time etc.

Connection Options
^^^^^^^^^^^^^^^^^^

Options to connect to servers. You cannot use ``--all`` and ``--dbname`` or
``--table`` or ``--parent-table`` together.

``-a``, ``--all``
    Reorganize all databases.

``-d DBNAME``, ``--dbname=DBNAME``
    Specifies the name of the database to be reorganized. If this is not
    specified and ``-a`` (or ``--all``) is not used, the database name is read
    from the environment variable PGDATABASE. If that is not set, the user
    name specified for the connection is used.

``-h HOSTNAME``, ``--host=HOSTNAME``
    Specifies the host name of the machine on which the server is running. If
    the value begins with a slash, it is used as the directory for the Unix
    domain socket.

``-p PORT``, ``--port=PORT``
    Specifies the TCP port or local Unix domain socket file extension on which
    the server is listening for connections.

``-U USERNAME``, ``--username=USERNAME``
    User name to connect as.

``-w``, ``--no-password``
    Never issue a password prompt. If the server requires password
    authentication and a password is not available by other means such as a
    ``.pgpass`` file, the connection attempt will fail. This option can be
    useful in batch jobs and scripts where no user is present to enter a
    password.

``-W``, ``--password``
    Force the program to prompt for a password before connecting to a
    database.

    This option is never essential, since the program will automatically
    prompt for a password if the server demands password authentication.
    However, pg_repack will waste a connection attempt finding out that the
    server wants a password. In some cases it is worth typing ``-W`` to avoid
    the extra connection attempt.


Generic Options
^^^^^^^^^^^^^^^

``-e``, ``--echo``
    Echo commands sent to server.

``-E LEVEL``, ``--elevel=LEVEL``
    Choose the output message level from ``DEBUG``, ``INFO``, ``NOTICE``,
    ``WARNING``, ``ERROR``, ``LOG``, ``FATAL``, and ``PANIC``. The default is
    ``INFO``.

``--help``
    Show usage of the program.

``--version``
    Show the version number of the program.


Environment
-----------

``PGDATABASE``, ``PGHOST``, ``PGPORT``, ``PGUSER``
    Default connection parameters

    This utility, like most other PostgreSQL utilities, also uses the
    environment variables supported by libpq (see `Environment Variables`__).

    .. __: http://www.postgresql.org/docs/current/static/libpq-envars.html


Examples
--------

Perform an online CLUSTER of all the clustered tables in the database
``test``, and perform an online VACUUM FULL of all the non-clustered tables::

    $ pg_repack test

Perform an online VACUUM FULL on the tables ``foo`` and ``bar`` in the
database ``test`` (an eventual cluster index is ignored)::

    $ pg_repack --no-order --table foo --table bar test

Move all indexes of table ``foo`` to tablespace ``tbs``::

    $ pg_repack -d test --table foo --only-indexes --tablespace tbs

Move the specified index to tablespace ``tbs``::

    $ pg_repack -d test --index idx --tablespace tbs


Diagnostics
-----------

Error messages are reported when pg_repack fails. The following list shows the
cause of errors.

You need to cleanup by hand after fatal errors. To cleanup, just remove
pg_repack from the database and install it again: for PostgreSQL 9.1 and
following execute ``DROP EXTENSION pg_repack CASCADE`` in the database where
the error occurred, followed by ``CREATE EXTENSION pg_repack``; for previous
version load the script ``$SHAREDIR/contrib/uninstall_pg_repack.sql`` into the
database where the error occured and then load
``$SHAREDIR/contrib/pg_repack.sql`` again.

.. class:: diag

INFO: database "db" skipped: pg_repack VER is not installed in the database
    pg_repack is not installed in the database when the ``--all`` option is
    specified.

    Create the pg_repack extension in the database.

ERROR: pg_repack VER is not installed in the database
    pg_repack is not installed in the database specified by ``--dbname``.

    Create the pg_repack extension in the database.

ERROR: program 'pg_repack V1' does not match database library 'pg_repack V2'
    There is a mismatch between the ``pg_repack`` binary and the database
    library (``.so`` or ``.dll``).

    The mismatch could be due to the wrong binary in the ``$PATH`` or the
    wrong database being addressed. Check the program directory and the
    database; if they are what expected you may need to repeat pg_repack
    installation.

ERROR: extension 'pg_repack V1' required, found 'pg_repack V2'
    The SQL extension found in the database does not match the version
    required by the pg_repack program.

    You should drop the extension from the database and reload it as described
    in the installation_ section.

ERROR: relation "table" must have a primary key or not-null unique keys
    The target table doesn't have a PRIMARY KEY or any UNIQUE constraints
    defined.

    Define a PRIMARY KEY or a UNIQUE constraint on the table.

ERROR: query failed: ERROR: column "col" does not exist
    The target table doesn't have columns specified by ``--order-by`` option.

    Specify existing columns.

WARNING: the table "tbl" already has a trigger called repack_trigger
    The trigger was probably installed during a previous attempt to run
    pg_repack on the table which was interrupted and for some reason failed
    to clean up the temporary objects.

    You can remove all the temporary objects by dropping and re-creating the
    extension: see the installation_ section for the details.

ERROR: Another pg_repack command may be running on the table. Please try again later.
    There is a chance of deadlock when two concurrent pg_repack commands are
    run on the same table. So, try to run the command after some time.

WARNING: Cannot create index  "schema"."index_xxxxx", already exists
    DETAIL: An invalid index may have been left behind by a previous pg_repack
    on the table which was interrupted. Please use DROP INDEX
    "schema"."index_xxxxx" to remove this index and try again.

    A temporary index apparently created by pg_repack has been left behind, and
    we do not want to risk dropping this index ourselves. If the index was in
    fact created by an old pg_repack job which didn't get cleaned up, you
    should just use DROP INDEX and try the repack command again.


Restrictions
------------

pg_repack comes with the following restrictions.

Temp tables
^^^^^^^^^^^

pg_repack cannot reorganize temp tables.

GiST indexes
^^^^^^^^^^^^

pg_repack cannot cluster tables by GiST indexes.

DDL commands
^^^^^^^^^^^^

You will not be able to perform DDL commands of the target table(s) **except**
VACUUM or ANALYZE while pg_repack is working. pg_repack will hold an
ACCESS SHARE lock on the target table during a full-table repack, to enforce
this restriction.

If you are using version 1.1.8 or earlier, you must not attempt to perform any
DDL commands on the target table(s) while pg_repack is running. In many cases
pg_repack would fail and rollback correctly, but there were some cases in these
earlier versions which could result in data corruption.


Details
-------

Full Table Repacks
^^^^^^^^^^^^^^^^^^

To perform a full-table repack, pg_repack will:

1. create a log table to record changes made to the original table
2. add a trigger onto the original table, logging INSERTs, UPDATEs and DELETEs into our log table
3. create a new table containing all the rows in the old table
4. build indexes on this new table
5. apply all changes which have accrued in the log table to the new table
6. swap the tables, including indexes and toast tables, using the system catalogs
7. drop the original table

pg_repack will only hold an ACCESS EXCLUSIVE lock for a short period during
initial setup (steps 1 and 2 above) and during the final swap-and-drop phase
(steps 6 and 7). For the rest of its time, pg_repack only needs
to hold an ACCESS SHARE lock on the original table, meaning INSERTs, UPDATEs,
and DELETEs may proceed as usual.


Index Only Repacks
^^^^^^^^^^^^^^^^^^

To perform an index-only repack, pg_repack will:

1. create new indexes on the table using CONCURRENTLY matching the definitions of the old indexes
2. swap out the old for the new indexes in the catalogs
3. drop the old indexes

Creating indexes concurrently comes with a few caveats, please see `the documentation`__ for details.

    .. __: http://www.postgresql.org/docs/current/static/sql-createindex.html#SQL-CREATEINDEX-CONCURRENTLY


Releases
--------

* pg_repack 1.4.9 (unreleased)

  * Fixed crash in ``get_order_by()`` using invalid relations (issue #321)

* pg_repack 1.4.8

  * Added support for PostgreSQL 15
  * Fixed --parent-table on declarative partitioned tables (issue #288)
  * Removed connection info from error log (issue #285)

* pg_repack 1.4.7

  * Added support for PostgreSQL 14

* pg_repack 1.4.6

  * Added support for PostgreSQL 13
  * Dropped support for PostgreSQL before 9.4

* pg_repack 1.4.5

  * Added support for PostgreSQL 12
  * Fixed parallel processing for indexes with operators from public schema

* pg_repack 1.4.4

  * Added support for PostgreSQL 11 (issue #181)
  * Remove duplicate password prompt (issue #184)

* pg_repack 1.4.3

  * Fixed possible CVE-2018-1058 attack paths (issue #168)
  * Fixed "unexpected index definition" after CVE-2018-1058 changes in
    PostgreSQL (issue #169)
  * Fixed build with recent Ubuntu packages (issue #179)

* pg_repack 1.4.2

  * added PostgreSQL 10 support (issue #120)
  * fixed error DROP INDEX CONCURRENTLY cannot run inside a transaction block
    (issue #129)

* pg_repack 1.4.1

  * fixed broken ``--order-by`` option (issue #138)

* pg_repack 1.4

  * added support for PostgreSQL 9.6, dropped support for versions before 9.1
  * use ``AFTER`` trigger to solve concurrency problems with ``INSERT
    CONFLICT`` (issue #106)
  * added ``--no-kill-backend`` option (issue #108)
  * added ``--no-superuser-check`` option (issue #114)
  * added ``--exclude-extension`` option (#97)
  * added ``--parent-table`` option (#117)
  * restore TOAST storage parameters on repacked tables (issue #10)
  * restore columns storage types in repacked tables (issue #94)

* pg_repack 1.3.4

  * grab exclusive lock before dropping original table (issue #81)
  * do not attempt to repack unlogged tables (issue #71)

* pg_repack 1.3.3

  * Added support for PostgreSQL 9.5
  * Fixed possible deadlock when pg_repack command is interrupted (issue #55)
  * Fixed exit code for when pg_repack is invoked with ``--help`` and
    ``--version``
  * Added Japanese language user manual

* pg_repack 1.3.2

  * Fixed to clean up temporary objects when pg_repack command is interrupted.
  * Fixed possible crash when pg_repack shared library is loaded alongside
    pg_statsinfo (issue #43).

* pg_repack 1.3.1

  * Added support for PostgreSQL 9.4.

* pg_repack 1.3

  * Added ``--schema`` to repack only the specified schema (issue #20).
  * Added ``--dry-run`` to do a dry run (issue #21).
  * Fixed advisory locking for >2B OID values (issue #30).
  * Avoid possible deadlock when other sessions lock a to-be-repacked
    table (issue #32).
  * Performance improvement for performing sql_pop DELETEs many-at-a-time.
  * Attempt to avoid pg_repack taking forever when dealing with a
    constant heavy stream of changes to a table.

* pg_repack 1.2

  * Support PostgreSQL 9.3.
  * Added ``--tablespace`` and ``--moveidx`` options to perform online
    SET TABLESPACE.
  * Added ``--index`` to repack the specified index only.
  * Added ``--only-indexes`` to repack only the indexes of the specified table
  * Added ``--jobs`` option for parallel operation.
  * Don't require ``--no-order`` to perform a VACUUM FULL on non-clustered
    tables (pg_repack issue #6).
  * Don't wait for locks held in other databases (pg_repack issue #11).
  * Bugfix: correctly handle key indexes with options such as DESC, NULL
    FIRST/LAST, COLLATE (pg_repack issue #3).
  * Fixed data corruption bug on delete (pg_repack issue #23).
  * More helpful program output and error messages.

* pg_repack 1.1.8

  * Added support for PostgreSQL 9.2.
  * Added support for CREATE EXTENSION on PostgreSQL 9.1 and following.
  * Give user feedback while waiting for transactions to finish  (pg_reorg
    issue #5).
  * Bugfix: Allow running on newly promoted streaming replication slaves
    (pg_reorg issue #1).
  * Bugfix: Fix interaction between pg_repack and Slony 2.0/2.1 (pg_reorg
    issue #4)
  * Bugfix: Properly escape column names (pg_reorg issue #6).
  * Bugfix: Avoid recreating invalid indexes, or choosing them as key
    (pg_reorg issue #9).
  * Bugfix: Never choose a partial index as primary key (pg_reorg issue #22).

* pg_reorg 1.1.7 (2011-08-07)

  * Bugfix: VIEWs and FUNCTIONs could be corrupted that used a reorganized
    table which has a dropped column.
  * Supports PostgreSQL 9.1 and 9.2dev. (but EXTENSION is not yet)


See Also
--------

* `clusterdb <http://www.postgresql.org/docs/current/static/app-clusterdb.html>`__
* `vacuumdb <http://www.postgresql.org/docs/current/static/app-vacuumdb.html>`__
