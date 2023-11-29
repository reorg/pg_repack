.. pg_repack -- Reorganize tables in PostgreSQL databases with minimal locks
   =========================================================================

pg_repack -- PostgreSQLデータベースのテーブルを最小限のロックで再編成します
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

pg_repack_ はPostgreSQLの拡張の一つで、肥大化したテーブルやインデックスを再編成し、さらに指定したインデックスにしたがってレコードを並び替えることができます。
PostgreSQLの CLUSTER_ や `VACUUM FULL`_ コマンドと違って、pg_repackは処理の間対象テーブルへの排他ロックを保持し続けないため、オンライン中に動作させることができます。
pg_repackはCLUSTERコマンドを直接実行するのと同じくらいの性能で起動することができて効率的です。

.. pg_repack is a fork of the previous pg_reorg_ project. Please check the
   `project page`_ for bug report and development information.

pg_repack は pg_reorg_ からフォークしたプロジェクトです。
バグ報告や開発情報については `project page`_ を参照してください。

.. You can choose one of the following methods to reorganize:
  
  * Online CLUSTER (ordered by cluster index)
  * Ordered by specified columns
  * Online VACUUM FULL (packing rows only)
  * Rebuild or relocate only the indexes of a table

pg_repackでは再編成する方法として次のものが選択できます。

* オンラインCLUSTER (cluster index順にレコードを並び替える)
* 指定したカラムでレコードを並び替える
* オンラインVACUUM FULL (レコードのすきまを詰める)
* 指定したテーブルのインデックスだけを再構築、もしくは再配置する 

.. NOTICE:
  
  * Only superusers can use the utility.
  * Target table must have a PRIMARY KEY, or at least a UNIQUE total index on a
    NOT NULL column.

注意:

* DBのスーパーユーザだけがpg_repackを実行できます
* 対象となるテーブルは主キー、もしくはNOT NULL制約を持つカラムへのユニーク制約をもつインデックスが存在している必要があります

.. _pg_repack: https://reorg.github.io/pg_repack
.. _CLUSTER: http://www.postgresql.jp/document/current/html/sql-cluster.html
.. _VACUUM FULL: VACUUM_
.. _VACUUM: http://www.postgresql.jp/document/current/html/sql-vacuum.html
.. _project page: https://github.com/reorg/pg_repack
.. _pg_reorg: https://github.com/reorg/pg_reorg


.. Requirements
  ------------
  
  PostgreSQL versions
      PostgreSQL 9.5, 9.6, 10, 11, 12, 13, 14, 15, 16.
  
      PostgreSQL 9.4 and before it are not supported.
  
  Disks
      Performing a full-table repack requires free disk space about twice as
      large as the target table(s) and its indexes. For example, if the total
      size of the tables and indexes to be reorganized is 1GB, an additional 2GB
      of disk space is required.


動作環境
---------

PostgreSQL バージョン
    PostgreSQL 9.5, 9.6, 10, 11, 12, 13, 14, 15, 16

ディスク
    テーブル全体の再編成を行うには、対象となるテーブルと付属するインデックスのおよそ2倍のサイズのディスク空き容量が必要です。例えば、テーブルとインデックスを合わせたサイズが1GBの場合、2GBのディスク領域が必要となります。

.. Download
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


ダウンロード
------------

pg_repackは、PGXNのWebサイトから `ダウンロード`__ できます。
ダウンロードしたアーカイブを展開し、以下の手順にしたがって `インストール`_ してください。

.. __: http://pgxn.org/dist/pg_repack/

もしくは、 `PGXN Client`_ を使ってダウンロードからコンパイル、インストールすることもできます。::

    $ pgxn install pg_repack

利用可能なオプションについては `pgxn install コマンドのドキュメント`__ を参照してください。

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
  process. On PostgreSQL 9.1 and following pg_repack is packaged as an
  extension, so you can execute::
  
      $ psql -c "CREATE EXTENSION pg_repack" -d your_database
  
  For previous PostgreSQL versions you should load the script
  ``$SHAREDIR/contrib/pg_repack.sql`` in the database to process; you can
  get ``$SHAREDIR`` using ``pg_config --sharedir``, e.g. ::
  
      $ psql -f "$(pg_config --sharedir)/contrib/pg_repack.sql" -d your_database
  
  You can remove pg_repack from a PostgreSQL 9.1 and following database using
  ``DROP EXTENSION pg_repack``. For previous Postgresql versions load the
  ``$SHAREDIR/contrib/uninstall_pg_repack.sql`` script or just drop the
  ``repack`` schema.
  
  If you are upgrading from a previous version of pg_repack or pg_reorg, just
  drop the old version from the database as explained above and install the new
  version.

インストール
------------

Unix やLinux上では、pg_repackは ``make`` コマンドでビルドすることができます。
その際、PostgreSQLの拡張向けの構築基盤であるPGXSが自動で利用されます。
ビルトに当たっては、事前にPostgreSQLの開発パッケージ (``postgresql-devel``, etc.)をインストールしておく必要があるかもしれません。
そして、 ``pg_config`` コマンドが存在するディレクトリが ``$PATH`` に追加されている必要があります。

その上で、以下のコマンドを実行します。::

    $ cd pg_repack
    $ make
    $ sudo make install

Windows OS上ではMicrosoft Visual C++ 2010を利用してビルドすることができます。
``msvc`` ディレクトリ配下にプロジェクトファイルがあります。 

インストールを行った後、pg_repack エクステンションを対象のデータベースに登録します。
PostgreSQL 9.1以上のバージョンでは、以下のコマンドで実施できます。 ::

    $ psql -c "CREATE EXTENSION pg_repack" -d your_database

それ以前のPostgreSQLバージョンの場合は、 ``$SHAREDIR/contrib/pg_repack.sql`` スクリプトを対象とするデータベースに対して実施します。 ``$SHAREDIR`` は ``pg_config --sharedir`` コマンドを実行することで確認できます。 ::

    $ psql -f "$(pg_config --sharedir)/contrib/pg_repack.sql" -d your_database

pg_repackの登録を削除するには、PostgreSQL 9.1以上のバージョンでは、``DROP EXTENSION pg_repack`` を対象データベースに実行します。それ以前のPostgreSQLバージョンの場合は、 ``$SHAREDIR/contrib/uninstall_pg_repack.sql`` スクリプトを実行するか、 ``repack`` スキーマを削除します。

pg_repackもしくはpg_reorgの古いバージョンからのアップグレードを行うには、古いバージョンをデータベースから上記の手順で削除し、新しいバージョンを登録します。

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

利用方法
---------

::

    pg_repack [OPTION]... [DBNAME]

OPTIONには以下のものが指定できます。

固有オプション:
  -a, --all                 すべてのデータベースに対して実行します
  -t, --table=TABLE         指定したテーブルに対して実行します
  -I, --parent-table=TABLE  指定したテーブルとそれを継承する全ての子テーブルに対して実行します
  -c, --schema=SCHEMA       指定したスキーマに存在するテーブル全てに対して実行します
  -s, --tablespace=TBLSPC   指定したテーブル空間に再編成後のテーブルを配置します
  -S, --moveidx             -s/--tablespaceで指定したテーブル空間に再編成対象のテーブルに付与されたインデックスも配置します
  -o, --order-by=COLUMNS    指定したカラムの値順に再編成します
  -n, --no-order            オンラインVACUUM FULL相当の処理を行います
  -N, --dry-run             実際の処理は行わず、メッセージのみだけ出力します
  -j, --jobs=NUM            指定した並列度で処理を行います
  -i, --index=INDEX         指定したインデックスのみ再編成します
  -x, --only-indexes        指定したテーブルに付与されたインデックスだけを再編成します
  -T, --wait-timeout=SECS   ロック競合している他のトランザクションをキャンセルするまで待機する時間を指定します
  -D, --no-kill-backend     タイムアウト時に他のバックエンドをキャンセルしません
  -Z, --no-analyze          再編成後にANALYZEを行いません
  -k, --no-superuser-check  接続ユーザがスーパーユーザかどうかのチェックを行いません

接続オプション:
  -d, --dbname=DBNAME       接続する対象のデータベースを指定します
  -h, --host=HOSTNAME       接続する対象のホスト名、もしくはUNIXソケットドメインディレクトリを指定します
  -p, --port=PORT           接続する対象のデータベース・サーバのポート番号を指定します
  -U, --username=USERNAME   接続するユーザ名を指定します
  -w, --no-password         パスワードの入力表示を無効化します
  -W, --password            パスワード入力表示を強制的に表示します

一般オプション:
  -e, --echo                サーバに送信するSQLを表示します
  -E, --elevel=LEVEL        ログ出力レベルを指定します
  --help                    使用方法を表示します

.. Reorg Options
  ^^^^^^^^^^^^^

再編成オプション
----------------

.. ``-a``, ``--all``
    Attempt to repack all the databases of the cluster. Databases where the
    ``pg_repack`` extension is not installed will be skipped.

``-a``, ``--all``
    データベースクラスタのすべてのデータベースを再編成します。pg_repackのエクステンションがインストールされていないデータベースはスキップされます。

.. ``-t TABLE``, ``--table=TABLE``
    Reorganize the specified table(s) only. Multiple tables may be
    reorganized by writing multiple ``-t`` switches. By default, all eligible
    tables in the target databases are reorganized.

``-t TABLE``, ``--table=TABLE``
    指定したテーブルのみを再編成します。 ``-t`` オプションを複数同時に使用することで、複数のテーブルを指定することができます。このオプションを指定しない限り、対象のデータベースに存在するすべてのテーブルを再編成します。

.. ``-I TABLE``, ``--parent-table=TABLE``
    Reorganize both the specified table(s) and its inheritors. Multiple
    table hierarchies may be reorganized by writing multiple ``-I`` switches.

``-I TABLE``, ``--parent-table=TABLE``
    指定したテーブルとその子テーブルのみを再編成します。 ``-I`` オプションを複数同時に使用することで、複数の親テーブルを指定することができます。

.. ``-c``, ``--schema``
    Repack the tables in the specified schema(s) only. Multiple schemas may
    be repacked by writing multiple ``-c`` switches. May be used in
    conjunction with ``--tablespace`` to move tables to a different tablespace.

``-c``, ``--schema``
    指定したスキーマに存在するテーブルを再編成します。 ``-c`` オプションを複数同時に指定することで、複数のスキーマを指定することができます。 ``--tablespace`` オプションと同時に使用することで、特定のスキーマのテーブルを別のテーブル空間に移動する利用例が挙げられます。

.. ``-o COLUMNS [,...]``, ``--order-by=COLUMNS [,...]``
    Perform an online CLUSTER ordered by the specified columns.

``-o COLUMNS [,...]``, ``--order-by=COLUMNS [,...]``
    指定したカラムの値を用いてオンラインCLUSTER処理を実行します。

.. ``-n``, ``--no-order``
    Perform an online VACUUM FULL.  Since version 1.2 this is the default for
    non-clustered tables.

``-n``, ``--no-order``
    オンラインVACUUM FULL処理を実行します。バージョン1.2から、クラスタキーのないテーブルに対してはこれがデフォルトの挙動になっています。

.. ``-N``, ``--dry-run``
    List what would be repacked and exit.

``-N``, ``--dry-run``
    実際の処理は実行せずに、実施する内容についてのメッセージだけを出力します。

.. ``-j``, ``--jobs``
    Create the specified number of extra connections to PostgreSQL, and
    use these extra connections to parallelize the rebuild of indexes
    on each table. Parallel index builds are only supported for full-table
    repacks, not with ``--index`` or ``--only-indexes`` options. If your
    PostgreSQL server has extra cores and disk I/O available, this can be a
    useful way to speed up pg_repack.

``-j``, ``--jobs``
    指定した数だけ追加でPostgreSQLへのコネクションを作成し、それらのコネクションを使って並列でインデックス作成処理を行います。並列でのインデックス作成は、テーブル全体を再編成する場合にのみ有効です。 ``--index`` や ``--only-indexes`` オプションとは同時に利用できません。PostgreSQLサーバのCPUコア数およびディスクI/Oに余裕がある場合には、このオプションを利用することでpg_repackの処理を高速化するための有力な手段になりえます。

.. ``-s TBLSPC``, ``--tablespace=TBLSPC``
    Move the repacked tables to the specified tablespace: essentially an
    online version of ``ALTER TABLE ... SET TABLESPACE``. The tables' indexes
    are left in the original tablespace unless ``--moveidx`` is specified too.

``-s TBLSPC``, ``--tablespace=TBLSPC``
    再編成したテーブルを指定したテーブル空間に移動します。即ち、 ``ALTER TABLE ... SET TABLESPACE`` 相当の処理をオンラインで実施します。 ``--moveidx`` オプションを併用しない限り、再編成したテーブルのインデックスは元のテーブル空間に残されます。

.. ``-S``, ``--moveidx``
    Also move the indexes of the repacked tables to the tablespace specified
    by the ``--tablespace`` option.

``-S``, ``--moveidx``
    ``--tablespace`` オプションと併用することで、再編成したテーブルのインデックスも指定したテーブル空間に移動します。

.. ``-i``, ``--index``
    Repack the specified index(es) only. Multiple indexes may be repacked
    by writing multiple ``-i`` switches. May be used in conjunction with
    ``--tablespace`` to move the index to a different tablespace.

``-i``, ``--index``
    指定したインデックスのみを再編成します。 ``-i`` オプションを複数同時に指定することで、複数のインデックスを指定することができます。 ``--tablespace`` オプションと同時に使用することで、特定のスキーマのテーブルを別のテーブル空間に移動する利用例が挙げられます。

.. ``-x``, ``--only-indexes``
    Repack only the indexes of the specified table(s), which must be specified
    with the ``--table`` or ``--parent-table`` option.

``-x``, ``--only-indexes``
    ``--table`` または ``--parent-table`` オプションと併用することで、指定したテーブルのインデックスのみを再編成します。

.. ``-T SECS``, ``--wait-timeout=SECS``
    pg_repack needs to take an exclusive lock at the end of the
    reorganization.  This setting controls how many seconds pg_repack will
    wait to acquire this lock. If the lock cannot be taken after this duration
    and ``--no-kill-backend`` option is not specified, pg_repack will forcibly
    cancel the conflicting queries. If you are using PostgreSQL version 8.4
    or newer, pg_repack will fall back to using pg_terminate_backend() to
    disconnect any remaining backends after twice this timeout has passed.
    The default is 60 seconds.

``-T SECS``, ``--wait-timeout=SECS``
    pg_repackは再編成の完了直前に排他ロックを利用します。このオプションは、このロック取得時に何秒間pg_repackが取得を待機するかを指定します。指定した時間経ってもロックが取得できないかつ、``no-kill-backend``\オプションが指定されていない場合、pg_repackは競合するクエリを強制的にキャンセルさせます。PostgreSQL 8.4以上のバージョンを利用している場合、指定した時間の2倍以上経ってもロックが取得できない場合、pg_repackは競合するクエリを実行しているPostgreSQLバックエンドプロセスをpg_terminate_backend()関数により強制的に停止させます。このオプションのデフォルトは60秒です。

..  ``-D``, ``--no-kill-backend``
    Skip to repack table if the lock cannot be taken for duration specified
    ``--wait-timeout``, instead of cancelling conflicting queries. The default
    is false.

``-D``, ``--no-kill-backend``
    ``--wait-timeout``\オプションで指定された時間が経過してもロックが取得できない場合、競合するクエリをキャンセルする代わりに対象テーブルの再編成をスキップします。

.. ``-Z``, ``--no-analyze``
    Disable ANALYZE after a full-table reorganization. If not specified, run
    ANALYZE after the reorganization.

``-Z``, ``--no-analyze``
    再編成終了後にANALYZEを行うことを無効にします。デフォルトでは再編成完了後に統計情報を更新するためANALYZEを実行します。

.. ``-k``, ``--no-superuser-check``
    Skip the superuser checks in the client. This setting is useful for using
    pg_repack on platforms that support running it as non-superusers.

``-k``, ``--no-superuser-check``
    接続ユーザがスーパーユーザかどうかのチェックを行いません。これは、非スーパーユーザのみが利用できる環境でpg_repackを使用するときに有用です。

.. Connection Options
   ^^^^^^^^^^^^^^^^^^
  Options to connect to servers. You cannot use ``--all`` and ``--dbname`` or
  ``--table`` or ``--parent-table`` together.

接続オプション
---------------

PostgreSQLサーバに接続するためのオプションです。
``--all`` オプションと同時に ``--dbname`` 、 ``--table`` や ``--parent-table`` を利用することはできません。


.. ``-a``, ``--all``
    Reorganize all databases.

``-a``, ``--all``
    すべてのデータベースを再編成します。

.. ``-d DBNAME``, ``--dbname=DBNAME``
    Specifies the name of the database to be reorganized. If this is not
    specified and ``-a`` (or ``--all``) is not used, the database name is read
    from the environment variable PGDATABASE. If that is not set, the user
    name specified for the connection is used.

``-d DBNAME``, ``--dbname=DBNAME``
    指定したデータベースのみを再編成します。このオプションや ``-a`` ( ``--all`` )オプションを指定しなかった場合、環境変数PGDATABASEで指定されたデータベースを再編成します。PGDATABASEも指定されていない場合、接続に利用するユーザ名と同じ名称のデータベースを再編成します。

.. ``-h HOSTNAME``, ``--host=HOSTNAME``
    Specifies the host name of the machine on which the server is running. If
    the value begins with a slash, it is used as the directory for the Unix
    domain socket.

``-h HOSTNAME``, ``--host=HOSTNAME``
    指定したホスト名を持つサーバ上のPostgreSQLに接続します。指定した値が ``/`` で始まる場合、Unixドメインソケットが配置されたディレクトリと解釈して接続します。

.. ``-p PORT``, ``--port=PORT``
    Specifies the TCP port or local Unix domain socket file extension on which
    the server is listening for connections.

``-p PORT``, ``--port=PORT``
    指定したポート番号でPostgreSQLサーバに接続します。

.. ``-U USERNAME``, ``--username=USERNAME``
    User name to connect as.

``-U USERNAME``, ``--username=USERNAME``
    指定したユーザ名でPostgreSQLサーバに接続します。

.. ``-w``, ``--no-password``
    Never issue a password prompt. If the server requires password
    authentication and a password is not available by other means such as a
    ``.pgpass`` file, the connection attempt will fail. This option can be
    useful in batch jobs and scripts where no user is present to enter a
    password.

``-w``, ``--no-password``
    接続時にパスワード入力プロンプトを表示されないようにします。もし接続先のPostgreSQLサーバがパスワード認証を要求していて、パスワードが``.pgpass``ファイルなどの手段で取得できない場合、pg_repackは接続に失敗します。このオプションはパスワード入力なしで接続できるユーザを用いたバッチ処理やスクリプトにて利用します。

.. ``-W``, ``--password``
    Force the program to prompt for a password before connecting to a
    database.
  
    This option is never essential, since the program will automatically
    prompt for a password if the server demands password authentication.
    However, pg_repack will waste a connection attempt finding out that the
    server wants a password. In some cases it is worth typing ``-W`` to avoid
    the extra connection attempt.

``-W``, ``--password``
    接続時にパスワード入力プロンプトを強制的に表示します。
    サーバがパスワード認証を要求する場合、そもそも自動的にパスワード入力が促されるため、このオプションが重要になることはありません。
    しかし、サーバにパスワードが必要かどうかを判断するための接続試行を無駄に行います。 
    こうした余計な接続試行を防ぎたいのであれば、このオプションが利用してください。


.. Generic Options
   ^^^^^^^^^^^^^^^

一般オプション
--------------

.. ``-e``, ``--echo``
    Echo commands sent to server.

``-e``, ``--echo``
    サーバに送信するSQLを表示します。

.. ``-E LEVEL``, ``--elevel=LEVEL``
    Choose the output message level from ``DEBUG``, ``INFO``, ``NOTICE``,
    ``WARNING``, ``ERROR``, ``LOG``, ``FATAL``, and ``PANIC``. The default is
    ``INFO``.

``-E LEVEL``, ``--elevel=LEVEL``
    ログ出力レベルを設定します。 ``DEBUG``, ``INFO``. ``NOTICE``, ``WARNING``, ``ERROR``, ``LOG``, ``FATAL``, ``PANIC`` から選択できます。デフォルトは ``INFO`` です。

.. ``--help``
    Show usage of the program.

``--help``
    利用方法についての説明を表示します。

.. ``--version``
    Show the version number of the program.

``--version``
    バージョン情報を表示します。

.. Environment
  -----------
  
  ``PGDATABASE``, ``PGHOST``, ``PGPORT``, ``PGUSER``
      Default connection parameters
  
      This utility, like most other PostgreSQL utilities, also uses the
      environment variables supported by libpq (see `Environment Variables`__).
  
      .. __: http://www.postgresql.jp/document/current/html/libpq-envars.html

環境変数
---------

``PGDATABASE``, ``PGHOST``, ``PGPORT``, ``PGUSER``
    接続パラメータのデフォルト値として利用されます。

　　また、このユーティリティは、他のほとんどの PostgreSQL ユーティリティと同様、libpq でサポートされる環境変数を使用します。詳細については、 `環境変数`__  の項目を参照してください。

    .. __: http://www.postgresql.jp/document/current/html/libpq-envars.html

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

利用例
-------

以下のコマンドは、 ``test`` データベースのクラスタ可能なテーブル全てに対してオンラインCLUSTERを行い、その他のテーブルに対してオンラインVACUUM FULLを行います。::

    $ pg_repack test

``test`` データベースの ``foo`` テーブルと ``bar`` テーブルに対してオンラインVACUUM FULLを実行するには、以下のようにします。 ::

    $ pg_repack --no-order --table foo --table bar test

``foo`` テーブルのインデックス全てをテーブル空間 ``tbs`` に移動するには、以下のようにします。 ::

    $ pg_repack -d test --table foo --only-indexes --tablespace tbs

インデックス ``idx`` をテーブル空間 ``tbs`` に移動するには、以下のようにします。  ::

    $ pg_repack -d test --index idx --tablespace tbs

.. Diagnostics
   -----------

トラブルシューティング
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

pg_repackが失敗した場合、エラーメッセージが表示されます。
エラーの原因について以下に列記します。

FATALエラーが発生した場合、手動でクリーンアップを行う必要があります。
クリーンアップするには、pg_repackをデータベースから一度削除し、再度登録するだけです。
PostgreSQL 9.1以降では、 ``DROP EXTENSION pg_repack CASCADE`` をエラーが起きた
データベースで実行し、続いて ``CREATE EXTENSION pg_repack`` を実行します。
これより古いバージョンの場合、 ``$SHAREDIR/contrib/uninstall_pg_repack.sql`` 
スクリプトをエラーが起きたデータベースに対して実行し、その後 
``$SHAREDIR/contrib/pg_repack.sql`` を同様に実行します。

.. INFO: database "db" skipped: pg_repack VER is not installed in the database
    pg_repack is not installed in the database when the ``--all`` option is
    specified.
   
    Create the pg_repack extension in the database.

.. class:: diag

INFO: database "db" skipped: pg_repack VER is not installed in the database
    ``--all`` オプション指定時に、pg_repackがインストールされていない
    データベースに対して表示されます。

    該当のデータベースに対してpg_repackをインストールしてください。

.. ERROR: pg_repack VER is not installed in the database
    pg_repack is not installed in the database specified by ``--dbname``.
  
    Create the pg_repack extension in the database.

.. class:: diag

ERROR: pg_repack VER is not installed in the database
    ``--dbname`` オプション指定時に、指定したデータベースにpg_repackが
    インストールされていない場合に表示されます。

    該当のデータベースに対してpg_repackをインストールしてください。

.. ERROR: program 'pg_repack V1' does not match database library 'pg_repack V2'
    There is a mismatch between the ``pg_repack`` binary and the database
    library (``.so`` or ``.dll``).
  
    The mismatch could be due to the wrong binary in the ``$PATH`` or the
    wrong database being addressed. Check the program directory and the
    database; if they are what expected you may need to repeat pg_repack
    installation.

.. class:: diag

ERROR: program 'pg_repack V1' does not match database library 'pg_repack V2'
    There is a mismatch between the ``pg_repack`` binary and the database
    library (``.so`` or ``.dll``).

    データベースに登録されたpg_repackがバージョン2系であるのに、クライアント側
    コマンドのpg_repackのバージョンが1系である場合に表示されます。
    ``$PATH`` に誤ったpg_repackのバイナリを指定していたり、接続先のデータベースが
    間違っている可能性があります。pg_repackプログラムがインストールされた
    ディレクトリとデータベースを確認してください。それらが適切である場合、
    pg_repackを再インストールしてください。

.. ERROR: extension 'pg_repack V1' required, found extension 'pg_repack V2'
    The SQL extension found in the database does not match the version
    required by the pg_repack program.
  
    You should drop the extension from the database and reload it as described
    in the installation_ section.

.. class:: diag

ERROR: extension 'pg_repack V1' required, found extension 'pg_repack V2'
    クライアント側のpg_repackがバージョン1系であるのに、データベース側に
    登録されたpg_repackがバージョン2系の場合に表示されます。
    当該データベースからpg_repackを削除し、 `インストール`_ に従って
    再登録してください。 

.. ERROR: relation "table" must have a primary key or not-null unique keys
    The target table doesn't have a PRIMARY KEY or any UNIQUE constraints
    defined.
  
    Define a PRIMARY KEY or a UNIQUE constraint on the table.

.. class:: diag

ERROR: relation "table" must have a primary key or not-null unique keys
    対象のテーブルが主キーもしくはNOT NULLなユニーク制約を持っていない場合に表示されます。
    主キーもしくはユニーク制約を定義してください。

.. ERROR: query failed: ERROR: column "col" does not exist
    The target table doesn't have columns specified by ``--order-by`` option.
  
    Specify existing columns.

.. class:: diag

ERROR: query failed: ERROR: column "col" does not exist
    対象のテーブルが  ``--order-by`` オプションで指定したカラムを持っていない場合に表示されます。
    存在しているカラムを指定してください。

.. WARNING: the table "tbl" already has a trigger called a_repack_trigger
    The trigger was probably installed during a previous attempt to run
    pg_repack on the table which was interrupted and for some reason failed
    to clean up the temporary objects.
  
    You can remove all the temporary objects by dropping and re-creating the
    extension: see the installation_ section for the details.

.. class:: diag

WARNING: the table "tbl" already has a trigger called repack_trigger
    以前に実行したが何らかの理由で中断したか、あるいは失敗したpg_repackコマンドにより、
    対象テーブルにpg_repackが利用するトリガが残存している場合に表示されます。
    pg_repackを一度削除して、再度登録することで、こうした一時オブジェクトを削除できます。
    `インストール`_ を参照してください。
    
.. WARNING: trigger "trg" conflicting on table "tbl"
    The target table has a trigger whose name follows ``repack_trigger``
    in alphabetical order.
  
    The ``repack_trigger`` should be the first AFTER trigger to fire.
    Please rename your trigger so that it sorts alphabetically before
    pg_repack's one; you can use::
  
        ALTER TRIGGER aaa_my_trigger ON sometable RENAME TO bbb_my_trigger;

.. class:: diag

ERROR: Another pg_repack command may be running on the table. Please try again
    同じテーブルに複数のpg_repackが同時に実行されている場合に表示されます。
    これはデッドロックを引き起こす可能性があるため、片方のpg_repackが終了するのを
    待って再度実行してください。

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
DETAIL: An invalid index may have been left behind by a previous pg_repack
on the table which was interrupted. Please use DROP INDEX "schema"."index_xxxxx"
to remove this index and try again.

    以前に実行したが何らかの理由で中断したか、あるいは失敗したpg_repackコマンドにより、
    pg_repackが利用する一時的なインデックスが残存している場合に表示されます。
    DROP INDEXコマンドにより該当のインデックスを削除して、pg_repackを再実行してください。
    

.. Restrictions
  ------------
  
  pg_repack comes with the following restrictions.

制約
-----

pg_repackには以下の制約があります。

.. Temp tables
  ^^^^^^^^^^^
  
  pg_repack cannot reorganize temp tables.

一時テーブル
^^^^^^^^^^^^

pg_repackは一時テーブルは再編成できません。

.. GiST indexes
  ^^^^^^^^^^^^
  
  pg_repack cannot reorganize tables using GiST indexes.

GiSTインデックス
^^^^^^^^^^^^^^^^

pg_repackはGiSTインデックスを使ってテーブルを再編成することはできません。

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

DDLコマンド
^^^^^^^^^^^^

pg_repackを実行している間、VACUUMもしくはANALYZE以外のDDLコマンドを対象の
テーブルに対して実行することはできません。何故ならば、pg_repackは
ACCESS SHAREロックを対象テーブルに対して保持しつづけるからです。

バージョン1.1.8もしくはそれ以前のバージョンを使っている場合、あらゆるDDL
コマンドをpg_repackが走っているテーブルに対して実行することができません。
大抵はpg_repackが失敗してロールバックが適切に行われますが、古いバージョンでは
いくつかのケースでデータ不整合を引き起こす可能性があります。

.. Details
  -------

動作詳細
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

テーブル再編成
^^^^^^^^^^^^^^^

テーブル全体を再編成する場合、pg_repackは以下のように動作します:

1. 対象のテーブルに対して実行される変更を記録するためのログテーブルを作成します
2. 対象のテーブルに、INSERT、UPDATE、DELETEが行われた際にログテーブルに変更内容を記録するトリガを追加します
3. 対象テーブルに含まれるレコードを元に、新しいテーブルを指定した編成順でレコードを並ばせながら作成します
4. 新しいテーブルに対してインデックスを作成します
5. 再編成中に行われた元のテーブルに対する変更内容をログテーブルから取り出し、新しいテーブルに反映します
6. システムカタログを更新し、元のテーブルと新しいテーブルを入れ替えます。インデックスやトーストテーブルも入れ替えます
7. 元のテーブルを削除します

pg_repackは上の手順の中で、始めの1.と2.の時点、および最後の6.と7.の時点で対象のテーブルに対する
ACCESS EXCLUSIVEロックを取得します。その他のステップでは、ACCESS SHAREロックを必要とするだけなので、
元のテーブルに対するINSERT, UPDATE, DELETE操作は通常通りに実行されます。

.. Index Only Repacks
  ^^^^^^^^^^^^^^^^^^
  
  To perform an index-only repack, pg_repack will:
  
  1. create new indexes on the table using CONCURRENTLY matching the definitions of the old indexes
  2. swap out the old for the new indexes in the catalogs
  3. drop the old indexes
  
  Creating indexes concurrently comes with a few caveats, please see `the documentation`__ for details.
  
      .. __: http://www.postgresql.jp/document/current/html/sql-createindex.html#SQL-CREATEINDEX-CONCURRENTLY

インデックスのみの再編成
^^^^^^^^^^^^^^^^^^^^^^^^^

インデックスのみ再編成する場合、pg_repackは以下のように動作します:

1. 元のインデックス定義に添って、新しいインデックスをCONCURRENTLYオプションを利用して作成します
2. システムカタログを更新し、元のインデックスと新しいインデックスを入れ替えます
3. 元のインデックスを削除します

インデックス作成のCONCURRENTLYオプションにはいくつかの注意点があります。
詳細は、 `PostgreSQLドキュメント`__ を参照してください。

    .. __: http://www.postgresql.jp/document/current/html/sql-createindex.html#SQL-CREATEINDEX-CONCURRENTLY


.. Releases
  --------

リリースノート
---------------

.. * pg_repack 1.4.3
..  * Fixed possible CVE-2018-1058 attack paths (issue #168)
..  * Fixed "unexpected index definition" after CVE-2018-1058 changes in
..    PostgreSQL (issue #169)
..  * Fixed build with recent Ubuntu packages (issue #179)

* pg_repack 1.4.3

  * CVE-2018-1058を利用した攻撃の可能性を修正しました (issue #168)
  * PostgreSQLでのCVE-2018-1058の修正により"unexpected index definition"エラーが発生する事象を修正しました (issue #169)
  * 最近のUbuntuパッケージでビルドが失敗する事象を修正しました (issue #179)

.. * pg_repack 1.4.2
..  * added PostgreSQL 10 support (issue #120)
..  * fixed error DROP INDEX CONCURRENTLY cannot run inside a transaction block (issue #129)

* pg_repack 1.4.2

  * PostgreSQL 10をサポートしました (issue #120)
  * エラー「DROP INDEX CONCURRENTLY cannot run inside a transaction block」が発生する事象を修正しました (issue #129)

.. * pg_repack 1.4.1
..   * fixed broken ``--order-by`` option (issue #138)

* pg_repack 1.4.1

  * 壊れていた ``--order-by`` オプションを修正しました (issue #138)

.. * pg_repack 1.4
..   * added support for PostgreSQL 9.6
..   * use ``AFTER`` trigger to solve concurrency problems with ``INSERT
..     CONFLICT`` (issue #106)
..   * added ``--no-kill-backend`` option (issue #108)
..   * added ``--no-superuser-check`` option (issue #114)
..   * added ``--exclude-extension`` option (#97)
..   * added ``--parent-table`` option (#117)
..   * restore TOAST storage parameters on repacked tables (issue #10)
..   * restore columns storage types in repacked tables (issue #94)

* pg_repack 1.4

  * PostgreSQL 9.6をサポートしました
  * ``INSERT CONFLICT`` を同時実行した際の問題を解決するために、
    ``AFTER`` トリガを使うようにしました(issue #106)
  * ``--no-kill-backend`` オプションを追加しました (issue #108)
  * ``--no-superuser-check`` オプションを追加しました (issue #114)
  * ``--exclude-extension`` オプションを追加しました (#97)
  * ``--parent-table`` オプションを追加しました(#117)
  * TOASTテーブルの格納オプションを再編成後のテーブルに再設定するようにしました (issue #10)
  * 列の格納タイプを再編成後のテーブルに再設定するようにしました (issue #94)

.. * pg_repack 1.3.4
..  * grab exclusive lock before dropping original table (#81)
..  * do not attempt to repack unlogged table (#71)

* pg_repack 1.3.4

  * 元テーブルを削除する前に排他ロックを取得するようにしました(#81)
  * Unlogged Tableを再編成対象から外すようにしました (#71)

.. * pg_repack 1.3.3
..  * Added support for PostgreSQL 9.5
..  * Fixed possible deadlock when pg_repack command is interrupted (issue #55)
..  * Fixed exit code for when pg_repack is invoked with ``--help`` and
..    ``--version``
..  * Added Japanese language user manual

* pg_repack 1.3.3

  * PostgreSQL 9.5をサポートしました
  * pg_repackが中断されたときにデッドロックが発生する可能性を修正しました (issue #55)
  * ``--help`` または ``--version`` オプションを指定した実行したときの終了コードを修正しました
  * 日本語のユーザマニュアルを追加しました

.. * pg_repack 1.3.2
..  * Fixed to clean up temporary objects when pg_repack command is interrupted.
..  * Fixed possible crash when pg_repack shared library is loaded a alongside
..    pg_statsinfo (issue #43)

* pg_repack 1.3.2

  * pg_repackが中断されたときに一時オブジェクトを削除するようにしました
  * pg_statsinfoと同時にロードされている時にクラッシュする可能性を修正しました

.. * pg_repack 1.3.1
..  * Added support for PostgreSQL 9.4.

* pg_repack 1.3.1

  * PostgreSQL 9.4をサポートしました


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

  * 特定のスキーマのみを再編成対象とする ``--schema`` オプションを追加しました ( issue #20)
  * ドライランのための ``--dry-run`` オプションを追加しました (issue #21)
  * 勧告的ロックを取得する際のOIDの扱いを修正しました (issue #30)
  * 再編成予定のテーブルに対して別のセッションたロックを保持している場合にデッドロックが起きないように修正しました (issue #32) 
  * 一度に複数のDELETE操作をsql_popで取り扱う際の性能を改善しました
  * 常に高負荷の更新が行われているテーブルに対する再編成処理が終わらない事象が起きないように修正しました

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

  * PostgreSQL 9.3をサポートしました
  * オンラインSET TABLESPACE文に相当する処理を行うためのオプション ``--tablespace``,  ``--moveidx`` を追加しました
  * 特定のインデックスのみを再編成するためのオプション ``--index`` を追加しました
  * 特定のテーブルのインデックスをまとめて再編成するオプション ``--only-indexes`` を追加しました
  * 並列実行のためのオプション ``--jobs`` を追加しました
  * クラスタキーを持たないテーブルに対してVACUUM FULL相当の処理を行うために ``--no-order`` オプションを明示的に指定しなくてもよいようにしました (pg_repack issue #6) 
  * 他のデータベースにおけるロックを待たないようにしました (pg_repack issue #11)
  * バグ修正: DESC, NULL FIRST/LAST, COLLATEを持つインデックスキーを正しく取り扱えるように修正しました (pg_repack issue #3)
  * 同時に行われる削除操作によってデータ破壊が起こる可能性があったため修正しました (pg_repack issue #23)
  * 出力メッセージとエラーメッセージを改善しました

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

  * PostgreSQL 9.2をサポートしました
  * PostgreSQL 9.1およびそれ以降のバージョンでCREATE EXTENSIONによるインストールが行えるようにしました
  * 他のトランザクションの終了を待っていることをユーザに通知するようにしました (pg_reorg issue #5)
  * バグ修正: ストリーミングレプリケーション構成において、新たにマスタに昇格したサーバ上で動作するように修正しました (pg_reorg issue #1)
  * バグ修正: pg_repackとSlony 2.0/2.1が競合しないように修正しました (pg_reorg issue #4)
  * バグ修正: カラム名を適切にエスケープするように修正しました (pg_reorg issue #6)
  * バグ修正: invalidなインデックスを再編成の対象としたり、クラスタキーとして扱うことがないように修正しました (pg_reorg issue #9)
  * バグ修正: 部分インデックスを主キーとして選択しないように修正しました (pg_reorg issue #22)

.. * pg_reorg 1.1.7 (2011-08-07)
  
  * Bugfix: VIEWs and FUNCTIONs could be corrupted that used a reorganized
    table which has a dropped column.
  * Supports PostgreSQL 9.1 and 9.2dev. (but EXTENSION is not yet)

* pg_reorg 1.1.7 (2011-08-07)

  * バグ修正: 削除されたカラムを持つテーブルを再編成した際に、そのテーブルに対するビューや関数が壊れないように修正しました
  * PostgreSQL 9.1および9.2devをサポートしました (EXTENSIONはまだサポートしていません)

.. See Also
   --------

関連項目
--------

* `clusterdb <http://www.postgresql.jp/document/current/html/app-clusterdb.html>`__
* `vacuumdb <http://www.postgresql.jp/document/current/html/app-vacuumdb.html>`__
