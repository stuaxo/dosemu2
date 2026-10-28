/*
 * drivers/cdrom_img.c
 *
 * MSCDEX device-driver requests for CD-ROM drives that are backed by an
 * image file (see cdimage.h) instead of a Linux device.
 * cdrom.c calls cdrom_img_request() first; if the configured path is an
 * image file in a format that cdimage knows, the request is handled
 * here, otherwise cdrom.c carries on with the ioctl based code.
 * The image itself is read by the cdimage layer (src/base/lib/cdimage),
 * which knows nothing of DOS.
 * Register and request-buffer conventions follow cdrom_helper(): AL=0
 * success, AL=1 with an MSCDEX error code in AH.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "emu.h"
#include "memory.h"
#include "priv.h"
#include "cdimage.h"
#include "cdrom_priv.h"

#define NDRIVES 4

#define ERR_SEEK        0x08
#define ERR_GENERAL     0x0C
#define ERR_READ        0x0F

static cdimage_t *cd_img[NDRIVES];
static bool cd_img_tried[NDRIVES];

/*
 * A regular file that one of the registered formats claims.  Devices and
 * plain ISO files are left to the ioctl based code in cdrom.c.
 */
static bool path_is_image(const char *path)
{
  struct stat st;

  return !stat(path, &st) && S_ISREG(st.st_mode) && cdimage_probe(path);
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
    permit_dir_ro(dir);
}

static cdimage_t *get_image(int idx, const char *path)
{
  if (idx < 0 || idx >= NDRIVES || !path)
    return NULL;
  if (!cd_img_tried[idx]) {
    cd_img_tried[idx] = true;
    if (path_is_image(path)) {
      cd_img[idx] = cdimage_open(path);
      if (!cd_img[idx])
        error("CDROM: cannot open image %s\n", path);
      else
        C_printf("CDROM: image %s: %d tracks, %u sectors\n",
                 path, cd_img[idx]->ntracks, cd_img[idx]->leadout_lba);
    }
  }
  return cd_img[idx];
}

static void fail(int code)
{
  HI(ax) = code;
  LO(ax) = 1;
}

static void do_read_long(cdimage_t *img, unsigned char *req_buf,
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
    lba =
        cd_msf_to_lba(*CALC_PTR
                      (req_buf, MSCD_READ_STARTSECTOR + 2, u_char),
                      *CALC_PTR(req_buf, MSCD_READ_STARTSECTOR + 1,
                                u_char), *CALC_PTR(req_buf,
                                                   MSCD_READ_STARTSECTOR +
                                                   0, u_char));
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
    if (cdimage_read_cooked(img, lba + i, sec)) {
      C_printf("CDROM: image read of sector %#x failed\n", lba + i);
      fail(ERR_READ);
      return;
    }
    if (transfer_buf == NULL)
      MEMCPY_2DOS(dos_transfer_buf + i * CD_FRAME_COOKED, sec,
                  CD_FRAME_COOKED);
    else
      memcpy(transfer_buf + i * CD_FRAME_COOKED, sec, CD_FRAME_COOKED);
  }
  LO(ax) = 0;
}

int cdrom_img_request(int idx, const char *path, unsigned char *req_buf,
                      unsigned char *transfer_buf,
                      unsigned int dos_transfer_buf)
{
  cdimage_t *img = get_image(idx, path);

  if (!img)
    return 0;

  switch (HI(ax)) {
    case 0x01:                 /* init */
      cdrom_status_init();
      LO(ax) = 0;
      break;
    case 0x02:                 /* read long */
      do_read_long(img, req_buf, transfer_buf, dos_transfer_buf);
      break;
    case 0x03:                 /* seek: always succeeds */
      LO(ax) = 0;
      break;
    case 0x08:                 /* sector size */
      LO(ax) = 0;
      LWORD(ebx) = CD_FRAME_COOKED;
      break;
    case 0x09:                 /* media changed */
      HI(ax) = 0;
      LO(ax) = 0;
      LO(bx) = audio_status.media_changed;
      audio_status.media_changed = 0;
      break;
    case 0x0A:                 /* device status */
      HI(ax) = 0;
      LO(ax) = 0;
      LWORD(ebx) = audio_status.status;
      break;
    case 0x0B:                 /* drive reset */
    case 0x0D:                 /* eject: accepted, the image stays mounted */
    case 0x0E:                 /* close tray */
      LO(ax) = 0;
      break;
    case 0x0C:                 /* lock/unlock door */
      if (LO(bx) == 1)
        audio_status.status &= 0xFFFFFFFD;
      else
        audio_status.status |= 0x2;
      LO(ax) = 0;
      break;
    case 0x12:                 /* volume size */
      req_buf = SEG_ADR((unsigned char *), ds, si);
      *CALC_PTR(req_buf, MSCD_GETVOLUMESIZE_SIZE, int) = img->leadout_lba;
      LO(ax) = 0;
      break;
    default:
      /* audio, TOC and Q-channel requests are added separately */
      C_printf("CDROM: image: request %#x not supported yet\n", HI(ax));
      fail(ERR_GENERAL);
      break;
  }
  return 1;
}

void cdrom_img_done(void)
{
  int i;

  for (i = 0; i < NDRIVES; i++) {
    cdimage_close(cd_img[i]);
    cd_img[i] = NULL;
    cd_img_tried[i] = false;
  }
}
