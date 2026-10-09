/*
 * cd_image_tool - look inside a CD image with the cd_image layer.
 *
 *   cd_image_tool IMAGE                    track table, lead-out, ISO 9660 label
 *   cd_image_tool IMAGE --wav N OUT.wav [SECONDS]
 *                                         write audio track N (from INDEX 01) as a wav
 *   cd_image_tool IMAGE --read LBA         hex dump of one cooked sector
 * Handy for real discs; the unit tests only use generated images.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cd_image.h"

static void put16(FILE *f, unsigned v)
{
	fputc(v & 0xff, f);
	fputc(v >> 8, f);
}

static void put32(FILE *f, uint32_t v)
{
	put16(f, v & 0xffff);
	put16(f, v >> 16);
}

static void show(cd_image_t *img)
{
	uint8_t sec[CD_FRAME_COOKED];
	int i;

	printf("%d tracks, lead-out LBA %u\n", img->ntracks, img->leadout_lba);
	printf("trk type    start  index1    end     length\n");
	for (i = 0; i < img->ntracks; i++) {
		const cd_track_t *t = &img->tracks[i];
		uint8_t m, s, f;

		cd_lba_to_msf(t->index1_lba, &m, &s, &f);
		printf("%3d %-6s %7u %7u %7u  %7u  (%02u:%02u:%02u)%s\n", t->num,
		       t->is_audio ? "audio" : t->mode == 1 ? "mode1" : "mode2",
		       t->lba_start, t->index1_lba, t->end_lba,
		       t->end_lba - t->index1_lba, m, s, f,
		       t->pregap_in_file ? "" : " [pregap not in file]");
	}
	if (!img->tracks[0].is_audio &&
	    !cd_image_read_cooked(img, 16, sec) && !memcmp(sec + 1, "CD001", 5)) {
		char label[33];

		memcpy(label, sec + 40, 32);
		label[32] = 0;
		for (i = 31; i >= 0 && label[i] == ' '; i--)
			label[i] = 0;
		printf("ISO 9660 volume descriptor type %u, label \"%s\"\n",
		       sec[0], label);
	} else if (!img->tracks[0].is_audio) {
		printf("no ISO 9660 volume descriptor at sector 16\n");
	}
}

static int wav(cd_image_t *img, int trk, const char *path, int seconds)
{
	const cd_track_t *t;
	uint32_t n, lba, len;
	FILE *f;

	if (trk < 1 || trk > img->ntracks || !img->tracks[trk - 1].is_audio) {
		fprintf(stderr, "track %d is not an audio track\n", trk);
		return 1;
	}
	t = &img->tracks[trk - 1];
	n = t->end_lba - t->index1_lba;
	if (seconds > 0 && (uint32_t)seconds * 75 < n)
		n = seconds * 75;
	len = n * CD_AUDIO_FRAMES * 4;
	f = fopen(path, "wb");
	if (!f)
		return 1;
	fputs("RIFF", f); put32(f, 36 + len); fputs("WAVEfmt ", f);
	put32(f, 16); put16(f, 1); put16(f, 2); put32(f, 44100);
	put32(f, 44100 * 4); put16(f, 4); put16(f, 16);
	fputs("data", f); put32(f, len);
	for (lba = t->index1_lba; lba < t->index1_lba + n; lba++) {
		int16_t pcm[CD_AUDIO_FRAMES * 2];
		int i;

		if (cd_image_read_audio(img, lba, pcm)) {
			fprintf(stderr, "read error at LBA %u\n", lba);
			fclose(f);
			return 1;
		}
		for (i = 0; i < CD_AUDIO_FRAMES * 2; i++)
			put16(f, (uint16_t)pcm[i]);
	}
	fclose(f);
	printf("wrote %u sectors (%.1f s) of track %d to %s\n", n, n / 75.0,
	       trk, path);
	return 0;
}

int main(int argc, char **argv)
{
	cd_image_t *img;
	int rc = 0;

	if (argc < 2) {
		fprintf(stderr, "usage: %s IMAGE [--wav N OUT.wav [SECONDS] | --read LBA]\n",
			argv[0]);
		return 2;
	}
	img = cd_image_open(argv[1]);
	if (!img)
		return 1;
	if (argc >= 5 && !strcmp(argv[2], "--wav"))
		rc = wav(img, atoi(argv[3]), argv[4], argc > 5 ? atoi(argv[5]) : 0);
	else if (argc >= 4 && !strcmp(argv[2], "--read")) {
		uint8_t sec[CD_FRAME_COOKED];
		int i;

		if (cd_image_read_cooked(img, atoi(argv[3]), sec)) {
			fprintf(stderr, "read failed\n");
			rc = 1;
		} else {
			for (i = 0; i < CD_FRAME_COOKED; i++)
				printf("%02x%s", sec[i], (i % 32 == 31) ? "\n" : " ");
		}
	} else
		show(img);
	cd_image_close(img);
	return rc;
}
