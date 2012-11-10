#
# pg_repack: Makefile
#
#  Portions Copyright (c) 2008-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#  Portions Copyright (c) 2011, Itagaki Takahiro
#
ifndef USE_PGXS
top_builddir = ../..
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
subdir = pg_repack
include $(makefile_global)
include $(top_srcdir)/contrib/contrib-global.mk
endif

SUBDIRS = bin lib

# Pull out the version number from pg_config
VERSION = $(shell $(PG_CONFIG) --version | awk '{print $$2}')

# We support PostgreSQL 8.3 and later.
ifneq ($(shell echo $(VERSION) | grep -E "^7\.|^8\.[012]"),)
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
