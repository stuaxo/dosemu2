/*
 * cdrom_priv.h - state and MSCDEX request-buffer layout used by cdrom.c.
 */
#ifndef CDROM_PRIV_H
#define CDROM_PRIV_H

struct audio_status {
    unsigned int status;
    unsigned char media_changed;
    unsigned char paused_bit;
    unsigned int last_StartSector, last_EndSector;
    unsigned char outchan0, outchan1, outchan2, outchan3;
    unsigned char volume0, volume1, volume2, volume3;
};
extern struct audio_status audio_status;

#define CALC_PTR(PTR,OFFSET,RESULT_TYPE) ((RESULT_TYPE *)(PTR+OFFSET))

#define MSCD_GETVOLUMESIZE_SIZE    1

#define MSCD_READ_ADRESSING       13
#define MSCD_READ_STARTSECTOR     20
#define MSCD_READ_NUMSECTORS      18

#define MSCD_SEEK_STARTSECTOR     20

#define MSCD_PLAY_ADRESSING       13
#define MSCD_PLAY_STARTSECTOR     14
#define MSCD_PLAY_NUMSECTORS      18

#define MSCD_LOCH_ADRESSING        1
#define MSCD_LOCH_LOCATION         2

#define MSCD_DISKINFO_LTN          1
#define MSCD_DISKINFO_HTN          2
#define MSCD_DISKINFO_LEADOUT      3

#define MSCD_TRACKINFO_TRACKNUM    1
#define MSCD_TRACKINFO_TRACKPOS    2
#define MSCD_TRACKINFO_CTRL        6

#define MSCD_QCHAN_CTRL            1
#define MSCD_QCHAN_TNO             2
#define MSCD_QCHAN_IND             3
#define MSCD_QCHAN_MIN             4
#define MSCD_QCHAN_SEC             5
#define MSCD_QCHAN_FRM             6
#define MSCD_QCHAN_ZERO            7
#define MSCD_QCHAN_AMIN            8
#define MSCD_QCHAN_ASEC            9
#define MSCD_QCHAN_AFRM           10

#define MSCD_AUDSTAT_PAUSED        1
#define MSCD_AUDSTAT_START         3
#define MSCD_AUDSTAT_END           7

#define MSCD_CTRL_VOLUME0          2
#define MSCD_CTRL_VOLUME1          4
#define MSCD_CTRL_VOLUME2          6
#define MSCD_CTRL_VOLUME3          8

#define MSCD_AUDCHAN_VOLUME0       2
#define MSCD_AUDCHAN_VOLUME1       4
#define MSCD_AUDCHAN_VOLUME2       6
#define MSCD_AUDCHAN_VOLUME3       8

/* Red Book addressing: 75 frames to the second, 60 seconds to the minute. */
static inline unsigned int mscd_msf_to_frames(unsigned int m, unsigned int s,
					      unsigned int f)
{
    return (m * 60 + s) * 75 + f;
}

static inline void mscd_frames_to_msf(unsigned int frames, unsigned char *m,
				      unsigned char *s, unsigned char *f)
{
    *m = frames / (60 * 75);
    *s = (frames % (60 * 75)) / 75;
    *f = (frames % (60 * 75)) % 75;
}

/*
 * Image-backed drives (cdrom_img.c).  Returns 1 if the drive is backed by a
 * cd_image and the request was handled, 0 if cdrom.c should handle it.
 */
int cdrom_img_request(int idx, const char *path, unsigned char *req_buf,
		      unsigned char *transfer_buf, unsigned int dos_transfer_buf);
void cdrom_img_done(void);

/*
 * CD audio playback for image drives (cd_audio.c).  Positions are disc LBAs
 * (LBA 0 == MSF 00:02:00).  Only one drive plays at a time; position and
 * state follow the wall clock whether or not sound is configured, so games
 * that poll the Q-channel see time pass either way.
 */
struct cd_image;
enum cda_state { CDA_STOP, CDA_PLAY, CDA_PAUSE };
void cda_play(int idx, struct cd_image *img, uint32_t start, uint32_t end);
void cda_pause(int idx);
int cda_resume(int idx);		/* 1 if it was paused */
void cda_stop(int idx);
enum cda_state cda_state(int idx);	/* also notices end of the range */
uint32_t cda_position(int idx);
void cda_set_volume(int idx, const uint8_t vol[2], const uint8_t out[2]);
void cda_done(void);

/* serialises cd_image access between the helper and the audio thread */
void cdrom_img_lock(void);
void cdrom_img_unlock(void);

#endif /* CDROM_PRIV_H */
