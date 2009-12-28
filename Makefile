#
# pg_reorg: Makefile
#
#    Copyright (c) 2008-2009, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#
ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = pg_statsinfo
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
endif

SUBDIRS = bin lib

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
