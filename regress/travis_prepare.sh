#!/bin/bash

set -e -x

export PACKAGE="$PGVER"
export PGDIR="/usr/lib/postgresql/$PACKAGE"
export CONFDIR="/etc/postgresql/$PACKAGE/main"
export DATADIR="/var/lib/postgresql/$PACKAGE/main"
export PGBIN="$PGDIR/bin"
export PATH="$PGBIN:$PATH"

# This also stops the server currently running on port 5432
sudo apt-get remove -y libpq5

# Match libpq and server-dev packages
# See https://github.com/reorg/pg_repack/issues/63
sudo sh -c 'echo "deb [arch=amd64] http://apt.postgresql.org/pub/repos/apt $(lsb_release -cs)-pgdg main ${PGVER}" > /etc/apt/sources.list.d/pgdg.list'

# Import the repository signing key:
wget --quiet -O - https://www.postgresql.org/media/keys/ACCC4CF8.asc | sudo apt-key add -

sudo apt-get update

# This might be a moving target, but it currently fails. 13 could start
# failing in the future instead.
# Some versions break if this is not specified (9.4 for sure, maybe 9.6)
if [[ "$PGVER" = "9.4" ]]; then
    sudo apt-get install -y "libpq5=${PGVER}*" "libpq-dev=${PGVER}*"
    sudo apt-mark hold libpq5
fi

# missing build dependency by postgresql-server-dev
if [[ "$PGVER" -ge "14" ]]; then
    sudo apt-get install -y liblz4-dev
fi

if ! sudo apt-get install -y \
    postgresql-$PGVER \
    postgresql-client-$PGVER \
    postgresql-server-dev-$PGVER
then
    sudo systemctl status postgresql.service -l
    exit 1
fi

# ensure PostgreSQL is running on 5432 port with proper auth
sudo sed -i \
    's/\(^local[[:space:]]\+all[[:space:]]\+all[[:space:]]\+\).*/\1trust/' \
    "$CONFDIR/pg_hba.conf"
sudo bash -c "echo 'port=5432' >> $CONFDIR/postgresql.conf"
sudo service postgresql restart $PGVER

# ensure travis user exists. May be missed if the database was not provided by Travis
userexists=`sudo -u postgres "$PGBIN/psql" -tc "select count(*) from pg_catalog.pg_user where usename='travis';"`
if [ ${userexists} -eq 0  ]; then
  sudo -u postgres "$PGBIN/psql" -c "create user travis superuser"
fi

# Go somewhere else or sudo will fail
cd /

# Already started because of installing posgresql-$PGVER
# sudo -u postgres "$PGBIN/pg_ctl" -w -l /dev/null -D "$CONFDIR" start
sudo -u postgres mkdir -p /var/lib/postgresql/testts
sudo -u postgres "$PGBIN/psql" \
    -c "create tablespace testts location '/var/lib/postgresql/testts'"

# Go back to the build dir
cd -
