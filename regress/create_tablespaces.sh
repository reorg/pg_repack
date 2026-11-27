#!/usr/bin/env bash

mkdir /tmp/testts /tmp/1testts /tmp/test\ ts /tmp/test\"ts

psql -c "CREATE TABLESPACE testts LOCATION '/tmp/testts'"
psql -c "CREATE TABLESPACE \"1testts\" LOCATION '/tmp/1testts'"
psql -c "CREATE TABLESPACE \"test ts\" LOCATION '/tmp/test ts'"
psql -c "CREATE TABLESPACE \"test\"\"ts\" LOCATION '/tmp/test\"ts'"
