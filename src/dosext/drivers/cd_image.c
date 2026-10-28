/*
 * cd_image.c - backend-independent parts of the CD image layer.
 */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "cd_image.h"

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

void cd_image_qpos(const cd_image_t *img, uint32_t lba, cd_qpos_t *q)
{
	const cd_track_t *t = cd_image_find_track(img, lba);

	q->abs_lba = lba;
	if (!t) {
		const cd_track_t *last = &img->tracks[img->ntracks - 1];

		if (lba >= img->leadout_lba) {
			q->track = 0xAA;
			q->index = 1;
			q->rel_frames = lba - img->leadout_lba;
			q->ctrl_adr = 0x10 | (last->is_audio ? 0 : 4);
			return;
		}
		t = &img->tracks[0];	/* before the first INDEX 00 */
	}
	q->track = t->num;
	q->ctrl_adr = 0x10 | (t->is_audio ? 0 : 4);
	if (lba < t->index1_lba) {
		q->index = 0;
		q->rel_frames = t->index1_lba - lba;
	} else {
		q->index = 1;
		q->rel_frames = lba - t->index1_lba;
	}
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

cd_image_t *cd_image_open(const char *path)
{
	const char *ext = strrchr(path, '.');

	if (ext && !strcasecmp(ext, ".cue"))
		return cd_cue_open(path);
	if (ext && !strcasecmp(ext, ".chd")) {
#ifdef HAVE_LIBCHDR
		return cd_chd_open(path);
#else
		cd_log("CHD support not compiled in (libchdr missing)\n");
		return NULL;
#endif
	}
	cd_log("%s: unsupported image type\n", path);
	return NULL;
}

void cd_image_close(cd_image_t *img)
{
	if (img)
		img->close(img);
}
