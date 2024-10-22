.. pg_repack -- Reorganize tables in PostgreSQL databases with minimal locks
   =========================================================================

pg_repack -- 使用最少的锁重新组织 PostgreSQL 数据库中的表
=============================================================================

.. contents::
    :depth: 1
    :backlinks: none

.. pg_repack_ is a PostgreSQL extension which lets you remove bloat from
    tables and indexes, and optionally restore the physical order of clustered
    indexes. Unlike CLUSTER_ and `VACUUM FULL`_ it works online, without
    holding an exclusive lock on the processed tables during processing.
    pg_repack is efficient to boot, with performance comparable to using
    CLUSTER directly.

pg_repack_ 是 PostgreSQL 的一个扩展，它允许您从表和索引中删除膨胀，并可选择恢复聚集索引的物理顺序。
与 CLUSTER_ 和 `VACUUM FULL`_ 不同，它可以在线工作，在处理过程中无需对已处理的表保持独占锁定。
pg_repack 启动效率高，性能可与直接使用 CLUSTER 相媲美。

.. pg_repack is a fork of the previous pg_reorg_ project. Please check the
   `project page`_ for bug report and development information.

pg_repack 是前一个 pg_reorg_ 项目的一个分支。
请查看 `project page`_ 以获取错误报告和开发信息。

.. You can choose one of the following methods to reorganize:
  
  * Online CLUSTER (ordered by cluster index)
  * Ordered by specified columns
  * Online VACUUM FULL (packing rows only)
  * Rebuild or relocate only the indexes of a table

您可以选择以下方法之一进行重组：

* 在线 CLUSTER (ordered by cluster index)
* 按指定列排序
* 在线 VACUUM FULL (packing rows only)
* 仅重建或重新定位表的索引

.. NOTICE:
  
  * Only superusers can use the utility.
  * Target table must have a PRIMARY KEY, or at least a UNIQUE total index on a
    NOT NULL column.

注意:

* 只有超级用户可以使用该实用程序。
* 目标表必须具有 PRIMARY KEY，或者至少在 NOT NULL 列上具有 UNIQUE 总索引。

.. _pg_repack: https://reorg.github.io/pg_repack
.. _CLUSTER: http://www.postgresql.org/docs/current/static/sql-cluster.html
.. _VACUUM FULL: VACUUM_
.. _VACUUM: http://www.postgresql.org/docs/current/static/sql-vacuum.html
.. _project page: https://github.com/reorg/pg_repack
.. _pg_reorg: https://github.com/reorg/pg_reorg


.. Requirements
  ------------
  
  PostgreSQL versions
      PostgreSQL 9.5, 9.6, 10, 11, 12, 13, 14, 15, 16, 17.
  
      PostgreSQL 9.4 and before it are not supported.
  
  Disks
      Performing a full-table repack requires free disk space about twice as
      large as the target table(s) and its indexes. For example, if the total
      size of the tables and indexes to be reorganized is 1GB, an additional 2GB
      of disk space is required.


要求
---------

PostgreSQL 版本
    PostgreSQL 9.5, 9.6, 10, 11, 12, 13, 14, 15, 16, 17

    PostgreSQL 9.4 及之前版本不受支持。

磁盘
    执行全表重新打包需要的可用磁盘空间大约是目标表及其索引的两倍。例如，如果要重组的表和索引的总大小为 1GB，则需要额外的 2GB 磁盘空间。

.. Download
  --------
  
  You can `download pg_repack`__ from the PGXN website. Unpack the archive and
  follow the 安装_ instructions.
  
  .. __: http://pgxn.org/dist/pg_repack/
  
  Alternatively you can use the `PGXN Client`_ to download, compile and install
  the package; use::
  
      $ pgxn install pg_repack
  
  Check the `pgxn install documentation`__ for the options available.
  
  .. _PGXN Client: https://pgxn.github.io/pgxnclient/
  .. __: https://pgxn.github.io/pgxnclient/usage.html#pgxn-install


下载
------------

您可以从 PGXN 网站 `download pg_repack`__。解压存档并按照 `安装`_ 说明进行操作。

.. __: http://pgxn.org/dist/pg_repack/

或者，您可以使用 `PGXN Client`_ 来下载、编译和安装该包；使用::

    $ pgxn install pg_repack

查看 `pgxn install documentation`__ 以了解可用的选项。

.. _PGXN Client: https://pgxn.github.io/pgxnclient/
.. __: https://pgxn.github.io/pgxnclient/usage.html#pgxn-install


.. Installation
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

安装
------------

pg_repack 可以在 UNIX 或 Linux 上使用 ``make`` 构建。PGXS 构建框架会自动使用。
在构建之前，您可能需要安装 PostgreSQL 开发包（ ``postgresql-devel`` 等），并将包含 ``pg_config`` 的目录添加到 ``$PATH`` 中。然后您可以运行::

    $ cd pg_repack
    $ make
    $ sudo make install

您也可以使用Microsoft Visual C++ 2010在Windows上构建程序。在 ``msvc`` 文件夹中有项目文件。

安装后，在你要处理的数据库中加载pg_repack扩展。pg_repack 被打包为一个扩展，因此你可以执行::

    $ psql -c "CREATE EXTENSION pg_repack" -d your_database

您可以使用 ``DROP EXTENSION pg_repack`` 或直接删除 ``repack`` 模式来删除 pg_repack。

如果您要从以前版本的 pg_repack 或 pg_reorg 升级，只需按照上述说明从数据库中删除旧版本并安装新版本。

.. Usage
  -----
  
  ::
  
      pg_repack [OPTION]... [DBNAME]
  
  The following options can be specified in ``OPTIONS``.
  
  Options:
    -a, --all                     repack all databases
    -t, --table=TABLE             repack specific table only
    -I, --parent-table=TABLE      repack specific parent table and its inheritors
    -c, --schema=SCHEMA           repack tables in specific schema only
    -s, --tablespace=TBLSPC       move repacked tables to a new tablespace
    -S, --moveidx                 move repacked indexes to *TBLSPC* too
    -o, --order-by=COLUMNS        order by columns instead of cluster keys
    -n, --no-order                do vacuum full instead of cluster
    -N, --dry-run                 print what would have been repacked and exit
    -j, --jobs=NUM                Use this many parallel jobs for each table
    -i, --index=INDEX             move only the specified index
    -x, --only-indexes            move only indexes of the specified table
    -T, --wait-timeout=SECS       timeout to cancel other backends on conflict
    -D, --no-kill-backend         don't kill other backends when timed out
    -Z, --no-analyze              don't analyze at end
    -k, --no-superuser-check      skip superuser checks in client
    -C, --exclude-extension       don't repack tables which belong to specific extension
        --error-on-invalid-index  don't repack when invalid index is found
        --apply-count             number of tuples to apply in one trasaction during replay
        --switch-threshold        switch tables when that many tuples are left to catchup
  
  Connection options:
    -d, --dbname=DBNAME           database to connect
    -h, --host=HOSTNAME           database server host or socket directory
    -p, --port=PORT               database server port
    -U, --username=USERNAME       user name to connect as
    -w, --no-password             never prompt for password
    -W, --password                force password prompt
  
  Generic options:
    -e, --echo                    echo queries
    -E, --elevel=LEVEL            set output message level
    --help                        show this help, then exit
    --version                     output version information, then exit

用法
---------

::

    pg_repack [OPTION]... [DBNAME]

可以在 ``OPTIONS`` 中指定以下选项。

选项:
  -a, --all                     重新打包所有数据库
  -t, --table=TABLE             仅重新打包特定表
  -I, --parent-table=TABLE      重新打包特定的父表及其继承者
  -c, --schema=SCHEMA           仅在特定schema中重新打包表
  -s, --tablespace=TBLSPC       将重新打包的表移至新的表空间
  -S, --moveidx                 将重新打包的索引也移动到 *TBLSPC*
  -o, --order-by=COLUMNS        按列排序而不是按cluster键排序
  -n, --no-order                使用 vacuum full 代替 cluster
  -N, --dry-run                 打印重新打包的内容并退出
  -j, --jobs=NUM                为每个表使用这么多的并行作业
  -i, --index=INDEX             仅移动指定索引
  -x, --only-indexes            仅移动指定表的索引
  -T, --wait-timeout=SECS       发生冲突时取消其他后端的超时
  -D, --no-kill-backend         超时时不要终止其他后端
  -Z, --no-analyze              重组后不要analyze
  -k, --no-superuser-check      跳过客户端中的超级用户检查
  -C, --exclude-extension       不要重新打包属于特定扩展的表。
      --error-on-invalid-index  发现无效索引时不重新打包。
      --apply-count             重放期间在一个事务中应用的元组数量。
      --switch-threshold        当剩下很多元组需要跟上时，切换表。

连接选项:
  -d, --dbname=DBNAME       连接的数据库
  -h, --host=HOSTNAME       数据库服务器主机或套接字目录
  -p, --port=PORT           数据库服务器端口
  -U, --username=USERNAME   连接用户名
  -w, --no-password         从不提示输入密码
  -W, --password            强制密码提示

通用选项:
  -e, --echo                回显查询
  -E, --elevel=LEVEL        设置输出消息级别
  --help                    显示此帮助，然后退出
  --version                 输出版本信息，然后退出

.. Reorg Options
  ^^^^^^^^^^^^^

重组选项
^^^^^^^^^^^^^

.. ``-a``, ``--all``
    Attempt to repack all the databases of the cluster. Databases where the
    ``pg_repack`` extension is not installed will be skipped.

``-a``, ``--all``
    尝试重新打包集群的所有数据库。未安装 ``pg_repack`` 扩展的数据库将被跳过。

.. ``-t TABLE``, ``--table=TABLE``
    Reorganize the specified table(s) only. Multiple tables may be
    reorganized by writing multiple ``-t`` switches. By default, all eligible
    tables in the target databases are reorganized.

``-t TABLE``, ``--table=TABLE``
    仅重组指定的表。通过写入多个 ``-t`` 开关可以重组多个表。默认情况下，将重组目标数据库中所有符合条件的表。

.. ``-I TABLE``, ``--parent-table=TABLE``
    Reorganize both the specified table(s) and its inheritors. Multiple
    table hierarchies may be reorganized by writing multiple ``-I`` switches.

``-I TABLE``, ``--parent-table=TABLE``
    重组指定的表及其继承者。可以通过写入多个 ``-I`` 开关来重组多个表层次结构。

.. ``-c``, ``--schema``
    Repack the tables in the specified schema(s) only. Multiple schemas may
    be repacked by writing multiple ``-c`` switches. May be used in
    conjunction with ``--tablespace`` to move tables to a different tablespace.

``-c``, ``--schema``
    仅重新打包指定模式中的表。可以通过写入多个 ``-c`` 开关来重新打包多个模式。可以与 ``--tablespace`` 结合使用，将表移动到不同的表空间。

.. ``-o COLUMNS [,...]``, ``--order-by=COLUMNS [,...]``
    Perform an online CLUSTER ordered by the specified columns.

``-o COLUMNS [,...]``, ``--order-by=COLUMNS [,...]``
    执行按指定列排序的在线 CLUSTER。

.. ``-n``, ``--no-order``
    Perform an online VACUUM FULL.  Since version 1.2 this is the default for
    non-clustered tables.

``-n``, ``--no-order``
    执行在线 VACUUM FULL。从版本 1.2 开始，这是非集群表(non-clustered)的默认设置。

.. ``-N``, ``--dry-run``
    List what would be repacked and exit.

``-N``, ``--dry-run``
    列出需要重新打包的内容并退出。

.. ``-j``, ``--jobs``
    Create the specified number of extra connections to PostgreSQL, and
    use these extra connections to parallelize the rebuild of indexes
    on each table. Parallel index builds are only supported for full-table
    repacks, not with ``--index`` or ``--only-indexes`` options. If your
    PostgreSQL server has extra cores and disk I/O available, this can be a
    useful way to speed up pg_repack.

``-j``, ``--jobs``
    创建指定数量的 PostgreSQL 额外连接，并使用这些额外连接并行重建每个表上的索引。并行索引构建仅支持全表重新打包，不支持 ``--index`` 或 ``--only-indexes`` 选项。如果您的 PostgreSQL 服务器有额外的核和可用的磁盘 I/O，这可能是加快 pg_repack 速度的有效方法。

.. ``-s TBLSPC``, ``--tablespace=TBLSPC``
    Move the repacked tables to the specified tablespace: essentially an
    online version of ``ALTER TABLE ... SET TABLESPACE``. The tables' indexes
    are left in the original tablespace unless ``--moveidx`` is specified too.

``-s TBLSPC``, ``--tablespace=TBLSPC``
    将重新打包的表移动到指定的表空间：本质上是 ``ALTER TABLE ... SET TABLESPACE`` 的在线版本。除非还指定了 ``--moveidx`` ，否则表的索引将保留在原始表空间中。

.. ``-S``, ``--moveidx``
    Also move the indexes of the repacked tables to the tablespace specified
    by the ``--tablespace`` option.

``-S``, ``--moveidx``
    还将重新打包的表的索引移动到 ``--tablespace`` 选项指定的表空间。

.. ``-i``, ``--index``
    Repack the specified index(es) only. Multiple indexes may be repacked
    by writing multiple ``-i`` switches. May be used in conjunction with
    ``--tablespace`` to move the index to a different tablespace.

``-i``, ``--index``
    仅重新打包指定的索引。可以通过写入多个 ``-i`` 开关来重新打包多个索引。可以与 ``--tablespace`` 结合使用，将索引移动到不同的表空间。

.. ``-x``, ``--only-indexes``
    Repack only the indexes of the specified table(s), which must be specified
    with the ``--table`` or ``--parent-table`` option.

``-x``, ``--only-indexes``
    仅重新打包指定表的索引，必须使用 ``--table`` 或 ``--parent-table`` 选项指定。

.. ``-T SECS``, ``--wait-timeout=SECS``
    pg_repack needs to take one exclusive lock at the beginning as well as one
    exclusive lock at the end of the repacking process. This setting controls
    how many seconds pg_repack will wait to acquire this lock. If the lock
    cannot be taken after this duration and ``--no-kill-backend`` option is
    not specified, pg_repack will forcibly cancel the conflicting queries.
    If you are using PostgreSQL version 8.4 or newer, pg_repack will fall
    back to using pg_terminate_backend() to disconnect any remaining
    backends after twice this timeout has passed.
    The default is 60 seconds.

``-T SECS``, ``--wait-timeout=SECS``
    pg_repack 需要在重新打包过程开始时获取一个排他锁，并在结束时获取一个排他锁。此设置控制 pg_repack 将等待多少秒来获取此锁。如果在此持续时间后无法获取锁并且未指定 ``--no-kill-backend`` 选项，则 pg_repack 将强制取消冲突的查询。如果您使用的是 PostgreSQL 版本 8.4 或更新版本，则 pg_repack 将在两次此超时后恢复使用 pg_terminate_backend() 断开任何剩余的后端。默认值为 60 秒。

..  ``-D``, ``--no-kill-backend``
    Skip to repack table if the lock cannot be taken for duration specified
    ``--wait-timeout``, instead of cancelling conflicting queries. The default
    is false.

``-D``, ``--no-kill-backend``
    如果在指定的 ``--wait-timeout`` 时间内无法锁定，则跳过重新打包表，而不是取消冲突的查询。默认值为 false。

.. ``-Z``, ``--no-analyze``
    Disable ANALYZE after a full-table reorganization. If not specified, run
    ANALYZE after the reorganization.

``-Z``, ``--no-analyze``
    全表重组后禁用 ANALYZE。如果未指定，则在重组后运行 ANALYZE。

.. ``-k``, ``--no-superuser-check``
    Skip the superuser checks in the client. This setting is useful for using
    pg_repack on platforms that support running it as non-superusers.

``-k``, ``--no-superuser-check``
    跳过客户端中的超级用户检查。此设置对于在支持以非超级用户身份运行 pg_repack 的平台上使用 pg_repack 很有用。

.. ``-C``, ``--exclude-extension``
    Skip tables that belong to the specified extension(s). Some extensions
    may heavily depend on such tables at planning time etc.

``-C``, ``--exclude-extension``
    跳过属于指定扩展的表。某些扩展在规划时可能严重依赖此类表。

.. ``--switch-threshold``
    Switch tables when that many tuples are left in log table.
    This setting can be used to avoid the inability to catchup with write-heavy tables.

``--switch-threshold``
    当日志表中剩下那么多元组时切换表。此设置可用于避免无法追上写入繁忙的表。

.. Connection Options
   ^^^^^^^^^^^^^^^^^^
  Options to connect to servers. You cannot use ``--all`` and ``--dbname`` or
  ``--table`` or ``--parent-table`` together.

连接选项
^^^^^^^^^^^^^^^^^^

连接服务器的选项。您不能同时使用 ``--all`` 和 ``--dbname`` 或 ``--table`` 或 ``--parent-table`` 。

.. ``-a``, ``--all``
    Reorganize all databases.

``-a``, ``--all``
    重新组织所有数据库。

.. ``-d DBNAME``, ``--dbname=DBNAME``
    Specifies the name of the database to be reorganized. If this is not
    specified and ``-a`` (or ``--all``) is not used, the database name is read
    from the environment variable PGDATABASE. If that is not set, the user
    name specified for the connection is used.

``-d DBNAME``, ``--dbname=DBNAME``
    指定要重组的数据库的名称。如果未指定，并且未使用 ``-a`` （或 ``--all`` ），则从环境变量 PGDATABASE 读取数据库名称。如果未设置，则使用为连接指定的用户名。

.. ``-h HOSTNAME``, ``--host=HOSTNAME``
    Specifies the host name of the machine on which the server is running. If
    the value begins with a slash, it is used as the directory for the Unix
    domain socket.

``-h HOSTNAME``, ``--host=HOSTNAME``
    指定运行服务器的计算机的主机名。如果该值以斜杠 ``/`` 开头，则将其用作 Unix 域套接字的目录。

.. ``-p PORT``, ``--port=PORT``
    Specifies the TCP port or local Unix domain socket file extension on which
    the server is listening for connections.

``-p PORT``, ``--port=PORT``
    指定服务器正在监听连接的 TCP 端口或本地 Unix 域套接字文件扩展。

.. ``-U USERNAME``, ``--username=USERNAME``
    User name to connect as.

``-U USERNAME``, ``--username=USERNAME``
    连接的用户名。

.. ``-w``, ``--no-password``
    Never issue a password prompt. If the server requires password
    authentication and a password is not available by other means such as a
    ``.pgpass`` file, the connection attempt will fail. This option can be
    useful in batch jobs and scripts where no user is present to enter a
    password.

``-w``, ``--no-password``
    永远不要发出密码提示。如果服务器需要密码验证，而其他方式（例如 ``.pgpass`` 文件）无法提供密码，则连接尝试将失败。此选项在没有用户输入密码的批处理作业和脚本中很有用。

.. ``-W``, ``--password``
    Force the program to prompt for a password before connecting to a
    database.
  
    This option is never essential, since the program will automatically
    prompt for a password if the server demands password authentication.
    However, pg_repack will waste a connection attempt finding out that the
    server wants a password. In some cases it is worth typing ``-W`` to avoid
    the extra connection attempt.

``-W``, ``--password``
    强制程序在连接到数据库之前提示输入密码。

    此选项从来都不是必需的，因为如果服务器要求密码验证，程序将自动提示输入密码。但是，pg_repack 将浪费一次连接尝试来发现服务器需要密码。在某些情况下，值得输入 ``-W`` 以避免额外的连接尝试。


.. Generic Options
   ^^^^^^^^^^^^^^^

通用选项
^^^^^^^^^^^^^^^

.. ``-e``, ``--echo``
    Echo commands sent to server.

``-e``, ``--echo``
    回显发送至服务器的命令。

.. ``-E LEVEL``, ``--elevel=LEVEL``
    Choose the output message level from ``DEBUG``, ``INFO``, ``NOTICE``,
    ``WARNING``, ``ERROR``, ``LOG``, ``FATAL``, and ``PANIC``. The default is
    ``INFO``.

``-E LEVEL``, ``--elevel=LEVEL``
    从 ``DEBUG``、 ``INFO``、 ``NOTICE``、 ``WARNING``、 ``ERROR``、 ``LOG``、 ``FATAL``和 ``PANIC`` 中选择输出消息级别。默认值为 ``INFO``。

.. ``--help``
    Show usage of the program.

``--help``
    显示程序的使用方法。

.. ``--version``
    Show the version number of the program.

``--version``
    显示程序的版本号。

.. Environment
  -----------
  
  ``PGDATABASE``, ``PGHOST``, ``PGPORT``, ``PGUSER``
      Default connection parameters
  
      This utility, like most other PostgreSQL utilities, also uses the
      environment variables supported by libpq (see `Environment Variables`__).
  
      .. __: http://www.postgresql.org/docs/current/static/libpq-envars.html

环境
---------

``PGDATABASE``, ``PGHOST``, ``PGPORT``, ``PGUSER``
    默认连接参数

    与大多数其他 PostgreSQL 实用程序一样，此实用程序也使用 libpq 支持的环境变量（参见 `Environment Variables`__）。

    .. __: http://www.postgresql.org/docs/current/static/libpq-envars.html

.. Examples
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

示例
-------

对数据库 ``test`` 中的所有聚集表(the clustered tables)执行在线CLUSTER，并对所有非聚集表执行在线VACUUM FULL::

    $ pg_repack test

对数据库 ``test`` 中的表 ``foo`` 和 ``bar`` 执行在线 VACUUM FULL（最终的群集索引将被忽略）::

    $ pg_repack --no-order --table foo --table bar test

将表 ``foo`` 的所有索引移动到表空间 ``tbs`` ::

    $ pg_repack -d test --table foo --only-indexes --tablespace tbs

将指定索引移动到表空间 ``tbs``::

    $ pg_repack -d test --index idx --tablespace tbs

.. Diagnostics
   -----------

诊断
----------------------

.. Error messages are reported when pg_repack fails. The following list shows the
  cause of errors.
  
  You need to cleanup by hand after fatal errors. To cleanup, just remove
  pg_repack from the database and install it again: for PostgreSQL 9.1 and
  following execute ``DROP EXTENSION pg_repack CASCADE`` in the database where
  the error occurred, followed by ``CREATE EXTENSION pg_repack``; for previous
  version load the script ``$SHAREDIR/contrib/uninstall_pg_repack.sql`` into the
  database where the error occured and then load
  ``$SHAREDIR/contrib/pg_repack.sql`` again.

pg_repack 失败时会报告错误消息。以下列表显示了错误的原因。

出现致命错误后，您需要手动清理。
要清理，只需从数据库中删除 pg_repack 并重新安装：
对于 PostgreSQL 9.1，然后在发生错误的数据库中执行 ``DROP EXTENSION pg_repack CASCADE`` ，然后执行 ``CREATE EXTENSION pg_repack``；
对于以前的版本，将脚本 ``$SHAREDIR/contrib/uninstall_pg_repack.sql`` 加载到发生错误的数据库中，然后再次加载 ``$SHAREDIR/contrib/pg_repack.sql`` 。


.. INFO: database "db" skipped: pg_repack VER is not installed in the database
    pg_repack is not installed in the database when the ``--all`` option is
    specified.
   
    Create the pg_repack extension in the database.

.. class:: diag

INFO: database "db" skipped: pg_repack VER is not installed in the database
    当指定 ``--all`` 选项时，对于未安装 pg_repack 的数据库显示。

    在数据库中创建 pg_repack 扩展。

.. ERROR: pg_repack VER is not installed in the database
    pg_repack is not installed in the database specified by ``--dbname``.
  
    Create the pg_repack extension in the database.

.. class:: diag

ERROR: pg_repack VER is not installed in the database
    pg_repack 未安装在 ``--dbname`` 指定的数据库中。

    在数据库中创建 pg_repack 扩展。

.. ERROR: program 'pg_repack V1' does not match database library 'pg_repack V2'
    There is a mismatch between the ``pg_repack`` binary and the database
    library (``.so`` or ``.dll``).
  
    The mismatch could be due to the wrong binary in the ``$PATH`` or the
    wrong database being addressed. Check the program directory and the
    database; if they are what expected you may need to repeat pg_repack
    installation.

.. class:: diag

ERROR: program 'pg_repack V1' does not match database library 'pg_repack V2'
     ``pg_repack`` 二进制文件和数据库库文件(``.so`` 或 ``.dll``)不匹配。

    不匹配可能是由于 ``$PATH`` 中的二进制文件错误或寻址的数据库错误造成的。
    请检查程序目录和数据库；如果它们符合预期，则可能需要重复安装 pg_repack。

.. ERROR: extension 'pg_repack V1' required, found extension 'pg_repack V2'
    The SQL extension found in the database does not match the version
    required by the pg_repack program.
  
    You should drop the extension from the database and reload it as described
    in the installation_ section.

.. class:: diag

ERROR: extension 'pg_repack V1' required, found extension 'pg_repack V2'
    数据库中发现的 SQL 扩展与 pg_repack 程序所需的版本不匹配。

    您应该从数据库中删除扩展，然后按照 `安装`_ 部分中的说明重新加载它。

.. ERROR: relation "table" must have a primary key or not-null unique keys
    The target table doesn't have a PRIMARY KEY or any UNIQUE constraints
    defined.
  
    Define a PRIMARY KEY or a UNIQUE constraint on the table.

.. class:: diag

ERROR: relation "table" must have a primary key or not-null unique keys
    目标表未定义 PRIMARY KEY 或任何 UNIQUE 约束。

    在表上定义 PRIMARY KEY 或 UNIQUE 约束。

.. ERROR: query failed: ERROR: column "col" does not exist
    The target table doesn't have columns specified by ``--order-by`` option.
  
    Specify existing columns.

.. class:: diag

ERROR: query failed: ERROR: column "col" does not exist
    目标表没有 ``--order-by`` 选项指定的列。

    指定现有列。

.. WARNING: the table "tbl" already has a trigger called a_repack_trigger
    The trigger was probably installed during a previous attempt to run
    pg_repack on the table which was interrupted and for some reason failed
    to clean up the temporary objects.
  
    You can remove all the temporary objects by dropping and re-creating the
    extension: see the installation_ section for the details.

.. class:: diag

WARNING: the table "tbl" already has a trigger called repack_trigger
    触发器可能是在之前尝试在表上运行 pg_repack 时安装的，该尝试被中断并且由于某种原因未能清理临时对象。

    您可以通过删除并重新创建扩展来删除所有临时对象：有关详细信息，请参阅 `安装`_ 部分。

.. ERROR: Another pg_repack command may be running on the table. Please try again later.
    There is a chance of deadlock when two concurrent pg_repack commands are
    run on the same table. So, try to run the command after some time.

.. class:: diag

ERROR: Another pg_repack command may be running on the table. Please try again later.
    当两个并发的 pg_repack 命令在同一张表上运行的时候，可能会出现死锁。因此，请尝试过一段时间再运行该命令。

.. WARNING: Cannot create index  "schema"."index_xxxxx", already exists
  DETAIL: An invalid index may have been left behind by a previous pg_repack on
  the table which was interrupted. Please use DROP INDEX "schema"."index_xxxxx"
  to remove this index and try again.
  
   A temporary index apparently created by pg_repack has been left behind, and
   we do not want to risk dropping this index ourselves. If the index was in
   fact created by an old pg_repack job which didn't get cleaned up, you
   should just use DROP INDEX and try the repack command again.

.. class:: diag

WARNING: Cannot create index  "schema"."index_xxxxx", already exists
    详细信息：上一次中断的 pg_repack 操作可能遗留了无效索引。请使用 DROP INDEX "schema"."index_xxxxx" 删除此索引，然后重试。

    一个临时索引显然是由 pg_repack 创建的，我们不想冒险删除这个索引。
    如果该索引实际上是由未清理的旧 pg_repack 作业创建的，则应使用 DROP INDEX 并再次尝试 repack 命令。
    

.. Restrictions
  ------------
  
  pg_repack comes with the following restrictions.

限制
-----

pg_repack 有以下限制。

.. Temp tables
  ^^^^^^^^^^^
  
  pg_repack cannot reorganize temp tables.

临时表
^^^^^^^^^^^^

pg_repack 无法重新组织临时表。

.. GiST indexes
  ^^^^^^^^^^^^
  
  pg_repack cannot reorganize tables using GiST indexes.

GiST 索引
^^^^^^^^^^^^^^^^

pg_repack 不能通过 GiST 索引来聚集表。

.. DDL commands
  ^^^^^^^^^^^^
  
  You will not be able to perform DDL commands of the target table(s) **except**
  VACUUM or ANALYZE while pg_repack is working. pg_repack will hold an
  ACCESS SHARE lock on the target table during a full-table repack, to enforce
  this restriction.
  
  If you are using version 1.1.8 or earlier, you must not attempt to perform any
  DDL commands on the target table(s) while pg_repack is running. In many cases
  pg_repack would fail and rollback correctly, but there were some cases in these
  earlier versions which could result in data corruption.

DDL命令
^^^^^^^^^^^^

当 pg_repack 正在运行时，您将无法对目标表执行 DDL 命令(VACUUM 或 ANALYZE 除外)。pg_repack 将在完整表重新打包期间对目标表保持 ACCESS SHARE 锁，以强制执行此限制。

如果您使用的是 1.1.8 或更早版本，则在 pg_repack 运行时，您不得尝试对目标表执行任何 DDL 命令。在许多情况下，pg_repack 会失败并正确回滚，但这些早期版本中的某些情况可能会导致数据损坏。

.. Details
  -------

细节
---------

.. Full Table Repacks
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

全表重组
^^^^^^^^^^^^^^^

要执行全表重新打包，pg_repack 将：

1. 创建日志表来记录对原始表所做的更改
2. 在原始表上添加触发器，将 INSERT、UPDATE 和 DELETE 记录到我们的日志表中
3. 创建一个包含旧表所有行的新表
4. 在这个新表上建立索引
5. 将日志表中累积的所有更改应用到新表
6. 使用系统目录交换表，包括索引和 toast 表
7. 删除原始表

pg_repack 仅在初始设置(上述步骤 1 和 2)和最终交换和删除阶段(步骤 6 和 7)期间短暂持有 ACCESS EXCLUSIVE 锁。
在其余时间，pg_repack 只需在原始表上持有 ACCESS SHARE 锁，这意味着 INSERT、UPDATE 和 DELETE 可以照常进行。

.. Index Only Repacks
  ^^^^^^^^^^^^^^^^^^
  
  To perform an index-only repack, pg_repack will:
  
  1. create new indexes on the table using CONCURRENTLY matching the definitions of the old indexes
  2. swap out the old for the new indexes in the catalogs
  3. drop the old indexes
  
  Creating indexes concurrently comes with a few caveats, please see `the documentation`__ for details.
  
      .. __: http://www.postgresql.org/docs/current/static/sql-createindex.html#SQL-CREATEINDEX-CONCURRENTLY

仅索引重新打包
^^^^^^^^^^^^^^^^^^^^^^^^^

要执行仅索引重新打包，pg_repack 将：

1. 使用 CONCURRENTLY 匹配旧索引的定义在表上创建新索引
2. 在catalogs中将旧索引替换为新索引
3. 删除旧索引

并发创建索引有一些注意事项，请参阅 `the documentation`__ 了解详情。

    .. __: http://www.postgresql.org/docs/current/static/sql-createindex.html#SQL-CREATEINDEX-CONCURRENTLY


.. Releases
  --------

发行
---------------


.. * pg_repack 1.5.1
  * Added support for PostgreSQL 17
  * Fix wrong OID format type in repack_trigger (issue #380)
  * Fix check of NOT NULL by repack.primary_keys (issue #282)
  * Fixed processing of tablespace names requiring quoted identifiers (issue #386)
  * Replace ``PQconnectdb()`` by ``PQconnectdbParams()`` (issue #382)
  * Added ``--apply-count`` option (issue #392)
  * Do not include a declaratively partitioned table with option ``--only-indexes`` (issue #389)
  * Fix possible two vacuums concurrently processing the same relfilenode (issue #399)
  * Use savepoints when retrying to take AccessShareLock (issue #383)
  * Fix swap of relfrozenxid, relfrozenxid and relallvisible (issue #377, #157)

* pg_repack 1.5.1

  * 增加了对 PostgreSQL 17 的支持
  * 修复 repack_trigger 中错误的 OID 格式类型 (issue #380)
  * 修复 repack.primary_keys 对 NOT NULL 的检查 (issue #282)
  * 修复需要带引号标识符的表空间名称的处理 (issue #386)
  * 将 ``PQconnectdb()`` 替换为 ``PQconnectdbParams()`` (issue #382)
  * 添加了 ``--apply-count`` 选项 (issue #392)
  * 不要使用选项 ``--only-indexes`` 包含声明式分区表 (issue #389)
  * 修复可能同时处理同一 relfilenode 的两个 vacuum (issue #399)
  * 重试获取 AccessShareLock 时使用保存点 (issue #383)
  * 修复 relfrozenxid、relfrozenxid 和 relallvisible 的交换 (issue #377, #157)

.. * pg_repack 1.5.0
  * Added support for PostgreSQL 16
  * Fix possible SQL injection (issue #368)
  * Support longer password length (issue #357)
  * Fixed infinite loop on empty password (issue #354)
  * Added ``--switch-threshold`` option (issue #347)
  * Fixed crash in ``get_order_by()`` using invalid relations (issue #321)
  * Added support for tables that have been previously rewritten with `VACUUM FULL` and use storage=plain for all columns (issue #313)
  * More careful locks acquisition (issue #298)

* pg_repack 1.5.0

  * 增加了对 PostgreSQL 16 的支持
  * 修复可能的 SQL 注入 (issue #368)
  * 支持更长的密码长度 (issue #357)
  * 修复了空密码的无限循环 (issue #354)
  * 增加了 ``--switch-threshold`` 选项 (issue #347)
  * 修复了使用无效relations导致 ``get_order_by()`` 崩溃的问题 (issue #321)
  * 增加了对之前使用 `VACUUM FULL` 重写和对所有列使用 storage=plain 的表的支持 (issue #313)
  * 更谨慎地获取锁 (issue #298)

.. * pg_repack 1.4.8
  * Added support for PostgreSQL 15
  * Fixed --parent-table on declarative partitioned tables (issue #288)
  * Removed connection info from error log (issue #285)

* pg_repack 1.4.8

  * 增加了对 PostgreSQL 15 的支持
  * 修复了声明式分区表(declarative partitioned tables)上的 --parent-table (issue #288)
  * 从错误日志中删除了连接信息 (issue #285)

.. * pg_repack 1.4.7
  * Added support for PostgreSQL 14

* pg_repack 1.4.7

  * 增加了对 PostgreSQL 14 的支持

.. * pg_repack 1.4.6
  * Added support for PostgreSQL 13
  * Dropped support for PostgreSQL before 9.4

* pg_repack 1.4.6

  * 增加了对 PostgreSQL 13 的支持
  * 放弃了对PostgreSQL 9.4之前版本的支持

.. * pg_repack 1.4.5
  * Added support for PostgreSQL 12
  * Fixed parallel processing for indexes with operators from public schema

* pg_repack 1.4.5

  * 增加了对 PostgreSQL 12 的支持
  * 修复了使用public schema中的运算符进行索引的并行处理

.. * pg_repack 1.4.4
  * Added support for PostgreSQL 11 (issue #181)
  * Remove duplicate password prompt (issue #184)

* pg_repack 1.4.4

  * 增加了对 PostgreSQL 11 的支持 (issue #181)
  * 删除重复密码提示 (issue #184)

.. * pg_repack 1.4.3
..  * Fixed possible CVE-2018-1058 attack paths (issue #168)
..  * Fixed "unexpected index definition" after CVE-2018-1058 changes in
..    PostgreSQL (issue #169)
..  * Fixed build with recent Ubuntu packages (issue #179)

* pg_repack 1.4.3

  * 修复可能的 CVE-2018-1058 攻击路径 (issue #168)
  * 修复了 PostgreSQL 中 CVE-2018-1058 更改后的"unexpected index definition"问题 (issue #169)
  * 使用最近的 Ubuntu 软件包修复了构建问题 (issue #179)

.. * pg_repack 1.4.2
..  * added PostgreSQL 10 support (issue #120)
..  * fixed error DROP INDEX CONCURRENTLY cannot run inside a transaction block (issue #129)

* pg_repack 1.4.2

  * 增加了对 PostgreSQL 10 的支持 (issue #120)
  * 修复错误 DROP INDEX CONCURRENTLY 无法在事务块内运行 (issue #129)

.. * pg_repack 1.4.1
..   * fixed broken ``--order-by`` option (issue #138)

* pg_repack 1.4.1

  * 修复损坏的 ``--order-by`` 选项 (issue #138)

.. * pg_repack 1.4
..   * added support for PostgreSQL 9.6, dropped support for versions before 9.1
..   * use ``AFTER`` trigger to solve concurrency problems with ``INSERT
..     CONFLICT`` (issue #106)
..   * added ``--no-kill-backend`` option (issue #108)
..   * added ``--no-superuser-check`` option (issue #114)
..   * added ``--exclude-extension`` option (#97)
..   * added ``--parent-table`` option (#117)
..   * restore TOAST storage parameters on repacked tables (issue #10)
..   * restore columns storage types in repacked tables (issue #94)

* pg_repack 1.4

  * 增加了对 PostgreSQL 9.6 的支持，放弃了对 9.1 之前版本的支持
  * 使用 ``AFTER`` 触发器解决 ``INSERT CONFLICT`` 的并发问题 (issue #106)
  * 添加了 ``--no-kill-backend`` 选项 (issue #108)
  * 添加了 ``--no-superuser-check`` 选项 (issue #114)
  * 添加了 ``--exclude-extension`` 选项 (#97)
  * 添加了 ``--parent-table`` 选项 (#117)
  * 恢复重新打包的表上的 TOAST 存储参数 (issue #10)
  * 恢复重新打包的表中的列存储类型 (issue #94)

.. * pg_repack 1.3.4
..  * grab exclusive lock before dropping original table (#81)
..  * do not attempt to repack unlogged table (#71)

* pg_repack 1.3.4

  * 在删除原始表之前获取独占锁 (issue #81)
  * 不要尝试重新打包unlogged表 (issue #71)

.. * pg_repack 1.3.3
..  * Added support for PostgreSQL 9.5
..  * Fixed possible deadlock when pg_repack command is interrupted (issue #55)
..  * Fixed exit code for when pg_repack is invoked with ``--help`` and
..    ``--version``
..  * Added Japanese language user manual

* pg_repack 1.3.3

  * 增加了对 PostgreSQL 9.5 的支持
  * 修复 pg_repack 命令中断时可能出现的死锁问题 (issue #55)
  * 修复了使用 ``--help`` 和 ``--version`` 调用 pg_repack 时的退出代码
  * 添加了日语用户手册

.. * pg_repack 1.3.2
..  * Fixed to clean up temporary objects when pg_repack command is interrupted.
..  * Fixed possible crash when pg_repack shared library is loaded a alongside
..    pg_statsinfo (issue #43)

* pg_repack 1.3.2

  * 已修复当 pg_repack 命令中断时清理临时对象的问题。
  * 修复了 pg_repack 共享库与 pg_statsinfo 一起加载时可能发生的崩溃 (issue #43)

.. * pg_repack 1.3.1
..  * Added support for PostgreSQL 9.4.

* pg_repack 1.3.1

  * 增加了对 PostgreSQL 9.4 的支持


.. * pg_repack 1.3
..  * Added ``--schema`` to repack only the specified schema (issue #20).
..  * Added ``--dry-run`` to do a dry run (issue #21).
..  * Fixed advisory locking for >2B OID values (issue #30).
..  * Avoid possible deadlock when other sessions lock a to-be-repacked
    table (issue #32).
..  * Performance improvement for performing sql_pop DELETEs many-at-a-time.
..  * Attempt to avoid pg_repack taking forever when dealing with a
    constant heavy stream of changes to a table.

* pg_repack 1.3

  * 添加 ``--schema`` 以仅重新打包指定的schema ( issue #20)
  * 添加 ``--dry-run`` 进行试运行 (issue #21)
  * 修复了 >2B OID 值的咨询锁定 (issue #30)
  * 避免当其他会话锁定要重新打包的表时可能出现的死锁 (issue #32) 
  * 一次执行多个 sql_pop DELETE 的性能改进
  * 在处理表的持续大量更改时，尝试避免 pg_repack 花费很长时间

.. * pg_repack 1.2
  
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

* pg_repack 1.2

  * 支持 PostgreSQL 9.3
  * 添加了 ``--tablespace`` 和 ``--moveidx`` 选项来执行在线 SET TABLESPACE。
  * 添加 ``--index`` 以仅重新打包指定索引。
  * 添加了 ``--only-indexes`` ，仅重新打包指定表的索引
  * 为并行操作添加了 ``--jobs`` 选项。
  * 不需要 ``--no-order`` 对非集群表执行 VACUUM FULL (pg_repack issue #6) 
  * 不要等待其他数据库持有的锁 (pg_repack issue #11)
  * 错误修复：正确处理具有 DESC、NULL FIRST/LAST、COLLATE 等选项的关键索引 (pg_repack issue #3)
  * 修复删除时数据损坏的错误 (pg_repack issue #23)
  * 更多有用的程序输出和错误消息。

.. * pg_repack 1.1.8
  
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

* pg_repack 1.1.8

  * 增加了对 PostgreSQL 9.2 的支持。
  * 增加了对 PostgreSQL 9.1 及更高版本中的 CREATE EXTENSION 的支持。
  * 在等待事务完成时向用户提供反馈 (pg_reorg issue #5)
  * 错误修复：允许在新提升的流复制从属上运行 (pg_reorg issue #1)
  * 错误修复：修复 pg_repack 和 Slony 2.0/2.1 之间的交互 (pg_reorg issue #4)
  * 错误修复：正确转义列名 (pg_reorg issue #6)
  * 错误修复：避免重新创建无效索引或选择它们作为键 (pg_reorg issue #9)
  * 错误修复：永远不要选择部分索引作为主键 (pg_reorg issue #22)

.. * pg_reorg 1.1.7 (2011-08-07)
  
  * Bugfix: VIEWs and FUNCTIONs could be corrupted that used a reorganized
    table which has a dropped column.
  * Supports PostgreSQL 9.1 and 9.2dev. (but EXTENSION is not yet)

* pg_reorg 1.1.7 (2011-08-07)

  * 错误修复：使用包含删除列的重组表的视图和功能可能会被损坏。
  * 支持 PostgreSQL 9.1 和 9.2dev。(但 EXTENSION 尚不支持)

.. See Also
   --------

参见
--------

* `clusterdb <http://www.postgresql.org/docs/current/static/app-clusterdb.html>`__
* `vacuumdb <http://www.postgresql.org/docs/current/static/app-vacuumdb.html>`__
