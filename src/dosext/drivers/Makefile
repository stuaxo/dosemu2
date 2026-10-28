top_builddir=../../..
include $(top_builddir)/Makefile.conf

# any misc. drivers (e.g. cdrom helper)
CFILES=cdrom.c aspi.c cd_image.c cd_cue.c
SFILES=
ALL=$(CFILES) $(SFILES)

OBJS=$(CFILES:.c=.o)
DEPENDS=$(CFILES:.c=.d)

include $(REALTOPDIR)/src/Makefile.common

install: all
