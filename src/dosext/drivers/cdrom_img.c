/*
 * drivers/cdrom_img.c
 *
 * MSCDEX device-driver requests for CD-ROM drives that are backed by an
 * image file (BIN/CUE or CHD, see cd_image.h) instead of a Linux device.
 * cdrom.c calls cdrom_img_request() first; if the configured path is a
 * .cue/.chd the request is handled here, otherwise cdrom.c carries on with
 * the ioctl based code.  Register and request-buffer conventions follow
 * cdrom_helper(): AL=0 success, AL=1 with an MSCDEX error code in AH.
 */
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "emu.h"
#include "memory.h"
#include "priv.h"
#include "cd_image.h"
#include "cdrom_priv.h"

#define NDRIVES 4

#define ERR_SEEK	0x08
#define ERR_GENERAL	0x0C
#define ERR_READ	0x0F

static pthread_mutex_t img_mtx = PTHREAD_MUTEX_INITIALIZER;

void cdrom_img_lock(void)
{
	pthread_mutex_lock(&img_mtx);
}

void cdrom_img_unlock(void)
{
	pthread_mutex_unlock(&img_mtx);
}

static cd_image_t *cd_img[NDRIVES];
static bool cd_img_tried[NDRIVES];

static bool path_is_image(const char *path)
{
	const char *ext = strrchr(path, '.');

	return ext && (!strcasecmp(ext, ".cue") || !strcasecmp(ext, ".chd"));
}

/*
 * Called when a cdrom path is configured.  dosemu2 sandboxes itself with
 * landlock before it opens anything, so the directory holding an image
 * (the CUE and the BINs next to it) has to be permitted while the
 * configuration is still being read.
 */
void cdrom_img_config(const char *path)
{
	char *dir, *slash;

	if (!path_is_image(path))
		return;
	dir = strdup(path);
	slash = dir ? strrchr(dir, '/') : NULL;
	if (!slash) {
		free(dir);
		dir = strdup(".");
	} else if (slash == dir) {
		slash[1] = '\0';
	} else {
		*slash = '\0';
	}
	if (dir)
		permit_dir_ro(dir);	/* the list keeps the string */
}

static cd_image_t *get_image(int idx, const char *path)
{
	if (idx < 0 || idx >= NDRIVES || !path)
		return NULL;
	if (!cd_img_tried[idx]) {
		cd_img_tried[idx] = true;
		if (path_is_image(path)) {
			cd_img[idx] = cd_image_open(path);
			if (!cd_img[idx])
				error("CDROM: cannot open image %s\n", path);
			else
				C_printf("CDROM: image %s: %d tracks, %u sectors\n",
					 path, cd_img[idx]->ntracks,
					 cd_img[idx]->leadout_lba);
		}
	}
	return cd_img[idx];
}

/* Red Book address as the driver returns it: frame, second, minute, 0 */
static void put_msf(unsigned char *p, uint32_t lba)
{
	uint8_t m, s, f;

	cd_lba_to_msf(lba, &m, &s, &f);
	p[0] = f;
	p[1] = s;
	p[2] = m;
	p[3] = 0;
}

static void audio_stop_state(void)
{
	audio_status.paused_bit = 0;
	audio_status.last_StartSector = 0;
	audio_status.last_EndSector = 0;
}

/* AH=1 tells MSCDEX that audio is playing */
static void set_busy(int idx)
{
	if (cda_state(idx) == CDA_PLAY)
		HI(ax) = 1;
}

static void fail(int code)
{
	HI(ax) = code;
	LO(ax) = 1;
}

static void do_read_long(cd_image_t *img, unsigned char *req_buf,
			 unsigned char *transfer_buf,
			 unsigned int dos_transfer_buf)
{
	uint32_t lba;
	unsigned n, i;
	uint8_t sec[CD_FRAME_COOKED];

	if (req_buf == NULL && transfer_buf == NULL) {
		req_buf = SEG_ADR((unsigned char *), es, di);
		dos_transfer_buf = SEGOFF2LINEAR(SREG(ds), LWORD(esi));
	}
	if (*CALC_PTR(req_buf, MSCD_READ_ADRESSING, u_char) == 1) {
		/* Red Book MSF, bytes are frame, second, minute */
		lba = cd_msf_to_lba(
			*CALC_PTR(req_buf, MSCD_READ_STARTSECTOR + 2, u_char),
			*CALC_PTR(req_buf, MSCD_READ_STARTSECTOR + 1, u_char),
			*CALC_PTR(req_buf, MSCD_READ_STARTSECTOR + 0, u_char));
	} else {
		lba = *CALC_PTR(req_buf, MSCD_READ_STARTSECTOR, u_int);
	}
	n = *CALC_PTR(req_buf, MSCD_READ_NUMSECTORS, u_short);
	C_printf("CDROM: image read sector %#x count %u\n", lba, n);

	if (lba >= img->leadout_lba || n > img->leadout_lba - lba) {
		fail(ERR_SEEK);
		return;
	}
	for (i = 0; i < n; i++) {
		int err;

		cdrom_img_lock();
		err = cd_image_read_cooked(img, lba + i, sec);
		cdrom_img_unlock();
		if (err) {
			C_printf("CDROM: image read of sector %#x failed\n",
				 lba + i);
			fail(ERR_READ);
			return;
		}
		if (transfer_buf == NULL)
			MEMCPY_2DOS(dos_transfer_buf + i * CD_FRAME_COOKED, sec,
				    CD_FRAME_COOKED);
		else
			memcpy(transfer_buf + i * CD_FRAME_COOKED, sec,
			       CD_FRAME_COOKED);
	}
	LO(ax) = 0;
}

int cdrom_img_request(int idx, const char *path, unsigned char *req_buf,
		      unsigned char *transfer_buf, unsigned int dos_transfer_buf)
{
	cd_image_t *img = get_image(idx, path);

	if (!img)
		return 0;

	switch (HI(ax)) {
	case 0x01:			/* init: same defaults as cdrom.c */
		audio_status.status = 0x00000710;
		audio_status.paused_bit = 0;
		audio_status.media_changed = 0;
		audio_status.volume0 = 0xFF;
		audio_status.volume1 = 0xFF;
		audio_status.volume2 = 0;
		audio_status.volume3 = 0;
		audio_status.outchan0 = 0;
		audio_status.outchan1 = 1;
		audio_status.outchan2 = 2;
		audio_status.outchan3 = 3;
		cda_stop(idx);
		audio_stop_state();
		LO(ax) = 0;
		break;
	case 0x02:			/* read long */
		do_read_long(img, req_buf, transfer_buf, dos_transfer_buf);
		break;
	case 0x03:			/* seek: nothing to do for an image */
		LO(ax) = 0;
		break;
	case 0x08:			/* sector size */
		LO(ax) = 0;
		LWORD(ebx) = CD_FRAME_COOKED;
		break;
	case 0x09:			/* media changed */
		HI(ax) = 0;
		LO(ax) = 0;
		LO(bx) = audio_status.media_changed;
		if (!LO(bx))
			set_busy(idx);
		audio_status.media_changed = 0;
		break;
	case 0x0A:			/* device status */
		HI(ax) = 0;
		LO(ax) = 0;
		LWORD(ebx) = audio_status.status;
		set_busy(idx);
		break;
	case 0x0B:			/* drive reset */
	case 0x0D:			/* eject: an image cannot be ejected */
	case 0x0E:			/* close tray */
		LO(ax) = 0;
		break;
	case 0x0C:			/* lock/unlock door */
		if (LO(bx) == 1)
			audio_status.status &= 0xFFFFFFFD;
		else
			audio_status.status |= 0x2;
		LO(ax) = 0;
		break;
	case 0x04: {			/* play */
		uint32_t sec150, count, start, end;

		req_buf = SEG_ADR((unsigned char *), es, di);
		if (*CALC_PTR(req_buf, MSCD_PLAY_ADRESSING, u_char) == 1) {
			sec150 = *CALC_PTR(req_buf, MSCD_PLAY_STARTSECTOR + 2,
					   u_char) * 60 * 75 +
				 *CALC_PTR(req_buf, MSCD_PLAY_STARTSECTOR + 1,
					   u_char) * 75 +
				 *CALC_PTR(req_buf, MSCD_PLAY_STARTSECTOR + 0,
					   u_char);
		} else {
			sec150 = *CALC_PTR(req_buf, MSCD_PLAY_STARTSECTOR,
					   u_int) + 150;
		}
		count = *CALC_PTR(req_buf, MSCD_PLAY_NUMSECTORS, u_int);
		audio_status.last_StartSector = sec150;
		audio_status.last_EndSector = sec150 + count;
		audio_status.paused_bit = 0;
		/* A start inside the pregap (even LBA 0 of a hidden track)
		 * is valid: do not clamp to INDEX 01. */
		start = sec150 > CD_LEADIN ? sec150 - CD_LEADIN : 0;
		C_printf("CDROM: image play lba %u count %u\n", start, count);
		if (start >= img->leadout_lba) {
			fail(ERR_SEEK);
			break;
		}
		end = count > img->leadout_lba - start ?
			img->leadout_lba : start + count;
		cda_play(idx, img, start, end);
		LO(ax) = 0;
		break;
	}
	case 0x05:			/* pause (stop) audio */
		LO(ax) = 0;
		if (cda_state(idx) == CDA_PLAY) {
			audio_status.last_StartSector =
				cda_position(idx) + CD_LEADIN;
			cda_pause(idx);
			audio_status.paused_bit = 1;
		} else {
			cda_stop(idx);
			audio_stop_state();
		}
		break;
	case 0x06:			/* resume audio */
		LO(ax) = 0;
		if (audio_status.paused_bit && cda_resume(idx)) {
			audio_status.paused_bit = 0;
			HI(ax) = 1;
		} else {
			LO(ax) = 1;
		}
		break;
	case 0x07: {			/* location of head */
		uint32_t lba = cda_position(idx);

		LWORD(eax) = 0;
		set_busy(idx);
		req_buf = SEG_ADR((unsigned char *), ds, si);
		if (*CALC_PTR(req_buf, MSCD_LOCH_ADRESSING, u_char) == 0)
			*CALC_PTR(req_buf, MSCD_LOCH_LOCATION, u_int) = lba;
		else
			put_msf(req_buf + MSCD_LOCH_LOCATION, lba);
		break;
	}
	case 0x0F: {			/* audio channel control */
		uint8_t vol[2], out[2];

		LWORD(eax) = 0;
		set_busy(idx);
		req_buf = SEG_ADR((unsigned char *), ds, si);
		audio_status.volume0 = *CALC_PTR(req_buf, MSCD_CTRL_VOLUME0, u_char);
		audio_status.volume1 = *CALC_PTR(req_buf, MSCD_CTRL_VOLUME1, u_char);
		audio_status.volume2 = *CALC_PTR(req_buf, MSCD_CTRL_VOLUME2, u_char);
		audio_status.volume3 = *CALC_PTR(req_buf, MSCD_CTRL_VOLUME3, u_char);
		audio_status.outchan0 = *CALC_PTR(req_buf, MSCD_CTRL_VOLUME0 - 1, u_char);
		audio_status.outchan1 = *CALC_PTR(req_buf, MSCD_CTRL_VOLUME1 - 1, u_char);
		audio_status.outchan2 = *CALC_PTR(req_buf, MSCD_CTRL_VOLUME2 - 1, u_char);
		audio_status.outchan3 = *CALC_PTR(req_buf, MSCD_CTRL_VOLUME3 - 1, u_char);
		vol[0] = audio_status.volume0;
		vol[1] = audio_status.volume1;
		out[0] = audio_status.outchan0;
		out[1] = audio_status.outchan1;
		cda_set_volume(idx, vol, out);
		break;
	}
	case 0x13: {			/* Q-channel */
		cd_qpos_t q;
		uint8_t m, s, f;

		LWORD(eax) = 0;
		set_busy(idx);
		cd_image_qpos(img, cda_position(idx), &q);
		req_buf = SEG_ADR((unsigned char *), ds, si);
		*CALC_PTR(req_buf, MSCD_QCHAN_CTRL, u_char) = q.ctrl_adr;
		*CALC_PTR(req_buf, MSCD_QCHAN_TNO, u_char) = q.track;
		*CALC_PTR(req_buf, MSCD_QCHAN_IND, u_char) = q.index;
		/* relative time is not offset by the 150 frame lead-in;
		 * in the pregap it counts down to INDEX 01 */
		m = q.rel_frames / (60 * 75);
		s = (q.rel_frames / 75) % 60;
		f = q.rel_frames % 75;
		*CALC_PTR(req_buf, MSCD_QCHAN_MIN, u_char) = m;
		*CALC_PTR(req_buf, MSCD_QCHAN_SEC, u_char) = s;
		*CALC_PTR(req_buf, MSCD_QCHAN_FRM, u_char) = f;
		*CALC_PTR(req_buf, MSCD_QCHAN_ZERO, u_char) = 0;
		cd_lba_to_msf(q.abs_lba, &m, &s, &f);
		*CALC_PTR(req_buf, MSCD_QCHAN_AMIN, u_char) = m;
		*CALC_PTR(req_buf, MSCD_QCHAN_ASEC, u_char) = s;
		*CALC_PTR(req_buf, MSCD_QCHAN_AFRM, u_char) = f;
		break;
	}
	case 0x14:			/* audio status */
		LWORD(eax) = 0;
		set_busy(idx);
		req_buf = SEG_ADR((unsigned char *), ds, si);
		*CALC_PTR(req_buf, MSCD_AUDSTAT_PAUSED, u_short) =
			audio_status.paused_bit;
		*CALC_PTR(req_buf, MSCD_AUDSTAT_START, u_int) =
			audio_status.last_StartSector;
		*CALC_PTR(req_buf, MSCD_AUDSTAT_END, u_int) =
			audio_status.last_EndSector;
		break;
	case 0x15:			/* get audio channel information */
		LWORD(eax) = 0;
		set_busy(idx);
		req_buf = SEG_ADR((unsigned char *), ds, si);
		*CALC_PTR(req_buf, MSCD_AUDCHAN_VOLUME0, u_char) = audio_status.volume0;
		*CALC_PTR(req_buf, MSCD_AUDCHAN_VOLUME1, u_char) = audio_status.volume1;
		*CALC_PTR(req_buf, MSCD_AUDCHAN_VOLUME2, u_char) = audio_status.volume2;
		*CALC_PTR(req_buf, MSCD_AUDCHAN_VOLUME3, u_char) = audio_status.volume3;
		*CALC_PTR(req_buf, MSCD_AUDCHAN_VOLUME0 - 1, u_char) = audio_status.outchan0;
		*CALC_PTR(req_buf, MSCD_AUDCHAN_VOLUME1 - 1, u_char) = audio_status.outchan1;
		*CALC_PTR(req_buf, MSCD_AUDCHAN_VOLUME2 - 1, u_char) = audio_status.outchan2;
		*CALC_PTR(req_buf, MSCD_AUDCHAN_VOLUME3 - 1, u_char) = audio_status.outchan3;
		break;
	case 0x10:			/* audio disc info */
		LWORD(eax) = 0;
		req_buf = SEG_ADR((unsigned char *), ds, si);
		*CALC_PTR(req_buf, MSCD_DISKINFO_LTN, u_char) =
			img->tracks[0].num;
		*CALC_PTR(req_buf, MSCD_DISKINFO_HTN, u_char) =
			img->tracks[img->ntracks - 1].num;
		put_msf(req_buf + MSCD_DISKINFO_LEADOUT, img->leadout_lba);
		break;
	case 0x11: {			/* track info */
		int ctrl;
		uint32_t lba;
		unsigned trk;

		req_buf = SEG_ADR((unsigned char *), ds, si);
		trk = *CALC_PTR(req_buf, MSCD_TRACKINFO_TRACKNUM, u_char);
		C_printf("CDROM: image track info, track %u\n", trk);
		if (trk == 0xAA) {		/* lead-out */
			lba = img->leadout_lba;
			ctrl = img->tracks[img->ntracks - 1].is_audio ? 0 : 4;
		} else if (trk >= 1 && trk <= (unsigned)img->ntracks) {
			lba = img->tracks[trk - 1].index1_lba;
			ctrl = img->tracks[trk - 1].is_audio ? 0 : 4;
		} else {
			/* MSCDEX reads beyond the last track; not an error to log */
			LO(ax) = 1;
			break;
		}
		put_msf(req_buf + MSCD_TRACKINFO_TRACKPOS, lba);
		*CALC_PTR(req_buf, MSCD_TRACKINFO_CTRL, u_char) =
			ctrl << 4 | 0x20;	/* as cdrom.c does for devices */
		LO(ax) = 0;
		break;
	}
	case 0x12:			/* volume size */
		req_buf = SEG_ADR((unsigned char *), ds, si);
		*CALC_PTR(req_buf, MSCD_GETVOLUMESIZE_SIZE, int) =
			img->leadout_lba;
		LO(ax) = 0;
		break;
	default:
		C_printf("CDROM: image: unknown request %#x\n", HI(ax));
		fail(ERR_GENERAL);
		break;
	}
	return 1;
}

void cdrom_img_done(void)
{
	int i;

	cda_done();
	for (i = 0; i < NDRIVES; i++) {
		cd_image_close(cd_img[i]);
		cd_img[i] = NULL;
		cd_img_tried[i] = false;
	}
}
