#
# pg_repack: lib/Makefile
#
#  Portions Copyright (c) 2008-2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#  Portions Copyright (c) 2011, Itagaki Takahiro
#  Portions Copyright (c) 2012-2020, The Reorg Development Team
#

PG_CONFIG ?= pg_config

# version as a number, e.g. 9.1.4 -> 901
VERSION := $(shell $(PG_CONFIG) --version | sed 's/.* \([[:digit:].]\{1,\}\).*/\1/')
INTVERSION := $(shell echo $$(($$(echo $(VERSION).0 | sed 's/\([[:digit:]]\{1,\}\)\.\([[:digit:]]\{1,\}\).*/\1*100+\2/'))))

EXTENSION = pg_repack
MODULE_big = $(EXTENSION)

OBJS = repack.o pgut/pgut-spi.o

SHLIB_EXPORTS = exports.txt


# It is not possible to create tables with OIDs on PostgreSQL 12 or later
ifeq ($(shell echo $$(($(INTVERSION) < 1200))),1)
    RELHASOIDS := relhasoids
else
    RELHASOIDS := false
endif

# The version number of the program. It should be the same of the library.
REPACK_VERSION = $(shell grep '"version":' ../META.json | head -1 \
	| sed -e 's/[ 	]*"version":[ 	]*"\(.*\)",/\1/')

PG_CPPFLAGS = -DREPACK_VERSION=$(REPACK_VERSION)

DATA_built = pg_repack--$(REPACK_VERSION).sql pg_repack.control

USE_PGXS = 1
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# remove dependency on libxml2, libxslt, and libpam.
# XXX: find a better way to make sure we are linking with libraries
# from pg_config which we actually need.
LIBS := $(filter-out -lpam -lxml2 -lxslt, $(LIBS))

pg_repack.sql: pg_repack.sql.in
	echo "BEGIN;" > $@; \
	sed 's,MODULE_PATHNAME,$$libdir/$(MODULE_big),g' $< \
	| sed 's,REPACK_VERSION,$(REPACK_VERSION),g' >> $@; \
	echo "COMMIT;" >> $@;

pg_repack--$(REPACK_VERSION).sql: pg_repack.sql.in
	sed 's,REPACK_VERSION,$(REPACK_VERSION),g' $< \
	| sed 's,relhasoids,$(RELHASOIDS),g'> $@;

pg_repack.control: pg_repack.control.in
	sed 's,REPACK_VERSION,$(REPACK_VERSION),g' $< > $@
