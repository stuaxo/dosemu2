# Host-side tests for the cd_image layer: make -C test/cdimg check
DRV = ../../src/dosext/drivers
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu17 -Wall -Wextra -Wstrict-prototypes -DCD_IMAGE_STANDALONE -I$(DRV)
OUT ?= out

cd_image_test: cd_image_test.c $(DRV)/cd_image.c $(DRV)/cd_cue.c $(DRV)/cd_image.h
	$(CC) $(CFLAGS) -o $@ cd_image_test.c $(DRV)/cd_image.c $(DRV)/cd_cue.c -lm

$(OUT)/.stamp: make_images.py
	python3 -I make_images.py $(OUT) && touch $@

check: cd_image_test $(OUT)/.stamp
	./cd_image_test $(OUT)

clean:
	rm -rf cd_image_test $(OUT)
