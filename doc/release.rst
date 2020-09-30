What to do to release pg_repack
===============================

This document is the list of operations to do to release a new pg_repack
version. The version number in this document is indicated by ``$VER``: it
should be a three-digit dot-separated version, eventually followed by a
pre-release string: ``1.2.0``, ``1.2.1``, ``1.2-dev0``, ``1.2.0-beta1`` are
valid version numbers.

In order to release the package you will need accounts on Github and PGXN
with the right privileges: contact Daniele Varrazzo to obtain them.

- Set the right version number in ``META.json`` (note: it's in two different
  places).
- Set the right release_status in ``META.json``: ``testing`` or ``stable``.
- Commit the above metadata changes.

- Create a package running ``make package``. The package will be called
  ``dist/pg_repack-$VER.zip``.

- Verify the packages installs and passes tests with `pgxn client`__::

    pgxn install --sudo -- dist/pg_repack-$VER.zip
    pgxn check dist/pg_repack-$VER.zip

  (note that ``check`` may require the Postgres bin directory to be added to
  the path, e.g. ``PATH=$(pg_config --bindir):$PATH``; check the ``install``
  log to see where ``pg_repack`` executable was installed).

  .. __: https://pgxn.github.io/pgxnclient/

- Push the code changes on github::

    git push

- Upload the package on http://manager.pgxn.org/.

- Check the uploaded package works as expected; if not fix and push more::

    pgxn install --sudo -- pg_repack
    pgxn check pg_repack

- Create a tag, signed if possible::

    git tag -a -s ver_$VER

- Push the new tag on github::

    git push --tags

- Upload the docs by pushing in the repos at
  http://reorg.github.io/pg_repack/. The operations are roughly::

    git clone --recursive git@github.com:reorg/reorg.github.com.git
    cd reorg.github.com
    make sm
    make
    git commit -a -m "Docs upload for release $VER"
    git push

- Check the page http://reorg.github.io/pg_repack/ is right.

- Announce the package on pgsql-announce@postgresql.org.
