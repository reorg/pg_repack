#
# pg_reorg: bin/Makefile
#
#    Copyright (c) 2008-2010, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#
SRCS = pg_reorg.c pgut/pgut.c pgut/pgut-fe.c
OBJS = $(SRCS:.c=.o)
PROGRAM = pg_reorg
REGRESS = init reorg

ifdef DEBUG_REORG
PG_CPPFLAGS = -I$(libpq_srcdir) -DDEBUG_REORG
else
PG_CPPFLAGS = -I$(libpq_srcdir)
endif
PG_LIBS = $(libpq)

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
