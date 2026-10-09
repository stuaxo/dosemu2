/*
 * cd_cue.c - BIN/CUE backend for the cd_image layer.
 *
 * Disc positions are absolute LBAs (LBA 0 == MSF 00:02:00).  Times in a CUE
 * sheet are positions inside the current FILE; PREGAP/POSTGAP occupy disc
 * time but no file space, so they shift every later disc LBA in that file.
 * Audio in a BIN is raw little-endian 16-bit stereo.
 */
#define _FILE_OFFSET_BITS 64
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "cd_image.h"

typedef struct {
	char    *path;
	FILE    *fp;
	off_t    size;
	uint32_t sector_size;	/* 0 until the first track in it is seen */
	uint32_t base_lba;	/* disc LBA of file sector 0 */
	uint32_t end_lba;	/* disc LBA just past the file's data */
} cue_file_t;

typedef struct {
	int      file_idx;
	uint32_t file_lba0;	/* file sector holding first_stored_lba */
} cue_trk_t;

typedef struct {
	cue_file_t files[CDI_MAX_TRACKS];
	int        nfiles;
	cue_trk_t  trk[CDI_MAX_TRACKS];
} cue_priv_t;

static int cue_read_frame(cd_image_t *img, const cd_track_t *t, uint32_t lba,
			  uint8_t *out)
{
	cue_priv_t *p = img->priv;
	const cue_trk_t *ct = &p->trk[t->num - 1];
	cue_file_t *f = &p->files[ct->file_idx];
	off_t off;

	if (lba < t->first_stored_lba)
		return -1;
	off = (off_t)(ct->file_lba0 + (lba - t->first_stored_lba)) *
		f->sector_size;
	if (!f->fp) {
		f->fp = fopen(f->path, "rb");
		if (!f->fp) {
			cd_log("cannot open %s\n", f->path);
			return -1;
		}
	}
	if (fseeko(f->fp, off, SEEK_SET) ||
			fread(out, 1, f->sector_size, f->fp) != f->sector_size)
		return -1;
	return 0;
}

static void cue_close(cd_image_t *img)
{
	cue_priv_t *p = img->priv;
	int i;

	for (i = 0; i < p->nfiles; i++) {
		if (p->files[i].fp)
			fclose(p->files[i].fp);
		free(p->files[i].path);
	}
	free(p);
	free(img);
}

/* ---- parsing helpers ---- */

/* Next whitespace separated token; "quoted names" may contain spaces. */
static char *next_tok(char **sp)
{
	char *s = *sp, *tok;

	while (*s && isspace((unsigned char)*s))
		s++;
	if (!*s) {
		*sp = s;
		return NULL;
	}
	if (*s == '"') {
		tok = ++s;
		while (*s && *s != '"')
			s++;
	} else {
		tok = s;
		while (*s && !isspace((unsigned char)*s))
			s++;
	}
	if (*s)
		*s++ = '\0';
	*sp = s;
	return tok;
}

static int parse_msf(const char *s, uint32_t *out)
{
	unsigned m, sec, f;
	char c;

	if (!s || sscanf(s, "%u:%u:%u%c", &m, &sec, &f, &c) != 3 ||
			sec >= 60 || f >= 75)
		return -1;
	*out = (m * 60 + sec) * 75 + f;
	return 0;
}

/* Resolve name relative to the CUE's directory, with a case-insensitive
 * fallback because CUEs written on Windows often get the case wrong. */
static char *resolve_path(const char *cue, const char *name)
{
	const char *slash = strrchr(cue, '/');
	size_t dlen = slash ? (size_t)(slash - cue) + 1 : 0;
	char *full = malloc(dlen + strlen(name) + 1);
	struct stat st;
	DIR *d;
	struct dirent *de;
	char *dir;

	if (!full)
		return NULL;
	memcpy(full, cue, dlen);
	strcpy(full + dlen, name);
	if (!stat(full, &st))
		return full;

	dir = strndup(dlen ? cue : ".", dlen ? dlen : 1);
	d = dir ? opendir(dir) : NULL;
	if (d) {
		while ((de = readdir(d))) {
			if (!strcasecmp(de->d_name, name)) {
				free(full);
				full = malloc(dlen + strlen(de->d_name) + 1);
				if (full) {
					memcpy(full, cue, dlen);
					strcpy(full + dlen, de->d_name);
				}
				break;
			}
		}
		closedir(d);
	}
	free(dir);
	return full;
}

static int track_mode(const char *m, cd_track_t *t)
{
	t->is_audio = false;
	t->mode = 1;
	t->hdr_skip = 0;
	if (!strcasecmp(m, "AUDIO")) {
		t->is_audio = true;
		t->mode = 0;
		t->sector_size = CD_FRAME_RAW;
	} else if (!strcasecmp(m, "MODE1/2048")) {
		t->sector_size = 2048;
	} else if (!strcasecmp(m, "MODE1/2352")) {
		t->sector_size = CD_FRAME_RAW;
		t->hdr_skip = 16;
	} else if (!strcasecmp(m, "MODE2/2336")) {
		t->mode = 2;
		t->sector_size = 2336;
		t->hdr_skip = 8;
	} else if (!strcasecmp(m, "MODE2/2352")) {
		t->mode = 2;
		t->sector_size = CD_FRAME_RAW;
		t->hdr_skip = 24;
	} else {
		return -1;
	}
	return 0;
}

static int finish_file(cue_file_t *f, uint32_t synth_in_file)
{
	if (!f->sector_size) {
		f->end_lba = f->base_lba + synth_in_file;
		return 0;
	}
	f->end_lba = f->base_lba + synth_in_file +
		(uint32_t)(f->size / f->sector_size);
	return 0;
}

cd_image_t *cd_cue_open(const char *path)
{
	FILE *fp = fopen(path, "r");
	cd_image_t *img;
	cue_priv_t *p;
	char line[1024];
	int lineno = 0;
	int cur = -1;			/* current track index, -1 before any */
	bool have_index00 = false, have_index01 = false;
	uint32_t synth_pregap = 0;	/* this track's PREGAP */
	uint32_t synth_in_file = 0;	/* PREGAP+POSTGAP seen in this file */
	int i;

	if (!fp) {
		cd_log("%s: cannot open: %s\n", path, strerror(errno));
		return NULL;
	}
	img = calloc(1, sizeof(*img));
	p = calloc(1, sizeof(*p));
	if (!img || !p) {
		free(img);
		free(p);
		fclose(fp);
		return NULL;
	}
	img->priv = p;
	img->read_frame = cue_read_frame;
	img->close = cue_close;
	img->audio_big_endian = false;

	while (fgets(line, sizeof(line), fp)) {
		char *s = line, *kw, *a, *b;
		uint32_t msf;

		lineno++;
		if (lineno == 1 && !memcmp(s, "\xef\xbb\xbf", 3))
			s += 3;
		kw = next_tok(&s);
		if (!kw)
			continue;

		if (!strcasecmp(kw, "FILE")) {
			struct stat st;
			cue_file_t *f;

			a = next_tok(&s);
			b = next_tok(&s);
			if (!a || !b || strcasecmp(b, "BINARY")) {
				cd_log("%s:%d: only FILE ... BINARY supported\n",
				       path, lineno);
				goto fail;
			}
			if (p->nfiles >= CDI_MAX_TRACKS)
				goto fail;
			if (p->nfiles)
				finish_file(&p->files[p->nfiles - 1],
					    synth_in_file);
			f = &p->files[p->nfiles];
			f->path = resolve_path(path, a);
			if (!f->path || stat(f->path, &st) || !S_ISREG(st.st_mode)) {
				cd_log("%s:%d: cannot find %s\n", path, lineno, a);
				if (f->path)
					p->nfiles++;	/* so close() frees it */
				goto fail;
			}
			f->size = st.st_size;
			f->base_lba = p->nfiles ?
				p->files[p->nfiles - 1].end_lba : 0;
			p->nfiles++;
			synth_in_file = 0;
		} else if (!strcasecmp(kw, "TRACK")) {
			cd_track_t *t;
			cue_file_t *f;
			unsigned num;

			a = next_tok(&s);
			b = next_tok(&s);
			if (cur >= 0 && !have_index01) {
				cd_log("%s:%d: track %d has no INDEX 01\n",
				       path, lineno, cur + 1);
				goto fail;
			}
			if (!a || !b || sscanf(a, "%u", &num) != 1 ||
					!p->nfiles ||
					cur + 1 >= CDI_MAX_TRACKS) {
				cd_log("%s:%d: bad TRACK\n", path, lineno);
				goto fail;
			}
			cur++;
			t = &img->tracks[cur];
			if (track_mode(b, t)) {
				cd_log("%s:%d: unsupported track mode %s\n",
				       path, lineno, b);
				goto fail;
			}
			/* num is only used for display; the table index is
			 * what the rest of the code uses. */
			t->num = cur + 1;
			f = &p->files[p->nfiles - 1];
			if (f->sector_size && f->sector_size != t->sector_size) {
				cd_log("%s:%d: mixed sector sizes in one FILE\n",
				       path, lineno);
				goto fail;
			}
			f->sector_size = t->sector_size;
			p->trk[cur].file_idx = p->nfiles - 1;
			have_index00 = have_index01 = false;
			synth_pregap = 0;
			img->ntracks = cur + 1;
		} else if (!strcasecmp(kw, "PREGAP") || !strcasecmp(kw, "POSTGAP")) {
			if (cur < 0 || parse_msf(next_tok(&s), &msf)) {
				cd_log("%s:%d: bad %s\n", path, lineno, kw);
				goto fail;
			}
			synth_in_file += msf;
			if (!strcasecmp(kw, "PREGAP"))
				synth_pregap = msf;
		} else if (!strcasecmp(kw, "INDEX")) {
			cd_track_t *t;
			cue_file_t *f;
			unsigned idx;

			a = next_tok(&s);
			b = next_tok(&s);
			if (cur < 0 || !a || sscanf(a, "%u", &idx) != 1 ||
					parse_msf(b, &msf)) {
				cd_log("%s:%d: bad INDEX\n", path, lineno);
				goto fail;
			}
			t = &img->tracks[cur];
			f = &p->files[p->trk[cur].file_idx];
			if (idx == 0) {
				t->lba_start = f->base_lba + synth_in_file + msf;
				t->first_stored_lba = t->lba_start;
				t->pregap_in_file = true;
				p->trk[cur].file_lba0 = msf;
				have_index00 = true;
			} else if (idx == 1) {
				t->index1_lba = f->base_lba + synth_in_file + msf;
				if (!have_index00) {
					t->lba_start = t->index1_lba - synth_pregap;
					t->first_stored_lba = t->index1_lba;
					t->pregap_in_file = false;
					p->trk[cur].file_lba0 = msf;
				}
				have_index01 = true;
			}
		}
		/* REM, TITLE, PERFORMER, CATALOG, FLAGS, ISRC, ... ignored */
	}
	if (cur < 0 || !have_index01) {
		cd_log("%s: no usable tracks\n", path);
		goto fail;
	}
	finish_file(&p->files[p->nfiles - 1], synth_in_file);

	for (i = 0; i < img->ntracks; i++) {
		cd_track_t *t = &img->tracks[i];

		if (i + 1 < img->ntracks &&
		    p->trk[i + 1].file_idx == p->trk[i].file_idx)
			t->end_lba = img->tracks[i + 1].lba_start;
		else
			t->end_lba = p->files[p->trk[i].file_idx].end_lba;
		if (t->end_lba < t->index1_lba) {
			cd_log("%s: track %d is empty or out of order\n",
			       path, i + 1);
			goto fail;
		}
	}
	img->leadout_lba = img->tracks[img->ntracks - 1].end_lba;
	fclose(fp);
	return img;

fail:
	fclose(fp);
	cue_close(img);
	return NULL;
}
