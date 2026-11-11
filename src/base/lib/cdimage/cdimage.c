/*
 * cdimage.c - backend-independent parts of the CD image layer.
 */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "cdimage.h"

const cdimage_track_t *cdimage_find_track(const cdimage_t *img,
                                          uint32_t lba)
{
  int i;

  for (i = img->ntracks - 1; i >= 0; i--) {
    const cdimage_track_t *t = &img->tracks[i];

    if (lba >= t->lba_start)
      return lba < t->end_lba ? t : NULL;
  }
  return NULL;
}

bool cdimage_is_synthetic(const cdimage_track_t *t, uint32_t lba)
{
  return lba < t->first_stored_lba;
}

int cdimage_read_cooked(cdimage_t *img, uint32_t lba, uint8_t *out)
{
  const cdimage_track_t *t = cdimage_find_track(img, lba);
  uint8_t frame[CD_FRAME_RAW];

  if (!t || t->is_audio)
    return -1;
  if (cdimage_is_synthetic(t, lba)) {
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

int cdimage_read_audio(cdimage_t *img, uint32_t lba, int16_t *pcm)
{
  const cdimage_track_t *t = cdimage_find_track(img, lba);
  uint8_t frame[CD_FRAME_RAW];
  int i;

  if (!t || !t->is_audio || cdimage_is_synthetic(t, lba)) {
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
        (int16_t) ((p[0] << 8) | p[1]) : (int16_t) ((p[1] << 8) | p[0]);
  }
  return 0;
}

void cdimage_qpos(const cdimage_t *img, uint32_t lba, cdimage_qpos_t *q)
{
  const cdimage_track_t *t = cdimage_find_track(img, lba);

  q->abs_lba = lba;
  if (!t) {
    const cdimage_track_t *last = &img->tracks[img->ntracks - 1];

    if (lba >= img->leadout_lba) {
      q->track = 0xAA;
      q->index = 1;
      q->rel_frames = lba - img->leadout_lba;
      q->ctrl_adr = 0x10 | (last->is_audio ? 0 : 4);
      return;
    }
    t = &img->tracks[0];        /* before the first INDEX 00 */
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

#define MAX_FORMATS     8
static const struct cdimage_format_ops *formats[MAX_FORMATS];
static int nformats;

void cdimage_format_register(const struct cdimage_format_ops *ops)
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
  cdimage_format_register(&cdimage_cue_format);
}

static const struct cdimage_format_ops *find_format(const char *path)
{
  int i;

  init_formats();
  for (i = 0; i < nformats; i++)
    if (formats[i]->probe(path))
      return formats[i];
  return NULL;
}

bool cdimage_probe(const char *path)
{
  return find_format(path) != NULL;
}

cdimage_t *cdimage_open(const char *path)
{
  const struct cdimage_format_ops *ops = find_format(path);

  if (!ops) {
    cd_log("%s: not a known image format\n", path);
    return NULL;
  }
  return ops->open(path);
}

void cdimage_close(cdimage_t *img)
{
  if (img)
    img->close(img);
}
