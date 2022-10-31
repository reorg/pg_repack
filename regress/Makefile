#
# pg_repack: regress/Makefile
#
#  Portions Copyright (c) 2008-2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#  Portions Copyright (c) 2011, Itagaki Takahiro
#  Portions Copyright (c) 2012-2020, The Reorg Development Team
#

PG_CONFIG ?= pg_config

# version as a number, e.g. 9.1.4 -> 901
VERSION := $(shell $(PG_CONFIG) --version | sed 's/.* \([[:digit:].]\{1,\}\).*/\1/')
INTVERSION := $(shell echo $$(($$(echo $(VERSION).0 | sed 's/\([[:digit:]]\{1,\}\)\.\([[:digit:]]\{1,\}\).*/\1*100+\2/'))))


#
# Test suite
#

REGRESS := init-extension repack-setup repack-run after-schema repack-check nosuper tablespace get_order_by

USE_PGXS = 1	# use pgxs if not in contrib directory
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
