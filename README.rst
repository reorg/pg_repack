pg_reorg -- UNOFFICIAL REPOSITORY
=================================

This is NOT the official pg_reorg repository. Official development is
currently on pgFoundry: http://pgfoundry.org/projects/reorg

This repository (and the url https://github.com/reorg) has been set up by me
(Daniele) to provide greater visibility and easier contribution to the
pg_reorg project, an intention apparent from a recent `mailing list
discussion`__.

.. __: http://archives.postgresql.org/pgsql-hackers/2012-09/msg00746.php

The current maintainers will be invited to the organization and will receive
administrative privileges, keeping total control of the project.

----

This repository has been created using the command::

	git cvsimport -v -d :pserver:anonymous@cvs.pgfoundry.org:/cvsroot/reorg -A pg_reorg.auths -C pg_reorg -k -r cvs pg_reorg

with a suitably populated pg_reorg.auths.

I assume new CSV commits will be added to ``remotes/cvs/master``, but I'm not
sure yet, so please consider this repository unstable until the development
model has been organized.

