#
# pg_repack: bin/Makefile
#
#  Portions Copyright (c) 2008-2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#  Portions Copyright (c) 2011, Itagaki Takahiro
#  Portions Copyright (c) 2012, The Reorg Development Team
#
SRCS = pg_repack.c pgut/pgut.c pgut/pgut-fe.c
OBJS = $(SRCS:.c=.o)
PROGRAM = pg_repack
REGRESS = init repack tablespace

EXTRA_CLEAN = sql/init-$(MAJORVERSION).sql sql/init.sql

# The version number of the program. It should be the same of the library.
REPACK_VERSION = $(shell grep '"version":' ../META.json | head -1 \
	| sed -e 's/[ 	]*"version":[ 	]*"\(.*\)",/\1/')

PG_CPPFLAGS = -I$(libpq_srcdir) -DREPACK_VERSION=$(REPACK_VERSION)

ifdef DEBUG_REPACK
PG_CPPFLAGS += -DDEBUG_REPACK
endif

PG_LIBS = $(libpq)

USE_PGXS = 1	# use pgxs if not in contrib directory
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# remove dependency to libxml2 and libxslt
LIBS := $(filter-out -lxml2, $(LIBS))
LIBS := $(filter-out -lxslt, $(LIBS))

ifndef MAJORVERSION
MAJORVERSION := $(basename $(VERSION))
endif

sql/init.sql: sql/init-$(MAJORVERSION).sql
	cp sql/init-$(MAJORVERSION).sql sql/init.sql
expected/init.out: expected/init-$(MAJORVERSION).out
	cp expected/init-$(MAJORVERSION).out expected/init.out
sql/init-8.3.sql:
	cp sql/init-legacy.sql sql/init-8.3.sql
sql/init-8.4.sql:
	cp sql/init-legacy.sql sql/init-8.4.sql
sql/init-9.0.sql:
	cp sql/init-legacy.sql sql/init-9.0.sql
sql/init-9.1.sql:
	cp sql/init-extension.sql sql/init-9.1.sql
sql/init-9.2.sql:
	cp sql/init-extension.sql sql/init-9.2.sql
sql/init-9.3.sql:
	cp sql/init-extension.sql sql/init-9.3.sql

installcheck: sql/init.sql
