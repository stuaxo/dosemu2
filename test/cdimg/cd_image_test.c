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

static char dir[256];

static cd_image_t *open_img(const char *rel)
{
	char p[2048];

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

static void test_qpos(void)
{
	cd_image_t *img = open_img("hidden/hidden.cue");
	cd_qpos_t q;

	CHECK(img != NULL);
	if (!img)
		return;
	/* hidden audio: index 0, counting down 150 .. 1 to INDEX 01 */
	cd_image_qpos(img, 0, &q);
	CHECK(q.track == 1 && q.index == 0 && q.rel_frames == 150);
	CHECK(q.ctrl_adr == 0x10);
	cd_image_qpos(img, 149, &q);
	CHECK(q.track == 1 && q.index == 0 && q.rel_frames == 1);
	cd_image_qpos(img, 150, &q);
	CHECK(q.track == 1 && q.index == 1 && q.rel_frames == 0);
	cd_image_qpos(img, 449, &q);
	CHECK(q.track == 1 && q.index == 1 && q.rel_frames == 299);
	cd_image_qpos(img, 450, &q);
	CHECK(q.track == 2 && q.index == 1 && q.rel_frames == 0);
	cd_image_qpos(img, 825, &q);
	CHECK(q.track == 0xAA && q.rel_frames == 0);
	cd_image_close(img);

	img = open_img("simple/simple.cue");
	CHECK(img != NULL);
	if (!img)
		return;
	cd_image_qpos(img, 16, &q);
	CHECK(q.track == 1 && q.index == 1 && q.ctrl_adr == 0x14);
	cd_image_qpos(img, 300, &q);	/* synthetic pregap of track 2 */
	CHECK(q.track == 2 && q.index == 0 && q.rel_frames == 150);
	CHECK(q.ctrl_adr == 0x10);
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


#ifdef HAVE_LIBCHDR
static void same_disc(cd_image_t *a, cd_image_t *b)
{
	uint32_t lba;
	int i;

	CHECK(a->ntracks == b->ntracks);
	CHECK(a->leadout_lba == b->leadout_lba);
	for (i = 0; i < a->ntracks && i < b->ntracks; i++) {
		const cd_track_t *x = &a->tracks[i], *y = &b->tracks[i];

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
		const cd_track_t *t = cd_image_find_track(a, lba);

		CHECK(cd_image_read_audio(a, lba, pa) == 0);
		CHECK(cd_image_read_audio(b, lba, pb) == 0);
		if (memcmp(pa, pb, sizeof(pa))) {
			CHECK(!"PCM differs");
			break;
		}
		if (t && !t->is_audio) {
			CHECK(cd_image_read_cooked(a, lba, ca) == 0);
			CHECK(cd_image_read_cooked(b, lba, cb) == 0);
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
		cd_image_t *cue, *chd;

		snprintf(p, sizeof(p), "%s.cue", names[k]);
		cue = open_img(p);
		snprintf(p, sizeof(p), "%s.chd", names[k]);
		chd = open_img(p);
		CHECK(cue != NULL);
		if (!chd) {
			fprintf(stderr, "SKIP: no %s (chdman missing?)\n", p);
			cd_image_close(cue);
			continue;
		}
		CHECK(chd->audio_big_endian && !cue->audio_big_endian);
		if (cue)
			same_disc(cue, chd);
		cd_image_close(cue);
		cd_image_close(chd);
	}

	/* hunk cache: re-reading the same sector must not decompress again */
	{
		cd_image_t *chd = open_img("hidden/hidden.chd");
		int16_t pcm[CD_AUDIO_FRAMES * 2];
		unsigned n;
		uint32_t lba;

		if (chd) {
			CHECK(cd_image_read_audio(chd, 460, pcm) == 0);
			n = cd_chd_hunk_reads(chd);
			CHECK(n >= 1);
			CHECK(cd_image_read_audio(chd, 460, pcm) == 0);
			CHECK(cd_chd_hunk_reads(chd) == n);
			n = cd_chd_hunk_reads(chd);
			for (lba = 460; lba < 476; lba++)
				CHECK(cd_image_read_audio(chd, lba, pcm) == 0);
			CHECK(cd_chd_hunk_reads(chd) - n <= 3);
			cd_image_close(chd);
		}
	}

	/* not a CHD at all, and a truncated one */
	snprintf(p, sizeof(p), "%s/fake.chd", dir);
	f = fopen(p, "w");
	fputs("this is not a chd file\n", f);
	fclose(f);
	CHECK(cd_image_open(p) == NULL);
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
			CHECK(cd_image_open(p) == NULL);
		}
	}
	remove(p);
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
#ifdef HAVE_LIBCHDR
	test_chd();
#else
	fprintf(stderr, "SKIP: built without libchdr\n");
#endif
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
