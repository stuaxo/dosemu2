/*
 * cd_image.c - backend-independent parts of the CD image layer.
 */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "cd_image.h"
#ifndef CD_IMAGE_STANDALONE
#include "utilities.h"
#endif

const cd_track_t *cd_image_find_track(const cd_image_t *img, uint32_t lba)
{
	int i;

	for (i = img->ntracks - 1; i >= 0; i--) {
		const cd_track_t *t = &img->tracks[i];

		if (lba >= t->lba_start)
			return lba < t->end_lba ? t : NULL;
	}
	return NULL;
}

bool cd_image_is_synthetic(const cd_track_t *t, uint32_t lba)
{
	return lba < t->first_stored_lba;
}

int cd_image_read_cooked(cd_image_t *img, uint32_t lba, uint8_t *out)
{
	const cd_track_t *t = cd_image_find_track(img, lba);
	uint8_t frame[CD_FRAME_RAW];

	if (!t || t->is_audio)
		return -1;
	if (cd_image_is_synthetic(t, lba)) {
		memset(out, 0, CD_FRAME_COOKED);
		return 0;
	}
	if (img->read_frame(img, t, lba, frame))
		return -1;
	/* A Mode 2 sector has a subheader just before the user data. Form 2
	 * sectors hold 2324 bytes, so there are no 2048 to return. */
	if (t->mode == 2 && t->hdr_skip >= 8 && (frame[t->hdr_skip - 6] & 0x20))
		return -1;
	memcpy(out, frame + t->hdr_skip, CD_FRAME_COOKED);
	return 0;
}

int cd_image_read_audio(cd_image_t *img, uint32_t lba, int16_t *pcm)
{
	const cd_track_t *t = cd_image_find_track(img, lba);
	uint8_t frame[CD_FRAME_RAW];
	int i;

	if (!t || !t->is_audio || cd_image_is_synthetic(t, lba)) {
		memset(pcm, 0, CD_AUDIO_FRAMES * 2 * sizeof(*pcm));
		return 0;
	}
	if (img->read_frame(img, t, lba, frame))
		return -1;
	/* Assemble samples explicitly so the result is host-endian whatever
	 * the host is.  BIN/CUE stores little-endian samples; CHD stores them
	 * big-endian.  Getting this wrong sounds like loud white noise. */
	for (i = 0; i < CD_AUDIO_FRAMES * 2; i++) {
		const uint8_t *p = frame + i * 2;

		pcm[i] = img->audio_big_endian ?
			(int16_t)((p[0] << 8) | p[1]) :
			(int16_t)((p[1] << 8) | p[0]);
	}
	return 0;
}

uint32_t cd_msf_to_lba(uint8_t m, uint8_t s, uint8_t f)
{
	uint32_t n = ((uint32_t)m * 60 + s) * 75 + f;

	return n < CD_LEADIN ? 0 : n - CD_LEADIN;
}

void cd_lba_to_msf(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f)
{
	uint32_t n = lba + CD_LEADIN;

	*m = n / (60 * 75);
	*s = (n / 75) % 60;
	*f = n % 75;
}

#define MAX_FORMATS	8
static const struct cd_format_ops *formats[MAX_FORMATS];
static int nformats;

void cd_format_register(const struct cd_format_ops *ops)
{
	int i;

	for (i = 0; i < nformats; i++)
		if (formats[i] == ops)
			return;
	if (nformats >= MAX_FORMATS) {
		cd_log("too many image formats, ignoring %s\n", ops->name);
		return;
	}
	formats[nformats++] = ops;
}

static void init_formats(void)
{
	static bool done;

	if (done)
		return;
	done = true;
	cd_format_register(&cd_cue_format);
	/* other formats come from plugins, which register themselves */
#if !defined(CD_IMAGE_STANDALONE) && defined(USE_DL_PLUGINS) && defined(USE_CHD)
	load_plugin("chd");
#endif
}

static const struct cd_format_ops *find_format(const char *path)
{
	int i;

	init_formats();
	for (i = 0; i < nformats; i++)
		if (formats[i]->probe(path))
			return formats[i];
	return NULL;
}

bool cd_image_probe(const char *path)
{
	return find_format(path) != NULL;
}

cd_image_t *cd_image_open(const char *path)
{
	const struct cd_format_ops *ops = find_format(path);

	if (!ops) {
		cd_log("%s: not a known image format\n", path);
		return NULL;
	}
	return ops->open(path);
}

void cd_image_close(cd_image_t *img)
{
	if (img)
		img->close(img);
}
