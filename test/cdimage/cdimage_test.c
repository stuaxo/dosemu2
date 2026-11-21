/*
 * Host-side unit test for the cdimage layer, built from its sources only.
 *   cdimage_test IMGDIR     (IMGDIR from make_images.py)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cdimage.h"

static int failures, checks;

#define CHECK(cond) do { \
  checks++; \
  if (!(cond)) { \
    failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
  } \
} while (0)

static char dir[256];

static cdimage_t *open_img(const char *rel)
{
  char p[2048];

  snprintf(p, sizeof(p), "%s/%s", dir, rel);
  return cdimage_open(p);
}

static void expect_track(cdimage_t *img, int i, uint32_t start,
                         uint32_t idx1, uint32_t end, bool in_file,
                         bool audio)
{
  const cdimage_track_t *t = &img->tracks[i];

  CHECK(t->lba_start == start);
  CHECK(t->index1_lba == idx1);
  CHECK(t->end_lba == end);
  CHECK(t->pregap_in_file == in_file);
  CHECK(t->is_audio == audio);
  CHECK(t->num == i + 1);
}

static int sine_sample(double freq, uint32_t lba, int n)
{
  return (int) (12000 * sin(2 * M_PI * freq *
                            ((double) lba * CD_AUDIO_FRAMES + n) / 44100));
}

static void check_sine(cdimage_t *img, double freq, uint32_t lba,
                       uint32_t sine_lba0)
{
  int16_t pcm[CD_AUDIO_FRAMES * 2];
  int n, bad = 0, nonzero = 0;

  CHECK(cdimage_read_audio(img, lba, pcm) == 0);
  for (n = 0; n < CD_AUDIO_FRAMES; n++) {
    int want = sine_sample(freq, lba - sine_lba0, n);

    if (abs(pcm[2 * n] - want) > 1 || pcm[2 * n] != pcm[2 * n + 1])
      bad++;
    if (pcm[2 * n])
      nonzero++;
  }
  CHECK(bad == 0);
  CHECK(nonzero > 500);
}

static int all_zero(const void *p, size_t n)
{
  const unsigned char *c = p;

  while (n--)
    if (*c++)
      return 0;
  return 1;
}

static void test_helpers(void)
{
  uint8_t m, s, f;

  CHECK(cd_msf_to_lba(0, 2, 0) == 0);
  CHECK(cd_msf_to_lba(0, 0, 0) == 0);   /* clamped */
  CHECK(cd_msf_to_lba(1, 0, 0) == 60 * 75 - 150);
  cd_lba_to_msf(0, &m, &s, &f);
  CHECK(m == 0 && s == 2 && f == 0);
  cd_lba_to_msf(60 * 75 - 150, &m, &s, &f);
  CHECK(m == 1 && s == 0 && f == 0);
  cd_lba_to_msf(449999, &m, &s, &f);
  CHECK(cd_msf_to_lba(m, s, f) == 449999);
}

static void test_simple(void)
{
  cdimage_t *img = open_img("simple/simple.cue");
  uint8_t sec[2048];
  int16_t pcm[CD_AUDIO_FRAMES * 2];
  const cdimage_track_t *t;

  CHECK(img != NULL);
  if (!img)
    return;
  CHECK(img->ntracks == 2);
  CHECK(img->leadout_lba == 825);
  expect_track(img, 0, 0, 0, 300, false, false);
  /* synthetic PREGAP: lba_start 300, INDEX 01 at 450 (300 + 150) */
  expect_track(img, 1, 300, 450, 825, false, true);
  CHECK(img->tracks[0].mode == 1 && img->tracks[0].hdr_skip == 16);

  /* ISO 9660 primary volume descriptor in sector 16 */
  CHECK(cdimage_read_cooked(img, 16, sec) == 0);
  CHECK(sec[0] == 1 && !memcmp(sec + 1, "CD001", 5));
  CHECK(cdimage_read_cooked(img, 400, sec) != 0);       /* audio */

  t = cdimage_find_track(img, 299);
  CHECK(t && t->num == 1);
  t = cdimage_find_track(img, 300);
  CHECK(t && t->num == 2 && cdimage_is_synthetic(t, 300));
  CHECK(!cdimage_is_synthetic(t, 450));
  CHECK(cdimage_find_track(img, 825) == NULL);

  /* synthetic pregap is silent; real audio starts at 450 */
  CHECK(cdimage_read_audio(img, 320, pcm) == 0);
  CHECK(all_zero(pcm, sizeof(pcm)));
  check_sine(img, 440, 450, 450);
  check_sine(img, 440, 500, 450);
  cdimage_close(img);
}

static void test_hidden(void)
{
  cdimage_t *img = open_img("hidden/hidden.cue");

  CHECK(img != NULL);
  if (!img)
    return;
  CHECK(img->ntracks == 2);
  CHECK(img->leadout_lba == 825);
  expect_track(img, 0, 0, 150, 450, true, true);
  expect_track(img, 1, 450, 450, 825, true, true);
  /* hidden audio before INDEX 01 is real data from the BIN */
  check_sine(img, 220, 0, 0);
  check_sine(img, 220, 100, 0);
  check_sine(img, 440, 460, 450);
  cdimage_close(img);
}

static void test_qpos(void)
{
  cdimage_t *img = open_img("hidden/hidden.cue");
  cdimage_qpos_t q;

  CHECK(img != NULL);
  if (!img)
    return;
  /* hidden audio: index 0, counting down 150 .. 1 to INDEX 01 */
  cdimage_qpos(img, 0, &q);
  CHECK(q.track == 1 && q.index == 0 && q.rel_frames == 150);
  CHECK(q.ctrl_adr == 0x10);
  cdimage_qpos(img, 149, &q);
  CHECK(q.track == 1 && q.index == 0 && q.rel_frames == 1);
  cdimage_qpos(img, 150, &q);
  CHECK(q.track == 1 && q.index == 1 && q.rel_frames == 0);
  cdimage_qpos(img, 449, &q);
  CHECK(q.track == 1 && q.index == 1 && q.rel_frames == 299);
  cdimage_qpos(img, 450, &q);
  CHECK(q.track == 2 && q.index == 1 && q.rel_frames == 0);
  cdimage_qpos(img, 825, &q);
  CHECK(q.track == 0xAA && q.rel_frames == 0);
  cdimage_close(img);

  img = open_img("simple/simple.cue");
  CHECK(img != NULL);
  if (!img)
    return;
  cdimage_qpos(img, 16, &q);
  CHECK(q.track == 1 && q.index == 1 && q.ctrl_adr == 0x14);
  cdimage_qpos(img, 300, &q);   /* synthetic pregap of track 2 */
  CHECK(q.track == 2 && q.index == 0 && q.rel_frames == 150);
  CHECK(q.ctrl_adr == 0x10);
  cdimage_close(img);
}

static void test_multi(void)
{
  cdimage_t *img = open_img("multi/multi.cue"); /* CRLF, 2 BINs */
  uint8_t sec[2048];

  CHECK(img != NULL);
  if (!img)
    return;
  CHECK(img->ntracks == 2);
  expect_track(img, 0, 0, 0, 300, false, false);
  expect_track(img, 1, 300, 450, 825, true, true);
  CHECK(img->leadout_lba == 825);
  CHECK(cdimage_read_cooked(img, 16, sec) == 0);
  CHECK(!memcmp(sec + 1, "CD001", 5));
  check_sine(img, 440, 450, 450);
  cdimage_close(img);
}

static void test_errors(void)
{
  char p[1024];
  FILE *f;

  CHECK(open_img("nonexistent.cue") == NULL);
  CHECK(open_img("simple/simple.bin") == NULL); /* not a known format */

  snprintf(p, sizeof(p), "%s/bad.cue", dir);
  f = fopen(p, "w");
  fputs
      ("FILE \"missing.bin\" BINARY\n TRACK 01 AUDIO\n  INDEX 01 00:00:00\n",
       f);
  fclose(f);
  CHECK(cdimage_open(p) == NULL);
  f = fopen(p, "w");
  fputs("FILE \"simple/simple.bin\" BINARY\n"
        " TRACK 01 CDG\n  INDEX 01 00:00:00\n", f);
  fclose(f);
  CHECK(cdimage_open(p) == NULL);
  f = fopen(p, "w");
  fputs("FILE \"simple/simple.bin\" BINARY\n TRACK 01 AUDIO\n", f);
  fclose(f);
  CHECK(cdimage_open(p) == NULL);       /* sheet ends after TRACK 01 */
  remove(p);
}

static void test_mode2(void)
{
  uint8_t sec[CD_FRAME_COOKED];
  cdimage_t *img = open_img("mode2/mode2.cue");

  CHECK(img != NULL);
  if (!img)
    return;
  CHECK(img->tracks[0].mode == 2 && img->tracks[0].hdr_skip == 24);
  CHECK(cdimage_read_cooked(img, 0, sec) == 0 && sec[0] == 1 &&
        sec[2047] == 1);
  CHECK(cdimage_read_cooked(img, 1, sec) == 0 && sec[0] == 2);
  CHECK(cdimage_read_cooked(img, 2, sec) == -1);        /* Form 2 */
  CHECK(cdimage_read_cooked(img, 3, sec) == 0 && sec[0] == 4);
  cdimage_close(img);
}

static void test_probe(void)
{
  char p[1024];
  FILE *f;

  snprintf(p, sizeof(p), "%s/simple/simple.cue", dir);
  CHECK(cdimage_probe(p));
  snprintf(p, sizeof(p), "%s/simple/simple.bin", dir);
  CHECK(!cdimage_probe(p));     /* binary data */
  snprintf(p, sizeof(p), "%s/sheet.txt", dir);
  f = fopen(p, "w");
  fputs("REM made by hand\nFILE \"x.bin\" BINARY\n", f);
  fclose(f);
  CHECK(cdimage_probe(p));      /* by content, not by name */
  f = fopen(p, "w");
  fputs("hello\n", f);
  fclose(f);
  CHECK(!cdimage_probe(p));
  remove(p);
}

#ifdef HAVE_LIBCHDR
static void same_disc(cdimage_t *a, cdimage_t *b)
{
  uint32_t lba;
  int i;

  CHECK(a->ntracks == b->ntracks);
  CHECK(a->leadout_lba == b->leadout_lba);
  for (i = 0; i < a->ntracks && i < b->ntracks; i++) {
    const cdimage_track_t *x = &a->tracks[i], *y = &b->tracks[i];

    CHECK(x->num == y->num);
    CHECK(x->is_audio == y->is_audio);
    CHECK(x->mode == y->mode);
    CHECK(x->lba_start == y->lba_start);
    CHECK(x->index1_lba == y->index1_lba);
    CHECK(x->end_lba == y->end_lba);
    CHECK(x->first_stored_lba == y->first_stored_lba);
    /* meaningless for a zero-length pregap */
    if (x->index1_lba > x->lba_start)
      CHECK(x->pregap_in_file == y->pregap_in_file);
  }
  for (lba = 0; lba < a->leadout_lba; lba++) {
    int16_t pa[CD_AUDIO_FRAMES * 2], pb[CD_AUDIO_FRAMES * 2];
    uint8_t ca[2048], cb[2048];
    const cdimage_track_t *t = cdimage_find_track(a, lba);

    CHECK(cdimage_read_audio(a, lba, pa) == 0);
    CHECK(cdimage_read_audio(b, lba, pb) == 0);
    if (memcmp(pa, pb, sizeof(pa))) {
      CHECK(!"PCM differs");
      break;
    }
    if (t && !t->is_audio) {
      CHECK(cdimage_read_cooked(a, lba, ca) == 0);
      CHECK(cdimage_read_cooked(b, lba, cb) == 0);
      if (memcmp(ca, cb, sizeof(ca))) {
        CHECK(!"cooked data differs");
        break;
      }
    }
  }
}

static void test_chd(void)
{
  static const char *const names[] = { "simple/simple", "hidden/hidden" };
  char p[1024];
  size_t k;
  FILE *f;

  for (k = 0; k < 2; k++) {
    cdimage_t *cue, *chd;

    snprintf(p, sizeof(p), "%s.cue", names[k]);
    cue = open_img(p);
    snprintf(p, sizeof(p), "%s.chd", names[k]);
    chd = open_img(p);
    CHECK(cue != NULL);
    if (!chd) {
      fprintf(stderr, "SKIP: no %s (chdman missing?)\n", p);
      cdimage_close(cue);
      continue;
    }
    CHECK(chd->audio_big_endian && !cue->audio_big_endian);
    if (cue)
      same_disc(cue, chd);
    cdimage_close(cue);
    cdimage_close(chd);
  }

  /* hunk cache: re-reading the same sector decompresses the hunk once */
  {
    cdimage_t *chd = open_img("hidden/hidden.chd");
    int16_t pcm[CD_AUDIO_FRAMES * 2];
    unsigned n;
    uint32_t lba;

    if (chd) {
      CHECK(cdimage_read_audio(chd, 460, pcm) == 0);
      n = cdimage_chd_hunk_reads(chd);
      CHECK(n >= 1);
      CHECK(cdimage_read_audio(chd, 460, pcm) == 0);
      CHECK(cdimage_chd_hunk_reads(chd) == n);
      n = cdimage_chd_hunk_reads(chd);
      for (lba = 460; lba < 476; lba++)
        CHECK(cdimage_read_audio(chd, lba, pcm) == 0);
      CHECK(cdimage_chd_hunk_reads(chd) - n <= 3);
      cdimage_close(chd);
    }
  }

  /* a text file named .chd, then a truncated CHD */
  snprintf(p, sizeof(p), "%s/fake.chd", dir);
  f = fopen(p, "w");
  fputs("this is not a chd file\n", f);
  fclose(f);
  CHECK(cdimage_open(p) == NULL);
  {
    char src[1024], buf[4096];
    size_t n;

    snprintf(src, sizeof(src), "%s/hidden/hidden.chd", dir);
    f = fopen(src, "rb");
    if (f) {
      n = fread(buf, 1, sizeof(buf), f);
      fclose(f);
      f = fopen(p, "wb");
      fwrite(buf, 1, n, f);
      fclose(f);
      CHECK(cdimage_open(p) == NULL);
    }
  }
  remove(p);
}
#endif

#ifdef HAVE_SNDFILE
#include <sndfile.h>

#define AUD_SECTORS 525         /* multi/audio.bin: 150 silent + 375 sine */

/* multi/audio.bin as 16-bit stereo samples */
static int16_t *load_audio_bin(void)
{
  char p[1024];
  int16_t *pcm = malloc(AUD_SECTORS * CD_FRAME_RAW);
  uint8_t *b = malloc(AUD_SECTORS * CD_FRAME_RAW);
  FILE *f;
  size_t i;

  snprintf(p, sizeof(p), "%s/multi/audio.bin", dir);
  f = fopen(p, "rb");
  CHECK(f && fread(b, CD_FRAME_RAW, AUD_SECTORS, f) == AUD_SECTORS);
  if (f)
    fclose(f);
  for (i = 0; i < AUD_SECTORS * CD_FRAME_RAW / 2; i++)
    pcm[i] = (int16_t) (b[2 * i] | b[2 * i + 1] << 8);
  free(b);
  return pcm;
}

/* Writes frames sample pairs (one channel of them if channels == 1) */
static void write_audio(const char *name, int format, int rate, int channels,
                        const int16_t *pcm, sf_count_t frames)
{
  SF_INFO info = {.samplerate = rate,.channels = channels,.format = format };
  char p[1024];
  SNDFILE *sf;
  double q = 1.0;

  snprintf(p, sizeof(p), "%s/multi/%s", dir, name);
  sf = sf_open(p, SFM_WRITE, &info);
  CHECK(sf != NULL);
  if (!sf)
    return;
  sf_command(sf, SFC_SET_VBR_ENCODING_QUALITY, &q, sizeof(q));
  if (channels == 2) {
    CHECK(sf_writef_short(sf, pcm, frames) == frames);
  } else {
    sf_count_t i;

    for (i = 0; i < frames; i++)
      CHECK(sf_writef_short(sf, &pcm[i * 2], 1) == 1);
  }
  sf_close(sf);
}

/* The multi disc, with its audio track read from an audio file */
static cdimage_t *open_with(const char *cue, const char *file,
                            const char *type, const char *mode)
{
  char p[1024];
  FILE *f;

  snprintf(p, sizeof(p), "%s/multi/%s", dir, cue);
  f = fopen(p, "w");
  CHECK(f != NULL);
  if (!f)
    return NULL;
  fprintf(f, "FILE \"data.bin\" BINARY\r\n"
          "  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n"
          "FILE \"%s\" %s\r\n"
          "  TRACK 02 %s\r\n    INDEX 00 00:00:00\r\n"
          "    INDEX 01 00:02:00\r\n", file, type, mode);
  fclose(f);
  return cdimage_open(p);
}

/* Largest difference from the BIN over the audio track; -1 on a read
 * error or if the channels differ when mono is set. */
static int audio_diff(cdimage_t *img, const int16_t *ref, bool mono)
{
  int16_t pcm[CD_AUDIO_FRAMES * 2];
  int worst = 0, i;
  uint32_t lba;

  for (lba = 300; lba < 825; lba++) {
    const int16_t *r = ref + (lba - 300) * CD_AUDIO_FRAMES * 2;

    if (cdimage_read_audio(img, lba, pcm))
      return -1;
    for (i = 0; i < CD_AUDIO_FRAMES * 2; i++) {
      int want = mono ? r[i & ~1] : r[i];

      if (mono && pcm[i] != pcm[i ^ 1])
        return -1;
      if (abs(pcm[i] - want) > worst)
        worst = abs(pcm[i] - want);
    }
  }
  return worst;
}

static void test_audio_files(void)
{
  const sf_count_t frames = (sf_count_t) AUD_SECTORS * CD_AUDIO_FRAMES;
  int16_t *ref = load_audio_bin();
  int16_t pcm[CD_AUDIO_FRAMES * 2];
  cdimage_t *img;
  int d;

  write_audio("aud.wav", SF_FORMAT_WAV | SF_FORMAT_PCM_16, 44100, 2, ref,
              frames);
  write_audio("aud.flac", SF_FORMAT_FLAC | SF_FORMAT_PCM_16, 44100, 2,
              ref, frames);
  write_audio("aud.ogg", SF_FORMAT_OGG | SF_FORMAT_VORBIS, 44100, 2, ref,
              frames);
  write_audio("mono.wav", SF_FORMAT_WAV | SF_FORMAT_PCM_16, 44100, 1, ref,
              frames);
  write_audio("short.flac", SF_FORMAT_FLAC | SF_FORMAT_PCM_16, 44100, 2,
              ref, frames - 100);
  write_audio("slow.wav", SF_FORMAT_WAV | SF_FORMAT_PCM_16, 22050, 2, ref,
              frames);

  /* lossless files read back as the BIN; the type need not match */
  img = open_with("wav.cue", "aud.wav", "WAVE", "AUDIO");
  CHECK(img != NULL);
  if (img) {
    expect_track(img, 1, 300, 450, 825, true, true);
    CHECK(img->leadout_lba == 825);
    CHECK(audio_diff(img, ref, false) == 0);
    check_sine(img, 440, 450, 450);
    cdimage_close(img);
  }
  img = open_with("flac.cue", "aud.flac", "MP3", "AUDIO");
  CHECK(img != NULL);
  if (img) {
    expect_track(img, 1, 300, 450, 825, true, true);
    CHECK(audio_diff(img, ref, false) == 0);
    cdimage_close(img);
  }

  /* Vorbis is lossy: the same length, and close to the BIN */
  img = open_with("ogg.cue", "aud.ogg", "MP3", "AUDIO");
  CHECK(img != NULL);
  if (img) {
    expect_track(img, 1, 300, 450, 825, true, true);
    d = audio_diff(img, ref, false);
    CHECK(d >= 0 && d < 600);
    cdimage_close(img);
  }

  /* mono plays on both channels */
  img = open_with("mono.cue", "mono.wav", "WAVE", "AUDIO");
  CHECK(img != NULL);
  if (img) {
    CHECK(audio_diff(img, ref, true) == 0);
    cdimage_close(img);
  }

  /* a partial last sector is padded with silence */
  img = open_with("short.cue", "short.flac", "WAVE", "AUDIO");
  CHECK(img != NULL);
  if (img) {
    expect_track(img, 1, 300, 450, 825, true, true);
    CHECK(cdimage_read_audio(img, 824, pcm) == 0);
    CHECK(!memcmp(pcm, ref + 524 * CD_AUDIO_FRAMES * 2,
                  (CD_AUDIO_FRAMES - 100) * 4));
    CHECK(all_zero(pcm + (CD_AUDIO_FRAMES - 100) * 2, 100 * 4));
    cdimage_close(img);
  }

  /* only 44100 Hz; no data tracks; a BIN is not an audio file */
  CHECK(open_with("slow.cue", "slow.wav", "WAVE", "AUDIO") == NULL);
  CHECK(open_with("data.cue", "aud.wav", "WAVE", "MODE1/2352") == NULL);
  CHECK(open_with("bin.cue", "audio.bin", "MOTOROLA", "AUDIO") == NULL);
  free(ref);
}
#endif

int main(int argc, char **argv)
{
  if (argc != 2) {
    fprintf(stderr, "usage: %s IMGDIR\n", argv[0]);
    return 2;
  }
  if (strlen(argv[1]) >= sizeof(dir))
    return 2;
  strcpy(dir, argv[1]);
  test_helpers();
  test_simple();
  test_hidden();
  test_multi();
  test_qpos();
  test_errors();
  test_mode2();
  test_probe();
#ifdef HAVE_LIBCHDR
  test_chd();
#else
  fprintf(stderr, "SKIP: built without libchdr\n");
#endif
#ifdef HAVE_SNDFILE
  test_audio_files();
#else
  fprintf(stderr, "SKIP: built without libsndfile\n");
#endif
  printf("%d checks, %d failures\n", checks, failures);
  return failures != 0;
}
