#
# pg_reorg: Makefile
#
#    Copyright (c) 2008-2009, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#
.PHONY: all install clean

all:
	make -C bin
	make -C lib

install:
	make -C bin install
	make -C lib install

clean:
	make -C bin clean
	make -C lib clean

debug:
	make -C bin DEBUG_REORG=enable
	make -C lib DEBUG_REORG=enable

uninstall: 
	make -C bin uninstall
	make -C lib uninstall

installcheck:
	make -C bin installcheck
