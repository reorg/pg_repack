#
# pg_reorg: bin/Makefile
#
#    Copyright (c) 2008-2009, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#
SRCS = \
	pg_reorg.c
OBJS = $(SRCS:.c=.o) $(top_builddir)/src/bin/scripts/common.o
PROGRAM = pg_reorg
REGRESS = init reorg

ifdef DEBUG_REORG
PG_CPPFLAGS = -I$(libpq_srcdir) -I$(top_builddir)/src/bin/scripts -DDEBUG_REORG
else
PG_CPPFLAGS = -I$(libpq_srcdir) -I$(top_builddir)/src/bin/scripts
endif
PG_LIBS = $(libpq)

ifdef USE_PGXS
PGXS := $(shell pg_config --pgxs)
include $(PGXS)
else
subdir = contrib/pg_reorg
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
