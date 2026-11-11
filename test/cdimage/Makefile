# Host-side tests for the cdimage layer: make -C test/cdimage check
CDIMAGE = ../../src/base/lib/cdimage
INC = ../../src/include
PLG = ../../src/plugin
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu17 -Wall -Wextra -Wstrict-prototypes -DCDIMAGE_STANDALONE -I$(INC)
SRCS = $(CDIMAGE)/cdimage.c $(CDIMAGE)/cdimage_cue.c
LIBS = -lm
OUT ?= out

# The CHD format is a plugin; its tests are built when libchdr is found
CHDR_LIBS := $(shell pkg-config --libs libchdr 2>/dev/null)
ifneq ($(CHDR_LIBS),)
CFLAGS += -DHAVE_LIBCHDR $(shell pkg-config --cflags libchdr)
SRCS += $(PLG)/chd/cdimage_chd.c
LIBS += $(CHDR_LIBS)
endif

cdimage_test: cdimage_test.c $(SRCS) $(INC)/cdimage.h
	$(CC) $(CFLAGS) -o $@ cdimage_test.c $(SRCS) $(LIBS)

cdimage_tool: cdimage_tool.c $(SRCS) $(INC)/cdimage.h
	$(CC) $(CFLAGS) -o $@ cdimage_tool.c $(SRCS) $(LIBS)

$(OUT)/.stamp: make_images.py
	python3 -I make_images.py $(OUT) && touch $@

check: cdimage_test $(OUT)/.stamp
	./cdimage_test $(OUT)

clean:
	rm -rf cdimage_test cdimage_tool $(OUT)
