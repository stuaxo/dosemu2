
top_builddir=../../..
include $(top_builddir)/Makefile.conf


CFILES=mfs.c mangle.c util.c dosc.c lfn.c mscdex.c
HFILES=mfs.h mangle.h
ALL=$(CFILES) $(HFILES)

ALL_CPPFLAGS += -DDOSEMU=1 -DMANGLE=1 -DMANGLED_STACK=50

include $(REALTOPDIR)/src/Makefile.common

install: all
