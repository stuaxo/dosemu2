/*
 * cd_image.h - CD image abstraction for the MSCDEX/cdrom helper.
 *
 * A cd_image presents a disc (BIN/CUE, CHD) as a list of tracks addressed by
 * LBA, where LBA 0 is MSF 00:02:00 (the same convention cdrom.c uses for HSG
 * sector numbers).  Backends implement a single op, read_frame(); cooking,
 * silence synthesis and PCM conversion live in cd_image.c.
 *
 * This header and the backends must not depend on the rest of dosemu so they
 * can be built into a stand-alone test (define CD_IMAGE_STANDALONE).
 */
#ifndef CD_IMAGE_H
#define CD_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#define CD_FRAME_RAW	2352
#define CD_FRAME_COOKED	2048
#define CD_AUDIO_FRAMES	588		/* stereo sample pairs per sector */
#define CD_LEADIN	150		/* LBA 0 == MSF 00:02:00 */
#define CDI_MAX_TRACKS	99

typedef struct {
	uint8_t  num;			/* 1-based track number */
	bool     is_audio;
	uint8_t  mode;			/* 1 or 2 for data tracks, 0 for audio */
	uint32_t lba_start;		/* absolute LBA of INDEX 00 */
	uint32_t index1_lba;		/* absolute LBA of INDEX 01 (TOC start) */
	uint32_t end_lba;		/* exclusive, includes any postgap */
	bool     pregap_in_file;	/* false: [lba_start,index1_lba) is silent */
	uint32_t sector_size;		/* stored bytes per sector: 2048/2336/2352 */
	uint32_t hdr_skip;		/* offset of the 2048 user bytes in a stored sector */
	uint32_t first_stored_lba;	/* first LBA that is really stored */
} cd_track_t;

typedef struct cd_image cd_image_t;

struct cd_image {
	int        ntracks;
	cd_track_t tracks[CDI_MAX_TRACKS];
	uint32_t   leadout_lba;
	bool       audio_big_endian;	/* byte order of stored CDDA samples */

	/* Read one stored sector of track t (t->sector_size bytes) into out,
	 * which must hold CD_FRAME_RAW bytes.  Never called for LBAs that
	 * are synthetic (see cd_image_is_synthetic).  0 on success. */
	int  (*read_frame)(cd_image_t *img, const cd_track_t *t, uint32_t lba,
			   uint8_t *out);
	/* Frees the backend state and the cd_image itself. */
	void (*close)(cd_image_t *img);
	void *priv;
};

/* Opens an image, choosing the backend by extension. NULL on failure. */
cd_image_t *cd_image_open(const char *path);
void cd_image_close(cd_image_t *img);

/* Pure helpers, no global state */
const cd_track_t *cd_image_find_track(const cd_image_t *img, uint32_t lba);
bool cd_image_is_synthetic(const cd_track_t *t, uint32_t lba);

/* 0 on success.  Synthetic ranges read as zeros. */
int cd_image_read_cooked(cd_image_t *img, uint32_t lba, uint8_t *out2048);
/* Always fills out; lba outside any audio data gives silence. */
int cd_image_read_audio(cd_image_t *img, uint32_t lba,
			int16_t *pcm /* 588*2 */);

/* Sub-channel Q data for the head at lba. */
typedef struct {
	uint8_t  ctrl_adr;	/* (ADR << 4) | control: 0x10 audio, 0x14 data */
	uint8_t  track;		/* 1..n, or 0xAA at/after the lead-out */
	uint8_t  index;		/* 0 inside the pregap, 1 from INDEX 01 on */
	uint32_t rel_frames;	/* distance to INDEX 01: counts down through
				 * the pregap to 0, then up through the track */
	uint32_t abs_lba;
} cd_qpos_t;
void cd_image_qpos(const cd_image_t *img, uint32_t lba, cd_qpos_t *q);

uint32_t cd_msf_to_lba(uint8_t m, uint8_t s, uint8_t f);
void cd_lba_to_msf(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f);

/* Backends */
cd_image_t *cd_cue_open(const char *path);
#ifdef HAVE_LIBCHDR
cd_image_t *cd_chd_open(const char *path);
/* number of hunk decompressions so far (for tests) */
unsigned cd_chd_hunk_reads(const cd_image_t *img);
#endif

#ifdef CD_IMAGE_STANDALONE
#include <stdio.h>
#define cd_log(...) fprintf(stderr, "cd_image: " __VA_ARGS__)
#else
#include "emu.h"
#define cd_log(...) C_printf("CDIMG: " __VA_ARGS__)
#endif

#endif /* CD_IMAGE_H */
