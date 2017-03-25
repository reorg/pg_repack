#!/bin/bash

set -e -x

export PACKAGE="$PGVER"
export PGDIR="/usr/lib/postgresql/$PACKAGE"
export CONFDIR="/etc/postgresql/$PACKAGE/main"
export DATADIR="/var/lib/postgresql/$PACKAGE/main"
export PGBIN="$PGDIR/bin"
export PATH="$PGBIN:$PATH"

# Match libpq and server-dev packages
# See https://github.com/reorg/pg_repack/issues/63
sudo sed -i "s/main[[:space:]]*$/main ${PGVER}/" \
    /etc/apt/sources.list.d/pgdg.list
sudo apt-get update
sudo apt-get remove -y libpq5
sudo apt-get install -y "libpq5=${PGVER}*" "libpq-dev=${PGVER}*"
sudo apt-mark hold libpq5
sudo apt-get install -y postgresql-server-dev-$PGVER postgresql-$PGVER

# Go somewhere else or sudo will fail
cd /
# Already started because of installing posgresql-$PGVER
# sudo -u postgres "$PGBIN/pg_ctl" -w -l /dev/null -D "$CONFDIR" start
sudo -u postgres mkdir -p /var/lib/postgresql/testts
sudo -u postgres "$PGBIN/psql" \
    -c "create tablespace testts location '/var/lib/postgresql/testts'"
cd -
