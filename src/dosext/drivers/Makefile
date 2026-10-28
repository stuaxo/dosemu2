top_builddir=../../..
include $(top_builddir)/Makefile.conf

# any misc. drivers (e.g. cdrom helper)
CFILES=cdrom.c aspi.c cdrom_img.c cdrom_audio.c
SFILES=
ALL=$(CFILES) $(SFILES)

OBJS=$(CFILES:.c=.o)
DEPENDS=$(CFILES:.c=.d)

include $(REALTOPDIR)/src/Makefile.common

install: all
