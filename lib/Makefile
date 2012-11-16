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

# The version of the extension, read from the .control file.
EXTVER = $(shell grep -e '^default_version' $(EXTENSION).control \
	| sed -e "s/[^']*'\([^']*\)'.*/\1/")

# Support CREATE EXTENSION for PG >= 9.1 and a simple sql script for PG < 9.1
HAVE_EXTENSION = $(shell $(PG_CONFIG) --version \
	| grep -qE " 8\.| 9\.0" && echo no || echo yes)

ifeq ($(HAVE_EXTENSION),yes)
DATA_built = pg_repack--$(EXTVER).sql
else
DATA_built = pg_repack.sql
DATA = uninstall_pg_repack.sql
endif

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
	cat $< > $@;
