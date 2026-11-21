#
# (C) Copyright 1992, ..., 2026 the "DOSEMU-Development-Team".
#
# for details see file COPYING.DOSEMU in the DOSEMU distribution
#

top_builddir=../../..
include ../plugins.mak

ALL_CFLAGS+=$(DL_CFLAGS) $(SNDFILECFLAGS)
CFILES = cdimage_sndfile.c
LDFLAGS += -Wl,-Bsymbolic
ifeq ($(USE_DL_PLUGINS),1)
all: $(BINPATH)/bin/libplugin_sndfile.so
endif

include $(REALTOPDIR)/src/Makefile.common.post

ifeq ($(USE_DL_PLUGINS),1)
$(BINPATH)/bin/libplugin_sndfile.so: $(OBJS) | $(BINPATH)/bin
	$(CC) $(LDFLAGS) $(PLUGIN_LDFLAGS) -o $@ $^ $(SNDFILELIB)
endif
