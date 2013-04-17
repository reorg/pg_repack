#
# pg_repack: bin/Makefile
#
#  Portions Copyright (c) 2008-2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#  Portions Copyright (c) 2011, Itagaki Takahiro
#  Portions Copyright (c) 2012, The Reorg Development Team
#

PG_CONFIG ?= pg_config

# version as a number, e.g. 9.1.4 -> 90104
VERSION := $(shell $(PG_CONFIG) --version | awk '{print $$2}')
INTVERSION := $(shell echo $(VERSION) | sed -E 's/([0-9]+)\.([0-9]+)\.?([0-9]+)?.*/(\1*100+\2)*100+0\3/' | bc)

SRCS = pg_repack.c pgut/pgut.c pgut/pgut-fe.c
OBJS = $(SRCS:.c=.o)
PROGRAM = pg_repack


#
# Test suite
#

ifeq ($(shell echo $$(($(INTVERSION) >= 90100))),1)
REGRESS = init-extension
else
REGRESS = init-legacy
endif

REGRESS += repack tablespace

# This test depends on collate, not supported before 9.0
ifeq ($(shell echo $$(($(INTVERSION) >= 90000))),1)
REGRESS += issue3
endif


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

