/*
 * drivers/cd_audio.c - CD audio playback for image backed drives.
 *
 * Two parts that deliberately do not depend on each other:
 *  - a clock driven play state (what MSCDEX asks about: playing, paused,
 *    where is the head) that works with or without sound;
 *  - when sound is configured, a thread, woken by the SIGALRM tick like the
 *    PC speaker one, that decodes sectors of the playing range and writes
 *    them to a PCM stream (MC_CD) at 44100 Hz stereo.
 * Lock order: state_mtx, then the image lock (cdrom_img_lock()).
 */
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>

#include "emu.h"
#include "timers.h"
#include "sig.h"
#include "sound/sound.h"
#include "cd_image.h"
#include "cdrom_priv.h"

#define CDA_RATE	44100
#define SECTOR_FRAMES	CD_AUDIO_FRAMES
#define MAX_CATCHUP	(CDA_RATE / 10)		/* never write >100 ms at once */

static struct {
	pthread_mutex_t mtx;
	int idx;			/* drive being played, -1 if none */
	cd_image_t *img;
	enum cda_state state;
	uint32_t start, end;		/* range, end exclusive */
	uint32_t base;			/* head position at t0 */
	hitimer_t t0;
	uint8_t vol[2], out[2];

	/* sound */
	bool snd_init, feeding;
	int strm;
	sem_t sem;
	pthread_t thr;
	uint32_t next_lba;
	int16_t buf[SECTOR_FRAMES * 2];
	int bufpos;
} cda = { .mtx = PTHREAD_MUTEX_INITIALIZER, .idx = -1,
	  .vol = { 0xff, 0xff }, .out = { 0, 1 } };

static uint32_t pos_locked(void)
{
	hitimer_t el;
	uint32_t p;

	if (cda.state != CDA_PLAY)
		return cda.base;
	el = GETusTIME(0) - cda.t0;
	if (el < 0)
		el = 0;
	p = cda.base + (uint32_t)(el * 75 / 1000000);
	return p < cda.end ? p : cda.end;
}

/* notice the end of the range */
static void update_locked(void)
{
	if (cda.state == CDA_PLAY && pos_locked() >= cda.end) {
		cda.base = cda.end;
		cda.state = CDA_STOP;
	}
}

static void feed_stop_locked(void)
{
	if (cda.feeding) {
		pcm_flush(cda.strm);
		cda.feeding = false;
	}
}

static void feed_start_locked(uint32_t from)
{
	if (!cda.snd_init)
		return;
	cda.next_lba = from;
	cda.bufpos = SECTOR_FRAMES;	/* nothing buffered */
	if (!cda.feeding) {
		pcm_prepare_stream(cda.strm);
		cda.feeding = true;
	}
	sem_post(&cda.sem);
}

static void feed_locked(void)
{
	double period = pcm_frame_period_us(CDA_RATE);
	double stime = pcm_get_stream_time(cda.strm);
	hitimer_t now = GETusTIME(0);
	int n;

	if (now < stime)
		return;
	n = (now - stime) / period;
	if (n > MAX_CATCHUP)
		n = MAX_CATCHUP;
	while (n > 0) {
		sndbuf_t out[SECTOR_FRAMES][SNDBUF_CHANS];
		int take, i;

		if (cda.bufpos >= SECTOR_FRAMES) {
			if (cda.next_lba >= cda.end) {
				feed_stop_locked();
				return;
			}
			cdrom_img_lock();
			if (cd_image_read_audio(cda.img, cda.next_lba, cda.buf))
				memset(cda.buf, 0, sizeof(cda.buf));
			cdrom_img_unlock();
			cda.next_lba++;
			cda.bufpos = 0;
		}
		take = SECTOR_FRAMES - cda.bufpos;
		if (take > n)
			take = n;
		for (i = 0; i < take; i++) {
			const int16_t *src = &cda.buf[(cda.bufpos + i) * 2];
			int ch;

			for (ch = 0; ch < 2; ch++) {
				int o = cda.out[ch];

				/* output channel ch plays source channel o,
				 * only the left/right sources exist */
				out[i][ch] = o > 1 ? 0 :
					(src[o] * cda.vol[ch]) / 255;
			}
		}
		pcm_write_interleaved(out, take, CDA_RATE, PCM_FORMAT_S16_LE,
				      2, cda.strm);
		cda.bufpos += take;
		n -= take;
	}
}

static void *cda_thread(void *arg)
{
	(void)arg;
	while (1) {
		sem_wait(&cda.sem);
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		pthread_mutex_lock(&cda.mtx);
		if (cda.state == CDA_PLAY && cda.feeding)
			feed_locked();
		pthread_mutex_unlock(&cda.mtx);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	}
	return NULL;
}

static void cda_tick(void)
{
	if (cda.feeding)
		sem_post(&cda.sem);
}

static void snd_init_locked(void)
{
	if (cda.snd_init || !config.sound)
		return;
	cda.strm = pcm_allocate_stream(2, "CD Audio", (void *)MC_CD);
	sem_init(&cda.sem, 0, 0);
	pthread_create(&cda.thr, NULL, cda_thread, NULL);
#if defined(HAVE_PTHREAD_SETNAME_NP) && defined(__GLIBC__)
	pthread_setname_np(cda.thr, "dosemu: cdaudio");
#endif
	sigalrm_register_handler(cda_tick);
	cda.snd_init = true;
}

static bool mine(int idx)
{
	return cda.idx == idx;
}

void cda_play(int idx, cd_image_t *img, uint32_t start, uint32_t end)
{
	pthread_mutex_lock(&cda.mtx);
	snd_init_locked();
	cda.idx = idx;
	cda.img = img;
	cda.start = start;
	cda.end = end;
	cda.base = start;
	cda.t0 = GETusTIME(0);
	cda.state = start < end ? CDA_PLAY : CDA_STOP;
	if (cda.state == CDA_PLAY)
		feed_start_locked(start);
	else
		feed_stop_locked();
	pthread_mutex_unlock(&cda.mtx);
}

void cda_pause(int idx)
{
	pthread_mutex_lock(&cda.mtx);
	update_locked();
	if (mine(idx) && cda.state == CDA_PLAY) {
		cda.base = pos_locked();
		cda.state = CDA_PAUSE;
		feed_stop_locked();
	}
	pthread_mutex_unlock(&cda.mtx);
}

int cda_resume(int idx)
{
	int ret = 0;

	pthread_mutex_lock(&cda.mtx);
	if (mine(idx) && cda.state == CDA_PAUSE) {
		cda.t0 = GETusTIME(0);
		cda.state = CDA_PLAY;
		feed_start_locked(cda.base);
		ret = 1;
	}
	pthread_mutex_unlock(&cda.mtx);
	return ret;
}

void cda_stop(int idx)
{
	pthread_mutex_lock(&cda.mtx);
	if (mine(idx)) {
		if (cda.state == CDA_PLAY)
			cda.base = pos_locked();
		cda.state = CDA_STOP;
		feed_stop_locked();
	}
	pthread_mutex_unlock(&cda.mtx);
}

enum cda_state cda_state(int idx)
{
	enum cda_state s = CDA_STOP;

	pthread_mutex_lock(&cda.mtx);
	update_locked();
	if (mine(idx))
		s = cda.state;
	pthread_mutex_unlock(&cda.mtx);
	return s;
}

uint32_t cda_position(int idx)
{
	uint32_t p = 0;

	pthread_mutex_lock(&cda.mtx);
	if (mine(idx))
		p = pos_locked();
	pthread_mutex_unlock(&cda.mtx);
	return p;
}

void cda_set_volume(int idx, const uint8_t vol[2], const uint8_t out[2])
{
	(void)idx;
	pthread_mutex_lock(&cda.mtx);
	memcpy(cda.vol, vol, 2);
	memcpy(cda.out, out, 2);
	pthread_mutex_unlock(&cda.mtx);
}

void cda_done(void)
{
	pthread_mutex_lock(&cda.mtx);
	cda.state = CDA_STOP;
	feed_stop_locked();
	cda.idx = -1;
	pthread_mutex_unlock(&cda.mtx);
	if (cda.snd_init) {
		pthread_cancel(cda.thr);
		pthread_join(cda.thr, NULL);
		sem_destroy(&cda.sem);
		cda.snd_init = false;
	}
}
