#
# pg_repack: lib/Makefile
#
#  Portions Copyright (c) 2008-2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#  Portions Copyright (c) 2011, Itagaki Takahiro
#  Portions Copyright (c) 2012, The Reorg Development Team
#

PG_CONFIG = pg_config

EXTENSION = pg_repack
MODULE_big = $(EXTENSION)

OBJS = repack.o pgut/pgut-be.o pgut/pgut-spi.o

# The version number of the program. It should be the same of the library.
REPACK_VERSION = $(shell grep '"version":' ../META.json | head -1 \
	| sed -e 's/[ 	]*"version":[ 	]*"\(.*\)",/\1/')

PG_CPPFLAGS = -DREPACK_VERSION=$(REPACK_VERSION)

# Support CREATE EXTENSION for PG >= 9.1 and a simple sql script for PG < 9.1
HAVE_EXTENSION = $(shell $(PG_CONFIG) --version \
	| grep -qE " 8\.| 9\.0" && echo no || echo yes)

ifeq ($(HAVE_EXTENSION),yes)
DATA_built = pg_repack--$(REPACK_VERSION).sql pg_repack.control
else
DATA_built = pg_repack.sql
DATA = uninstall_pg_repack.sql
endif

USE_PGXS = 1	# use pgxs if not in contrib directory
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# remove dependency to libxml2 and libxslt
LIBS := $(filter-out -lxml2, $(LIBS))
LIBS := $(filter-out -lxslt, $(LIBS))

pg_repack.sql: pg_repack.sql.in
	echo "BEGIN;\n" > $@; \
	sed 's,MODULE_PATHNAME,$$libdir/$(MODULE_big),g' $< >> $@; \
	echo "\nCOMMIT;" >> $@;

pg_repack--$(REPACK_VERSION).sql: pg_repack.sql.in
	cat $< > $@;

pg_repack.control: pg_repack.control.in
	sed 's,REPACK_VERSION,$(REPACK_VERSION),g' $< > $@
