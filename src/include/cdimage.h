/*
 * cdimage.h - CD image abstraction for the MSCDEX/cdrom helper.
 *
 * A cdimage presents a disc as a list of tracks addressed by LBA, where
 * LBA 0 is MSF 00:02:00 (the same convention cdrom.c uses for HSG sector
 * numbers).  A format (BIN/CUE built in, others from plugins) implements
 * probe() and open(), and sets read_frame() on the image; cooking, silence
 * synthesis and PCM conversion live in cdimage.c.
 *
 * The cdimage code is in src/base/lib/cdimage and knows nothing of DOS;
 * the cdrom_* files in src/dosext/drivers answer DOS requests with it.
 *
 * This header and the built-in format build into a stand-alone test (define
 * CDIMAGE_STANDALONE) that links the cdimage sources only.
 */
#ifndef CDIMAGE_H
#define CDIMAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "redbook.h"

#define CD_FRAME_RAW    2352
#define CD_FRAME_COOKED 2048
#define CD_AUDIO_FRAMES 588     /* stereo sample pairs per sector */
#define CDI_MAX_TRACKS  99

typedef struct {
  uint8_t num;                  /* 1-based track number */
  bool is_audio;
  uint8_t mode;                 /* 1 or 2 for data tracks, 0 for audio */
  uint32_t lba_start;           /* absolute LBA of INDEX 00 */
  uint32_t index1_lba;          /* absolute LBA of INDEX 01 (TOC start) */
  uint32_t end_lba;             /* exclusive, includes any postgap */
  bool pregap_in_file;          /* false: [lba_start,index1_lba) is silent */
  uint32_t sector_size;         /* stored bytes per sector: 2048/2336/2352 */
  uint32_t hdr_skip;            /* offset of the user data in a sector */
  uint32_t first_stored_lba;    /* first LBA that is really stored */
} cdimage_track_t;

typedef struct cdimage cdimage_t;

struct cdimage {
  int ntracks;
  cdimage_track_t tracks[CDI_MAX_TRACKS];
  uint32_t leadout_lba;
  bool audio_big_endian;        /* byte order of stored CDDA samples */

  /* Read one stored sector of track t (t->sector_size bytes) into out,
   * which must hold CD_FRAME_RAW bytes.  Called for stored
   * LBAs only (see cdimage_is_synthetic).  0 on success. */
  int (*read_frame)(cdimage_t * img, const cdimage_track_t * t,
                    uint32_t lba, uint8_t * out);
  /* Frees the backend state and the cdimage itself. */
  void (*close)(cdimage_t * img);
  void *priv;
};

/* A disc image format.  Formats register themselves; cdimage_open() asks
 * each one in turn to probe() the file. */
struct cdimage_format_ops {
  const char *name;
  /* true if the file is in this format; must be cheap */
  bool (*probe)(const char *path);
  /* NULL on failure; sets read_frame() and close() on the image */
  cdimage_t *(*open)(const char *path);
};
void cdimage_format_register(const struct cdimage_format_ops *ops);

/* True if a registered format claims the file. */
bool cdimage_probe(const char *path);
/* Opens an image in whichever format claims it. NULL on failure. */
cdimage_t *cdimage_open(const char *path);
void cdimage_close(cdimage_t * img);

/* Pure helpers that work on their arguments */
const cdimage_track_t *cdimage_find_track(const cdimage_t * img,
                                          uint32_t lba);
bool cdimage_is_synthetic(const cdimage_track_t * t, uint32_t lba);

/* 0 on success.  Synthetic ranges read as zeros. */
int cdimage_read_cooked(cdimage_t * img, uint32_t lba, uint8_t * out2048);
/* Always fills out; lba outside any audio data gives silence. */
int cdimage_read_audio(cdimage_t * img, uint32_t lba,
                       int16_t * pcm /* 588*2 */ );

/* The built-in BIN/CUE format */
extern const struct cdimage_format_ops cdimage_cue_format;

#ifdef CDIMAGE_STANDALONE
#include <stdio.h>
#define cd_log(...) fprintf(stderr, "cdimage: " __VA_ARGS__)
#else
#include "emu.h"
#define cd_log(...) C_printf("CDIMAGE: " __VA_ARGS__)
#endif

#endif                          /* CDIMAGE_H */
