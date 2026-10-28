# Host-side tests for the cdimage layer: make -C test/cdimage check
CDIMAGE = ../../src/base/lib/cdimage
INC = ../../src/include
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu17 -Wall -Wextra -Wstrict-prototypes -DCDIMAGE_STANDALONE -I$(INC)
OUT ?= out

cdimage_test: cdimage_test.c $(CDIMAGE)/cdimage.c $(CDIMAGE)/cdimage_cue.c $(INC)/cdimage.h
	$(CC) $(CFLAGS) -o $@ cdimage_test.c $(CDIMAGE)/cdimage.c $(CDIMAGE)/cdimage_cue.c -lm

$(OUT)/.stamp: make_images.py
	python3 -I make_images.py $(OUT) && touch $@

check: cdimage_test $(OUT)/.stamp
	./cdimage_test $(OUT)

clean:
	rm -rf cdimage_test $(OUT)
