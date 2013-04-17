#
# pg_repack: Makefile
#
#  Portions Copyright (c) 2008-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#  Portions Copyright (c) 2011, Itagaki Takahiro
#  Portions Copyright (c) 2012, The Reorg Development Team
#

PG_CONFIG ?= pg_config

SUBDIRS = bin lib

# Pull out the version number from pg_config
VERSION := $(shell $(PG_CONFIG) --version | awk '{print $$2}')

# version as a number, e.g. 9.1.4 -> 90104
INTVERSION := $(shell echo $(VERSION) | sed -E 's/([0-9]+)\.([0-9]+)\.?([0-9]+)?(.*)/(\1*100+\2)*100+0\3/' | bc)

# We support PostgreSQL 8.3 and later.
ifeq ($(shell echo $$(($(INTVERSION) < 80300))),1)
$(error pg_repack requires PostgreSQL 8.3 or later. This is $(VERSION))
endif


all install installdirs uninstall distprep clean distclean maintainer-clean debug:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@ || exit; \
	done

# We'd like check operations to run all the subtests before failing.
check installcheck:
	@CHECKERR=0; for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@ || CHECKERR=$$?; \
	done; \
	exit $$CHECKERR
