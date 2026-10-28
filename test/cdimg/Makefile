# Host-side tests for the cd_image layer: make -C test/cdimg check
DRV = ../../src/dosext/drivers
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu17 -Wall -Wextra -Wstrict-prototypes -DCD_IMAGE_STANDALONE -I$(DRV)
SRCS = $(DRV)/cd_image.c $(DRV)/cd_cue.c
LIBS = -lm
OUT ?= out

# CHD tests are built when libchdr is found
CHDR_LIBS := $(shell pkg-config --libs libchdr 2>/dev/null)
ifneq ($(CHDR_LIBS),)
CFLAGS += -DHAVE_LIBCHDR $(shell pkg-config --cflags libchdr)
SRCS += $(DRV)/cd_chd.c
LIBS += $(CHDR_LIBS)
endif

cd_image_test: cd_image_test.c $(SRCS) $(DRV)/cd_image.h
	$(CC) $(CFLAGS) -o $@ cd_image_test.c $(SRCS) $(LIBS)

cd_image_tool: cd_image_tool.c $(SRCS) $(DRV)/cd_image.h
	$(CC) $(CFLAGS) -o $@ cd_image_tool.c $(SRCS) $(LIBS)

$(OUT)/.stamp: make_images.py
	python3 -I make_images.py $(OUT) && touch $@

check: cd_image_test $(OUT)/.stamp
	./cd_image_test $(OUT)

clean:
	rm -rf cd_image_test cd_image_tool $(OUT)
