/*
 * cd_chd.c - MAME CHD backend for the cd_image layer (needs libchdr).
 *
 * libchdr only reads hunks and metadata; the track table is parsed here.
 * Facts about the format that matter:
 *  - every frame is CD_FRAME_SIZE (2448) bytes in the file, the track's data
 *    (2048/2336/2352) is at the start of it;
 *  - each track's stored frames are padded up to a multiple of
 *    CD_TRACK_PADDING; the pad is not part of the disc;
 *  - a pregap is stored in the file only if PGTYPE starts with 'V', and then
 *    FRAMES includes it; otherwise it is virtual and FRAMES excludes it;
 *  - audio samples are stored big-endian.
 * libchdr is not thread-safe; dosemu calls the cdrom helper from one thread.
 */
#ifdef HAVE_LIBCHDR
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <libchdr/chd.h>
#include <libchdr/cdrom.h>
#include "cd_image.h"

typedef struct {
	uint32_t frame0;	/* CHD frame of the first stored sector */
} chd_trk_t;

typedef struct {
	chd_file *chd;
	uint8_t  *hunk;
	uint32_t  frames_per_hunk;
	uint32_t  cached;	/* hunk number in ->hunk, UINT32_MAX if none */
	unsigned  nreads;	/* chd_read() calls, for tests */
	chd_trk_t trk[CDI_MAX_TRACKS];
} chd_priv_t;

unsigned cd_chd_hunk_reads(const cd_image_t *img)
{
	return ((const chd_priv_t *)img->priv)->nreads;
}

static int chd_get_hunk(chd_priv_t *p, uint32_t n)
{
	if (p->cached == n)
		return 0;
	if (chd_read(p->chd, n, p->hunk) != CHDERR_NONE) {
		p->cached = UINT32_MAX;
		return -1;
	}
	p->nreads++;
	p->cached = n;
	return 0;
}

static int chd_read_frame(cd_image_t *img, const cd_track_t *t, uint32_t lba,
			  uint8_t *out)
{
	chd_priv_t *p = img->priv;
	uint32_t frame;

	if (lba < t->first_stored_lba)
		return -1;
	frame = p->trk[t->num - 1].frame0 + (lba - t->first_stored_lba);
	if (chd_get_hunk(p, frame / p->frames_per_hunk))
		return -1;
	memcpy(out, p->hunk + (frame % p->frames_per_hunk) * CD_FRAME_SIZE,
	       t->sector_size);
	return 0;
}

static void chd_close_image(cd_image_t *img)
{
	chd_priv_t *p = img->priv;

	if (p->chd)
		chd_close(p->chd);
	free(p->hunk);
	free(p);
	free(img);
}

static int type_info(const char *s, cd_track_t *t)
{
	t->is_audio = false;
	t->mode = 1;
	t->hdr_skip = 0;
	if (!strcasecmp(s, "AUDIO")) {
		t->is_audio = true;
		t->mode = 0;
		t->sector_size = CD_MAX_SECTOR_DATA;
	} else if (!strcasecmp(s, "MODE1")) {
		t->sector_size = 2048;
	} else if (!strcasecmp(s, "MODE1_RAW")) {
		t->sector_size = CD_MAX_SECTOR_DATA;
		t->hdr_skip = 16;
	} else if (!strcasecmp(s, "MODE2_FORM1")) {
		t->mode = 2;
		t->sector_size = 2048;
	} else if (!strcasecmp(s, "MODE2") || !strcasecmp(s, "MODE2_FORM_MIX")) {
		t->mode = 2;
		t->sector_size = 2336;
		t->hdr_skip = 8;
	} else if (!strcasecmp(s, "MODE2_RAW")) {
		t->mode = 2;
		t->sector_size = CD_MAX_SECTOR_DATA;
		t->hdr_skip = 24;
	} else {
		return -1;
	}
	return 0;
}

/* Returns the number of tracks or -1. */
static int parse_tracks(chd_file *chd, cd_image_t *img, chd_priv_t *p)
{
	uint32_t disc = 0, next_frame = 0;
	char buf[512];
	uint32_t len;
	uint32_t tag = CDROM_TRACK_METADATA2_TAG;
	int n;

	if (chd_get_metadata(chd, GDROM_TRACK_METADATA_TAG, 0, buf,
			     sizeof(buf), &len, NULL, NULL) == CHDERR_NONE) {
		cd_log("GD-ROM CHDs are not supported\n");
		return -1;
	}
	if (chd_get_metadata(chd, tag, 0, buf, sizeof(buf), &len, NULL, NULL)
			!= CHDERR_NONE)
		tag = CDROM_TRACK_METADATA_TAG;	/* version 1 metadata */

	for (n = 0; n < CDI_MAX_TRACKS; n++) {
		cd_track_t *t = &img->tracks[n];
		char type[32] = "", sub[32] = "", pgtype[32] = "", pgsub[32] = "";
		int num = 0, frames = 0, pregap = 0, postgap = 0, got;
		bool stored_pg;
		uint32_t virt_pg;

		if (chd_get_metadata(chd, tag, n, buf, sizeof(buf) - 1, &len,
				     NULL, NULL) != CHDERR_NONE)
			break;
		buf[len < sizeof(buf) ? len : sizeof(buf) - 1] = '\0';
		if (tag == CDROM_TRACK_METADATA2_TAG)
			got = sscanf(buf, "TRACK:%d TYPE:%31s SUBTYPE:%31s "
				     "FRAMES:%d PREGAP:%d PGTYPE:%31s "
				     "PGSUB:%31s POSTGAP:%d", &num, type, sub,
				     &frames, &pregap, pgtype, pgsub, &postgap);
		else
			got = sscanf(buf, "TRACK:%d TYPE:%31s SUBTYPE:%31s "
				     "FRAMES:%d", &num, type, sub, &frames);
		if ((tag == CDROM_TRACK_METADATA2_TAG ? got != 8 : got != 4) ||
				num != n + 1 || frames <= 0 || pregap < 0 ||
				postgap < 0 || type_info(type, t)) {
			cd_log("bad CHD track metadata: %s\n", buf);
			return -1;
		}
		stored_pg = pregap > 0 && (pgtype[0] == 'V' || pgtype[0] == 'v');
		virt_pg = stored_pg ? 0 : (uint32_t)pregap;

		t->num = n + 1;
		t->lba_start = disc;
		t->index1_lba = disc + pregap;
		t->end_lba = disc + virt_pg + frames + postgap;
		t->pregap_in_file = stored_pg;
		t->first_stored_lba = stored_pg ? t->lba_start : t->index1_lba;
		p->trk[n].frame0 = next_frame;
		next_frame += ((uint32_t)frames + CD_TRACK_PADDING - 1) /
			CD_TRACK_PADDING * CD_TRACK_PADDING;
		disc = t->end_lba;
	}
	if (!n) {
		cd_log("no CD track metadata in CHD\n");
		return -1;
	}
	return n;
}

cd_image_t *cd_chd_open(const char *path)
{
	chd_file *chd = NULL;
	const chd_header *hdr;
	chd_priv_t *p;
	cd_image_t *img;
	int i;

	if (chd_open(path, CHD_OPEN_READ, NULL, &chd) != CHDERR_NONE) {
		cd_log("%s: not a readable CHD\n", path);
		return NULL;
	}
	hdr = chd_get_header(chd);
	if (hdr->unitbytes != CD_FRAME_SIZE ||
			hdr->hunkbytes % CD_FRAME_SIZE) {
		cd_log("%s: CHD is not a CD image\n", path);
		chd_close(chd);
		return NULL;
	}
	img = calloc(1, sizeof(*img));
	p = calloc(1, sizeof(*p));
	if (!img || !p) {
		free(img);
		free(p);
		chd_close(chd);
		return NULL;
	}
	p->chd = chd;
	p->cached = UINT32_MAX;
	p->frames_per_hunk = hdr->hunkbytes / CD_FRAME_SIZE;
	p->hunk = malloc(hdr->hunkbytes);
	img->priv = p;
	img->read_frame = chd_read_frame;
	img->close = chd_close_image;
	img->audio_big_endian = true;
	if (!p->hunk)
		goto fail;

	img->ntracks = parse_tracks(chd, img, p);
	if (img->ntracks < 0)
		goto fail;
	img->leadout_lba = img->tracks[img->ntracks - 1].end_lba;

	/* A libchdr built without WANT_RAW_DATA_SECTOR does not rebuild the
	 * sync/header of raw sectors, which would make cooked reads garbage. */
	for (i = 0; i < img->ntracks; i++) {
		static const uint8_t sync[12] = { 0, 255, 255, 255, 255, 255,
			255, 255, 255, 255, 255, 0 };
		const cd_track_t *t = &img->tracks[i];
		uint8_t fr[CD_FRAME_RAW];

		if (t->is_audio || t->hdr_skip < 16)
			continue;
		if (chd_read_frame(img, t, t->first_stored_lba, fr) ||
				memcmp(fr, sync, sizeof(sync))) {
			cd_log("%s: raw data sectors lack a sync pattern "
			       "(libchdr built without raw sector support?)\n",
			       path);
			goto fail;
		}
		break;
	}
	return img;

fail:
	chd_close_image(img);
	return NULL;
}
#endif /* HAVE_LIBCHDR */
