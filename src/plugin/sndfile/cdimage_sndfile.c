/*
 * cdimage_sndfile.c - audio files as CD tracks for cdimage, as a plugin;
 * depends on libsndfile (WAV, FLAC, Ogg Vorbis, Opus, MP3).
 */
#include <stdlib.h>
#include <sndfile.h>
#include "init.h"
#include "cdimage.h"

typedef struct {
  SNDFILE *sf;
  int channels;
  sf_count_t pos;               /* the next frame sf_readf_short() gives */
} sndfile_t;

static void *sndfile_open(const char *path, uint64_t *frames)
{
  SF_INFO info = { 0 };
  SNDFILE *sf = sf_open(path, SFM_READ, &info);
  sndfile_t *h;

  if (!sf) {
    cd_log("%s: %s\n", path, sf_strerror(NULL));
    return NULL;
  }
  if (info.samplerate != 44100 || info.channels < 1 || info.channels > 2 ||
      !info.seekable || info.frames < 0) {
    cd_log("%s: %d Hz, %d channels: only 44100 Hz mono or stereo\n",
           path, info.samplerate, info.channels);
    sf_close(sf);
    return NULL;
  }
  h = malloc(sizeof(*h));
  if (!h) {
    sf_close(sf);
    return NULL;
  }
  h->sf = sf;
  h->channels = info.channels;
  h->pos = 0;
  *frames = info.frames;
  return h;
}

static int sndfile_read(void *hv, uint64_t pos, int16_t *pcm, unsigned n)
{
  sndfile_t *h = hv;
  sf_count_t got;

  /* playback and raw reads go forwards, so seek only on a jump */
  if ((sf_count_t) pos != h->pos) {
    if (sf_seek(h->sf, pos, SEEK_SET) < 0)
      return -1;
    h->pos = pos;
  }
  got = sf_readf_short(h->sf, pcm, n);
  if (got < 0)
    return -1;
  h->pos += got;
  if (h->channels == 1) {
    sf_count_t i;

    for (i = got - 1; i >= 0; i--)
      pcm[i * 2] = pcm[i * 2 + 1] = pcm[i];
  }
  return got;
}

static void sndfile_close(void *hv)
{
  sndfile_t *h = hv;

  sf_close(h->sf);
  free(h);
}

static const struct cdimage_audio_ops sndfile_audio = {
  .name = "libsndfile",
  .open = sndfile_open,
  .read = sndfile_read,
  .close = sndfile_close,
};

CONSTRUCTOR(static void sndfile_register(void))
{
  cdimage_audio_register(&sndfile_audio);
}
