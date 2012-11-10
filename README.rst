pg_repack -- UNOFFICIAL REPOSITORY
=================================

This is NOT the official pg_repack repository. Official development is
currently on pgFoundry: http://pgfoundry.org/projects/repack

This repository (and the url https://github.com/repack) has been set up by me
(Daniele) to provide greater visibility and easier contribution to the
pg_repack project, an intention apparent from a recent `mailing list
discussion`__.

.. __: http://archives.postgresql.org/pgsql-hackers/2012-09/msg00746.php

The current maintainers have been be invited to the organization with
administrative privileges, keeping total control of the project.

----

This repository has been created using the command::

	git cvsimport -v -d :pserver:anonymous@cvs.pgfoundry.org:/cvsroot/repack -A pg_repack.auths -C pg_repack -k -r cvs pg_repack

with a suitably populated pg_repack.auths.

I assume new CSV commits will be added to ``remotes/cvs/master``, but I'm not
sure yet, so please consider this repository unstable until the development
model has been organized.

