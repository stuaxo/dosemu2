#
# (C) Copyright 1992, ..., 2014 the "DOSEMU-Development-Team".
#
# for details see file COPYING.DOSEMU in the DOSEMU distribution
#

top_builddir=../../..
include ../plugins.mak

ALL_CFLAGS+=$(DL_CFLAGS) $(CHDCFLAGS)
CFILES = cdimage_chd.c
LDFLAGS += -Wl,-Bsymbolic
ifeq ($(USE_DL_PLUGINS),1)
all: $(BINPATH)/bin/libplugin_chd.so
endif

include $(REALTOPDIR)/src/Makefile.common.post

ifeq ($(USE_DL_PLUGINS),1)
$(BINPATH)/bin/libplugin_chd.so: $(OBJS) | $(BINPATH)/bin
	$(CC) $(LDFLAGS) $(PLUGIN_LDFLAGS) -o $@ $^ $(CHDLIB)
endif
