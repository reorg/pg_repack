#
# pg_repack: regress/Makefile
#
#  Portions Copyright (c) 2008-2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#  Portions Copyright (c) 2011, Itagaki Takahiro
#  Portions Copyright (c) 2012, The Reorg Development Team
#

PG_CONFIG ?= pg_config

# version as a number, e.g. 9.1.4 -> 901
VERSION := $(shell $(PG_CONFIG) --version | awk '{print $$2}')
INTVERSION := $(shell echo $$(($$(echo $(VERSION) | sed 's/\([[:digit:]]\{1,\}\)\.\([[:digit:]]\{1,\}\).*/\1*100+\2/' ))))


#
# Test suite
#

ifeq ($(shell echo $$(($(INTVERSION) >= 901))),1)
REGRESS := init-extension
else
REGRESS := init-legacy
endif

# plpgsql not available by default on pg < 9.0
ifeq ($(shell echo $$(($(INTVERSION) < 900))),1)
REGRESS += plpgsql
endif

REGRESS += repack tablespace

# This test depends on collate, not supported before 9.1
ifeq ($(shell echo $$(($(INTVERSION) >= 901))),1)
REGRESS += issue3
endif


USE_PGXS = 1	# use pgxs if not in contrib directory
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
