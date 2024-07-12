pg_repack -- 使用最小锁定重组 PostgreSQL 数据库中的表
=====================================================

.. contents::
    :depth: 1
    :backlinks: none

pg_repack_ 是一个 PostgreSQL 扩展，允许您从表和索引中移除碎片，并且可以选择恢复聚簇索引的物理顺序。与 CLUSTER_ 和 `VACUUM FULL`_ 不同的是，它可以在线工作，处理过程中不会在处理的表上持有独占锁。
pg_repack 运行效率高，性能与直接使用 CLUSTER 相当。

pg_repack 是前 pg_reorg_ 项目的一个分支。请访问 `项目页面`_ 获取错误报告和开发信息。

您可以选择以下一种方法进行重组：

* 在线 CLUSTER（按聚簇索引排序）
* 按指定列排序
* 在线 VACUUM FULL（仅压缩行）
* 重建或迁移表的索引

注意：

* 只有超级用户可以使用此工具。
* 目标表必须具有主键，或者至少在一个非空列上具有唯一索引。

.. _pg_repack: https://reorg.github.io/pg_repack
.. _CLUSTER: http://www.postgresql.org/docs/current/static/sql-cluster.html
.. _VACUUM FULL: VACUUM_
.. _VACUUM: http://www.postgresql.org/docs/current/static/sql-vacuum.html
.. _项目页面: https://github.com/reorg/pg_repack
.. _pg_reorg: https://github.com/reorg/pg_reorg


要求
----

PostgreSQL 版本
    PostgreSQL 9.5, 9.6, 10, 11, 12, 13, 14, 15, 16, 17。

    不支持 PostgreSQL 9.4 及之前的版本。

磁盘
    执行完整表重组需要的空闲磁盘空间大约是目标表和其索引大小的两倍。例如，如果要重组的表和索引总大小为 1GB，则需要额外的 2GB 磁盘空间。

下载
----

您可以从 PGXN 网站 `下载 pg_repack`__ 。解压缩归档文件并按照安装_说明操作。

.. __: http://pgxn.org/dist/pg_repack/

或者您可以使用 `PGXN 客户端`_ 下载、编译和安装软件包；使用以下命令::

    $ pgxn install pg_repack

查看 `pgxn 安装文档`__ 了解可用选项。

.. _PGXN 客户端: https://pgxn.github.io/pgxnclient/
.. __: https://pgxn.github.io/pgxnclient/usage.html#pgxn-install


安装
----

在 UNIX 或 Linux 上可以使用 ``make`` 构建 pg_repack。PGXS 构建框架会自动使用。在构建之前，您可能需要安装 PostgreSQL 开发包（``postgresql-devel`` 等）并将包含 ``pg_config`` 的目录添加到您的 ``$PATH`` 中。然后可以运行::

    $ cd pg_repack
    $ make
    $ sudo make install

您也可以在 Windows 上使用 Microsoft Visual C++ 2010 构建程序。``msvc`` 文件夹中包含项目文件。

安装后，在要处理的数据库中加载 pg_repack 扩展。pg_repack 被打包为扩展，因此您可以执行::

    $ psql -c "CREATE EXTENSION pg_repack" -d your_database

您可以使用 ``DROP EXTENSION pg_repack`` 删除 pg_repack，或者仅删除 ``repack`` 模式。

如果您正在从旧版本的 pg_repack 或 pg_reorg 升级，请按上述说明从数据库中删除旧版本，并安装新版本。


用法
----

::

    pg_repack [选项]... [数据库名]

可以在 ``OPTIONS`` 中指定以下选项。

选项：
  -a, --all                     重组所有数据库
  -t, --table=TABLE             仅重组特定表
  -I, --parent-table=TABLE      重组特定父表及其继承者
  -c, --schema=SCHEMA           仅重组特定模式中的表
  -s, --tablespace=TBLSPC       将重组后的表移动到新的表空间
  -S, --moveidx                 将重组后的索引也移动到 *TBLSPC*
  -o, --order-by=COLUMNS        按列排序，而不是按聚簇键
  -n, --no-order                执行在线 VACUUM FULL，而不是聚簇
  -N, --dry-run                 列出即将重组的内容并退出
  -j, --jobs=NUM                对每个表使用多少个并行作业
  -i, --index=INDEX             仅重组指定索引
  -x, --only-indexes            仅重组指定表的索引
  -T, --wait-timeout=SECS       超时取消冲突的后端
  -D, --no-kill-backend         当超时时，不要杀死其他后端
  -Z, --no-analyze              完成全表重组后禁用 ANALYZE
  -k, --no-superuser-check      在客户端中跳过超级用户检查
  -C, --exclude-extension       不重组属于特定扩展的表
      --error-on-invalid-index  发现无效索引时不重组
      --apply-count             回放期间每个事务应用的元组数
      --switch-threshold        当剩余这么多元组要追上时切换表

连接选项：
  -d, --dbname=DBNAME           要连接的数据库
  -h, --host=HOSTNAME           数据库服务器主机或套接字目录
  -p, --port=PORT               数据库服务器端口
  -U, --username=USERNAME       要连接的用户名
  -w, --no-password             从不提示密码
  -W, --password                强制提示密码

通用选项：
  -e, --echo                    回显发送到服务器的命令
  -E, --elevel=LEVEL            设置输出消息级别
  --help                        显示此帮助信息并退出
  --version                     输出版本信息并退出


重组选项
^^^^^^^^

``-a``, ``--all``
    尝试重组群集中的所有数据库。未安装 ``pg_repack`` 扩展的数据库将被跳过。

``-t TABLE``, ``--table=TABLE``
    仅重组指定的表。可以通过多次写入 ``-t`` 开关来重组多个表。默认情况下，目标数据库中的所有符合条件的表都将被重组。

``-I TABLE``, ``--parent-table=TABLE``
    同时重组指定的表及其继承者。可以通过多次写入 ``-I`` 开关来重组多个表层级。

``-c``, ``--schema``
    仅重组指定模式中的表。可以通过多次写入 ``-c`` 开关来重组多个模式。可以与 ``--tablespace`` 一起使用，将表移动到不同的表空间。

``-o COLUMNS [,...]``, ``--order-by=COLUMNS [,...]``
    执行按指定列排序的在线 CLUSTER。

``-n``, ``--no-order``
    执行在线 VACUUM FULL。从版本 1.2 开始，这是非聚簇表的默认选项。

``-N``, ``--dry-run``
    列出即将重组的内容并退出。

``-j``, ``--jobs``
    创建指定数量的额外连接到 PostgreSQL，并使用这些额外连接来并行重建每个表的索引。并行索引重建仅支持全表重组，不支持 ``--index`` 或 ``--only-indexes`` 选项。如果您的 PostgreSQL 服务器有额外的核心和磁盘 I/O 可用，这是加快 pg_repack 运行速度的有效方法。

``-s TBLSPC``, ``--tablespace=TBLSPC``
    将重组后的表移动到指定的表空间：本质上是 ``ALTER TABLE ... SET TABLESPACE`` 的在线版本。表的索引仍保留在原始表空间，除非也指定了 ``--moveidx``。

``-S``, ``--moveidx``
    同时将重组后的表的索引移动到 ``--tablespace`` 选项指定的表空间。

``-i``, ``--index``
    仅重组指定的索引(es)。可以通过使用多个 ``-i`` 开关重组多个索引。可与 ``--tablespace`` 一起使用，将索引移动到不同的表空间。

``-x``, ``--only-indexes``
    仅重组指定表(s)的索引，这些表必须在 ``--table`` 或 ``--parent-table`` 选项中指定。

``-T SECS``, ``--wait-timeout=SECS``
    pg_repack 在重新组织过程开始时需要获取一个独占锁，以及在结束时获取另一个独占锁。此设置控制 pg_repack 将等待多少秒来获取此锁。如果在此持续时间后无法获取锁，并且未指定 ``--no-kill-backend`` 选项，pg_repack 将强制取消冲突的查询。如果您使用的是 PostgreSQL 版本 8.4 或更新版本，pg_repack 将在两倍超时后使用 pg_terminate_backend() 断开任何剩余的后端连接。默认值为 60 秒。

``-D``, ``--no-kill-backend``
    如果无法在指定的 ``--wait-timeout`` 时间内获取锁，则跳过重组表的操作，而不是取消冲突的查询。默认为 false。

``-Z``, ``--no-analyze``
    在进行全表重组后禁用 ANALYZE。如果未指定此选项，则在重组后运行 ANALYZE。

``-k``, ``--no-superuser-check``
    跳过客户端中的超级用户检查。此设置适用于支持以非超级用户身份运行 pg_repack 的平台。

``-C``, ``--exclude-extension``
    跳过属于指定扩展的表。某些扩展在计划时等方面可能严重依赖这些表。

``--switch-threshold``
    当剩余日志表中的元组数量达到此阈值时切换表。此设置可用于避免无法赶上写入密集型表的情况。


连接选项
^^^^^^^^

用于连接服务器的选项。不能同时使用 ``--all`` 和 ``--dbname`` 或 ``--table`` 或 ``--parent-table``。

``-a``, ``--all``
    重组所有数据库。

``-d DBNAME``, ``--dbname=DBNAME``
    指定要重组的数据库的名称。如果未指定此选项且未使用 ``-a``（或 ``--all``），则从环境变量 PGDATABASE 中读取数据库名称。如果未设置该变量，则使用连接时指定的用户名。

``-h HOSTNAME``, ``--host=HOSTNAME``
    指定运行服务器的机器的主机名。如果值以斜杠开头，则用作 Unix 域套接字的目录。

``-p PORT``, ``--port=PORT``
    指定服务器用于侦听连接的 TCP 端口或本地 Unix 域套接字文件扩展。

``-U USERNAME``, ``--username=USERNAME``
    要连接的用户名。

``-w``, ``--no-password``
    永远不要发出密码提示。如果服务器需要密码验证，而且没有其他途径（如 ``.pgpass`` 文件）可用密码，则连接尝试将失败。此选项在批处理作业和脚本中很有用，其中没有用户输入密码。

``-W``, ``--password``
    强制程序在连接到数据库之前提示输入密码。

    此选项从不是必需的，因为如果服务器要求密码验证，程序将自动提示输入密码。但是，pg_repack 将浪费一个连接尝试以确定服务器需要密码。在某些情况下，输入 ``-W`` 可以避免多余的连接尝试。


通用选项
^^^^^^^^

``-e``, ``--echo``
    回显发送到服务器的命令。

``-E LEVEL``, ``--elevel=LEVEL``
    选择输出消息的级别，可从 ``DEBUG``, ``INFO``, ``NOTICE``,
    ``WARNING``, ``ERROR``, ``LOG``, ``FATAL``, ``PANIC`` 中选择。默认为 ``INFO``。

``--help``
    显示程序的使用说明。

``--version``
    显示程序的版本号。


环境
----

``PGDATABASE``, ``PGHOST``, ``PGPORT``, ``PGUSER``
    默认连接参数

    此实用工具与大多数其他 PostgreSQL 实用工具一样，还使用由 libpq 支持的环境变量（参见 `环境变量`__）。

    .. __: http://www.postgresql.org/docs/current/static/libpq-envars.html


示例
----

在数据库``test``中对所有已集群表执行在线 CLUSTER 操作，并对所有非集群表执行在线 VACUUM FULL 操作::

    $ pg_repack test

在数据库``test``中对表``foo``和``bar``执行在线 VACUUM FULL 操作（忽略可能的集群索引）::

    $ pg_repack --no-order --table foo --table bar test

将表``foo``的所有索引移动到表空间``tbs``::

    $ pg_repack -d test --table foo --only-indexes --tablespace tbs

将指定的索引移动到表空间``tbs``::

    $ pg_repack -d test --index idx --tablespace tbs


诊断
----

当 pg_repack 失败时会报告错误消息。以下列表显示了错误的原因。

在致命错误后，您需要手动清理。要进行清理，只需从数据库中删除 pg_repack，然后重新安装：对于 PostgreSQL 9.1 及之后的版本，在发生错误的数据库中执行 ``DROP EXTENSION pg_repack CASCADE``，然后执行 ``CREATE EXTENSION pg_repack``；对于早期版本，加载脚本 ``$SHAREDIR/contrib/uninstall_pg_repack.sql`` 到发生错误的数据库中，然后再次加载 ``$SHAREDIR/contrib/pg_repack.sql``。

.. class:: diag

INFO: database "db" skipped: pg_repack VER is not installed in the database（数据库 "db" 被跳过：pg_repack 在数据库中未安装）
    当指定 ``--all`` 选项时，未在数据库中安装 pg_repack。

    在数据库中创建 pg_repack 扩展。

ERROR: pg_repack VER is not installed in the database（pg_repack 在数据库中未安装）
    未在 ``--dbname`` 指定的数据库中安装 pg_repack。

    在数据库中创建 pg_repack 扩展。

ERROR: program 'pg_repack V1' does not match database library 'pg_repack V2'（程序 'pg_repack V1' 与数据库库 'pg_repack V2' 不匹配）
    ``pg_repack`` 二进制文件与数据库库（``.so`` 或 ``.dll``）不匹配。

    不匹配可能是由于 ``$PATH`` 中错误的二进制文件或错误的数据库地址。检查程序目录和数据库；如果它们符合预期，则可能需要重复 pg_repack 安装。

ERROR: extension 'pg_repack V1' required, found 'pg_repack V2'（扩展 'pg_repack V1' 所需版本为 'pg_repack V2'）
    数据库中找到的 SQL 扩展与 pg_repack 程序所需版本不匹配。

    您应该从数据库中删除扩展，然后按照安装_部分的描述重新加载它。

ERROR: relation "table" must have a primary key or not-null unique keys（表 "table" 必须具有主键或非空唯一键）
    目标表未定义主键或任何唯一约束。

    在表上定义主键或唯一约束。

ERROR: query failed: ERROR: column "col" does not exist（查询失败：ERROR: 列 "col" 不存在）
    目标表未包含 ``--order-by`` 选项指定的列。

    指定现有列。

WARNING: the table "tbl" already has a trigger called repack_trigger（表 "tbl" 已存在名为 repack_trigger 的触发器）
    该触发器可能在先前尝试运行 pg_repack 时安装在表上，并由于某些原因未能清理临时对象。

    您可以通过删除并重新创建扩展来移除所有临时对象：详细信息请参阅安装_部分。

ERROR: Another pg_repack command may be running on the table. Please try again later.（可能有另一个 pg_repack 命令正在表上运行。请稍后重试。）
    当两个并发的 pg_repack 命令在同一表上运行时，可能会发生死锁。因此，请稍后再试运行该命令。

WARNING: Cannot create index "schema"."index_xxxxx", already exists（无法创建索引 "schema"."index_xxxxx"，该索引已存在）
    DETAIL: 先前的 pg_repack 留下了一个无效索引可能是由于之前的 pg_repack 作业未能清理的。请使用 DROP INDEX "schema"."index_xxxxx" 删除此索引，然后重试。

    似乎是 pg_repack 留下的临时索引，我们不希望自己冒险删除此索引。如果确实是旧的 pg_repack 作业创建的索引未能得到清理，您应该使用 DROP INDEX 并再次尝试 repack 命令。


限制
----

pg_repack 具有以下限制。

临时表
^^^^^^

pg_repack 无法重新组织临时表。

GiST 索引
^^^^^^^^^

pg_repack 无法通过 GiST 索引对表进行集群。

DDL 命令
^^^^^^^^

在 pg_repack 工作时，您将无法对目标表执行 DDL 命令，**除了** VACUUM 或 ANALYZE。在全表 repack 过程中，pg_repack 将在目标表上保持 ACCESS SHARE 锁，以强制执行此限制。

如果您使用的是 1.1.8 或更早版本，当 pg_repack 在运行时，请勿尝试在目标表上执行任何 DDL 命令。在许多情况下，pg_repack 将失败并正确回滚，但在这些较早版本中，可能会导致数据损坏。


详细信息
--------

全表 Repack
^^^^^^^^^^^

要执行全表 repack，pg_repack 将：

1. 创建一个日志表来记录对原始表所做的更改
2. 在原始表上添加触发器，将 INSERT、UPDATE 和 DELETE 记录到我们的日志表中
3. 创建一个包含旧表中所有行的新表
4. 在新表上构建索引
5. 将累积在日志表中的所有更改应用到新表中
6. 使用系统目录交换表，包括索引和 toast 表
7. 删除原始表

pg_repack 仅在初始设置（步骤 1 和 2）期间和最终交换和删除阶段（步骤 6 和 7）短暂持有 ACCESS EXCLUSIVE 锁。在其余时间内，pg_repack 只需在原始表上持有 ACCESS SHARE 锁，这意味着 INSERT、UPDATE 和 DELETE 可以像往常一样进行。


仅索引 Repack
^^^^^^^^^^^^^

要执行仅索引 repack，pg_repack 将：

1. 使用与旧索引定义匹配的 CONCURRENTLY，在表上创建新索引
2. 在系统目录中用新索引替换旧索引
3. 删除旧索引

并发创建索引会带来一些注意事项，请参阅 `文档`__ 了解详细信息。

    .. __: http://www.postgresql.org/docs/current/static/sql-createindex.html#SQL-CREATEINDEX-CONCURRENTLY


发布说明
--------

* pg_repack 1.5.1

  * 添加对 PostgreSQL 17 的支持
  * 修复 repack_trigger 中 OID 格式类型错误（问题 #380）
  * 修复 repack.primary_keys 对 NOT NULL 检查的问题（问题 #282）
  * 修复处理需要引号标识符的表空间名称（问题 #386）
  * 用 ``PQconnectdbParams()`` 替换 ``PQconnectdb()``（问题 #382）
  * 添加 ``--apply-count`` 选项（问题 #392）
  * 在 ``--only-indexes`` 选项下不包括声明性分区表（问题 #389）
  * 修复可能同时处理相同 relfilenode 的两个并发 VACUUM 的问题（问题 #399）
  * 在重试获取 AccessShareLock 时使用保存点（问题 #383）
  * 修复交换 relfrozenxid、relfrozenxid 和 relallvisible 的问题（问题 #377, #157）

* pg_repack 1.5.0

  * 添加对 PostgreSQL 16 的支持
  * 修复可能的 SQL 注入漏洞（问题 #368）
  * 支持更长的密码长度（问题 #357）
  * 修复空密码时的无限循环（问题 #354）
  * 添加 ``--switch-threshold`` 选项（问题 #347）
  * 修复在使用无效关系时 ``get_order_by()`` 中的崩溃（问题 #321）
  * 添加对先前使用 `VACUUM FULL` 重写并且对所有列使用 storage=plain 的表的支持（问题 #313）
  * 更谨慎地获取锁（问题 #298）

* pg_repack 1.4.8

  * 添加对 PostgreSQL 15 的支持
  * 修复声明性分区表上的 --parent-table 问题（问题 #288）
  * 从错误日志中删除连接信息（问题 #285）

* pg_repack 1.4.7

  * 添加对 PostgreSQL 14 的支持

* pg_repack 1.4.6

  * 添加对 PostgreSQL 13 的支持
  * 放弃对 9.4 版本之前 PostgreSQL 的支持

* pg_repack 1.4.5

  * 添加对 PostgreSQL 12 的支持
  * 修复公共模式中具有操作符的索引并行处理问题

* pg_repack 1.4.4

  * 添加对 PostgreSQL 11 的支持（问题 #181）
  * 删除重复的密码提示（问题 #184）

* pg_repack 1.4.3

  * 修复可能的 CVE-2018-1058 攻击路径（问题 #168）
  * 在 PostgreSQL 的 CVE-2018-1058 更改后修复 "unexpected index definition"（问题 #169）
  * 在最近的 Ubuntu 包中构建修复（问题 #179）

* pg_repack 1.4.2

  * 添加 PostgreSQL 10 的支持（问题 #120）
  * 修复 DROP INDEX CONCURRENTLY 不能在事务块内运行的错误（问题 #129）

* pg_repack 1.4.1

  * 修复破损的 ``--order-by`` 选项（问题 #138）

* pg_repack 1.4

  * 添加对 PostgreSQL 9.6 的支持，放弃对 9.1 版本之前的支持
  * 使用 ``AFTER`` 触发器解决 ``INSERT CONFLICT`` 的并发问题（问题 #106）
  * 添加 ``--no-kill-backend`` 选项（问题 #108）
  * 添加 ``--no-superuser-check`` 选项（问题 #114）
  * 添加 ``--exclude-extension`` 选项（#97）
  * 添加 ``--parent-table`` 选项（#117）
  * 在重组的表上恢复 TOAST 存储参数（问题 #10）
  * 在重组的表中恢复列的存储类型（问题 #94）

* pg_repack 1.3.4

  * 在删除原始表之前获取独占锁（问题 #81）
  * 不尝试重组未记录日志的表（问题 #71）

* pg_repack 1.3.3

  * 添加对 PostgreSQL 9.5 的支持
  * 修复当中断 pg_repack 命令时可能发生死锁的问题（问题 #55）
  * 当 pg_repack 使用 ``--help`` 和 ``--version`` 被调用时修复退出代码
  * 添加日语语言用户手册

* pg_repack 1.3.2

  * 当中断 pg_repack 命令时清理临时对象
  * 修复 pg_statsinfo 与 pg_repack 共享库加载时可能的崩溃（问题 #43）

* pg_repack 1.3.1

  * 添加对 PostgreSQL 9.4 的支持

* pg_repack 1.3

  * 添加 ``--schema`` 以仅重组指定的模式（问题 #20）
  * 添加 ``--dry-run`` 进行试运行（问题 #21）
  * 修复大于 2B OID 值的咨询锁定（问题 #30）
  * 避免在其他会话锁定待重组表时可能发生的死锁（问题 #32）
  * 提高对执行 sql_pop DELETEs 时的性能
  * 尝试避免 pg_repack 在处理持续大量表变更时花费太长时间的问题

* pg_repack 1.2

  * 支持 PostgreSQL 9.3
  * 添加 ``--tablespace`` 和 ``--moveidx`` 选项以执行在线 SET TABLESPACE
  * 添加 ``--index`` 以仅重组指定的索引
  * 添加 ``--only-indexes`` 以仅重组指定表的索引
  * 添加 ``--jobs`` 选项以进行并行操作
  * 不要求在非集群表上执行 VACUUM FULL 时使用 ``--no-order`` （pg_repack 问题 #6）
  * 不等待其他数据库中持有的锁（pg_repack 问题 #11）
  * Bugfix: 正确处理具有 DESC、NULL FIRST/LAST、COLLATE 等选项的键索引（pg_repack 问题 #3）
  * 修复删除时的数据损坏 bug（pg_repack 问题 #23）
  * 更加有用的程序输出和错误消息

* pg_repack 1.1.8

  * 添加对 PostgreSQL 9.2 的支持
  * 在 PostgreSQL 9.1 和之后添加 CREATE EXTENSION 支持
  * 在等待事务完成时向用户提供反馈（pg_reorg 问题 #5）
  * Bugfix: 允许在新晋升的流复制从库上运行（pg_reorg 问题 #1）
  * Bugfix: 修复 pg_repack 与 Slony 2.0/2.1 之间的交互（pg_reorg 问题 #4）
  * Bugfix: 正确转义列名（pg_reorg 问题 #6）
  * Bugfix: 避免重新创建无效索引或选择它们作为键（pg_reorg 问题 #9）
  * Bugfix: 永不选择部分索引作为主键（pg_reorg 问题 #22）

* pg_reorg 1.1.7 (2011-08-07)

  * Bugfix: 使用重新组织的表的 VIEW 和 FUNCTION 可能会损坏，该表具有已删除列。
  * 支持 PostgreSQL 9.1 和 9.2dev。（但尚未支持 EXTENSION）


另请参阅
--------

* `clusterdb <http://www.postgresql.org/docs/current/static/app-clusterdb.html>`__
* `vacuumdb <http://www.postgresql.org/docs/current/static/app-vacuumdb.html>`__
