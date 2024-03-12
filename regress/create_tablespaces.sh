#!/usr/bin/env bash

sudo -u postgres mkdir /tmp/testts /tmp/1testts /tmp/test\ ts /tmp/test\"ts

sudo -u postgres psql -c "CREATE TABLESPACE testts LOCATION '/tmp/testts'"
sudo -u postgres psql -c "CREATE TABLESPACE \"1testts\" LOCATION '/tmp/1testts'"
sudo -u postgres psql -c "CREATE TABLESPACE \"test ts\" LOCATION '/tmp/test ts'"
sudo -u postgres psql -c "CREATE TABLESPACE \"test\"\"ts\" LOCATION '/tmp/test\"ts'"
