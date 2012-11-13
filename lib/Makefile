#
# pg_repack: lib/Makefile
#
#  Portions Copyright (c) 2008-2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#  Portions Copyright (c) 2011, Itagaki Takahiro
#  Portions Copyright (c) 2012, The Reorg Development Team
#
MODULE_big = pg_repack
OBJS = repack.o pgut/pgut-be.o pgut/pgut-spi.o

EXTENSION = pg_repack

# The version of the extension, read from the .control file.
# Note that it doesn't need to be the same of the library version: it should
# be increased only when the sql changes.
EXTVER = $(shell grep -e '^default_version' $(EXTENSION).control \
	| sed -e "s/[^']*'\([^']*\)'.*/\1/")

#supports both EXTENSION (for >=9.1) and without_EXTENSION (for <PG 9.1)
DATA_built = pg_repack.sql pg_repack--$(EXTVER).sql
DATA = uninstall_pg_repack.sql

ifndef USE_PGXS
top_builddir = ../../..
makefile_global = $(top_builddir)/src/Makefile.global
ifeq "$(wildcard $(makefile_global))" ""
USE_PGXS = 1	# use pgxs if not in contrib directory
endif
endif

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/$(MODULE_big)
include $(makefile_global)
include $(top_srcdir)/contrib/contrib-global.mk
endif

# remove dependency to libxml2 and libxslt
LIBS := $(filter-out -lxml2, $(LIBS))
LIBS := $(filter-out -lxslt, $(LIBS))

pg_repack.sql: pg_repack.sql.in
	echo "BEGIN;\n" > $@; \
	sed 's,MODULE_PATHNAME,$$libdir/$(MODULE_big),g' $< >> $@; \
	echo "\nCOMMIT;" >> $@;

pg_repack--$(EXTVER).sql: pg_repack.sql.in
	echo '\echo Use "CREATE EXTENSION pg_repack" to load this file. \quit' > $@; \
	cat $< >> $@;
