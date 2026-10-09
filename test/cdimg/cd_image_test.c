/*
 * Host-side unit test for the cd_image layer.  No DOS, no dosemu headers.
 *   cd_image_test IMGDIR     (IMGDIR from make_images.py)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cd_image.h"

static int failures, checks;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

static char dir[512];

static cd_image_t *open_img(const char *rel)
{
	char p[1024];

	snprintf(p, sizeof(p), "%s/%s", dir, rel);
	return cd_image_open(p);
}

static void expect_track(cd_image_t *img, int i, uint32_t start,
			 uint32_t idx1, uint32_t end, bool in_file, bool audio)
{
	const cd_track_t *t = &img->tracks[i];

	CHECK(t->lba_start == start);
	CHECK(t->index1_lba == idx1);
	CHECK(t->end_lba == end);
	CHECK(t->pregap_in_file == in_file);
	CHECK(t->is_audio == audio);
	CHECK(t->num == i + 1);
}

static int sine_sample(double freq, uint32_t lba, int n)
{
	return (int)(12000 * sin(2 * M_PI * freq *
		((double)lba * CD_AUDIO_FRAMES + n) / 44100));
}

static void check_sine(cd_image_t *img, double freq, uint32_t lba,
		       uint32_t sine_lba0)
{
	int16_t pcm[CD_AUDIO_FRAMES * 2];
	int n, bad = 0, nonzero = 0;

	CHECK(cd_image_read_audio(img, lba, pcm) == 0);
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
	CHECK(cd_msf_to_lba(0, 0, 0) == 0);	/* clamped */
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
	cd_image_t *img = open_img("simple/simple.cue");
	uint8_t sec[2048];
	int16_t pcm[CD_AUDIO_FRAMES * 2];
	const cd_track_t *t;

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
	CHECK(cd_image_read_cooked(img, 16, sec) == 0);
	CHECK(sec[0] == 1 && !memcmp(sec + 1, "CD001", 5));
	CHECK(cd_image_read_cooked(img, 400, sec) != 0);	/* audio */

	t = cd_image_find_track(img, 299);
	CHECK(t && t->num == 1);
	t = cd_image_find_track(img, 300);
	CHECK(t && t->num == 2 && cd_image_is_synthetic(t, 300));
	CHECK(!cd_image_is_synthetic(t, 450));
	CHECK(cd_image_find_track(img, 825) == NULL);

	/* synthetic pregap is silent; real audio starts at 450 */
	CHECK(cd_image_read_audio(img, 320, pcm) == 0);
	CHECK(all_zero(pcm, sizeof(pcm)));
	check_sine(img, 440, 450, 450);
	check_sine(img, 440, 500, 450);
	cd_image_close(img);
}

static void test_hidden(void)
{
	cd_image_t *img = open_img("hidden/hidden.cue");

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
	cd_image_close(img);
}

static void test_multi(void)
{
	cd_image_t *img = open_img("multi/multi.cue");	/* CRLF, 2 BINs */
	uint8_t sec[2048];

	CHECK(img != NULL);
	if (!img)
		return;
	CHECK(img->ntracks == 2);
	expect_track(img, 0, 0, 0, 300, false, false);
	expect_track(img, 1, 300, 450, 825, true, true);
	CHECK(img->leadout_lba == 825);
	CHECK(cd_image_read_cooked(img, 16, sec) == 0);
	CHECK(!memcmp(sec + 1, "CD001", 5));
	check_sine(img, 440, 450, 450);
	cd_image_close(img);
}

static void test_errors(void)
{
	char p[1024];
	FILE *f;

	CHECK(open_img("nonexistent.cue") == NULL);
	CHECK(open_img("simple/simple.bin") == NULL);	/* unsupported ext */

	snprintf(p, sizeof(p), "%s/bad.cue", dir);
	f = fopen(p, "w");
	fputs("FILE \"missing.bin\" BINARY\n TRACK 01 AUDIO\n  INDEX 01 00:00:00\n", f);
	fclose(f);
	CHECK(cd_image_open(p) == NULL);
	f = fopen(p, "w");
	fputs("FILE \"simple/simple.bin\" BINARY\n TRACK 01 CDG\n  INDEX 01 00:00:00\n", f);
	fclose(f);
	CHECK(cd_image_open(p) == NULL);
	f = fopen(p, "w");
	fputs("FILE \"simple/simple.bin\" BINARY\n TRACK 01 AUDIO\n", f);
	fclose(f);
	CHECK(cd_image_open(p) == NULL);	/* no INDEX 01 */
	remove(p);
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s IMGDIR\n", argv[0]);
		return 2;
	}
	snprintf(dir, sizeof(dir), "%s", argv[1]);
	test_helpers();
	test_simple();
	test_hidden();
	test_multi();
	test_errors();
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
