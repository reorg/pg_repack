#
# pg_reorg: lib/Makefile
#
#    Copyright (c) 2008-2009, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#
SRCS = reorg.c pgut/pgut-be.c
OBJS = $(SRCS:.c=.o)
MODULE_big = pg_reorg
DATA_built = pg_reorg.sql
DATA = uninstall_pg_reorg.sql

ifdef USE_PGXS
PGXS := $(shell pg_config --pgxs)
include $(PGXS)
else
subdir = contrib/pg_reorg
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
