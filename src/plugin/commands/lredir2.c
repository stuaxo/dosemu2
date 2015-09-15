/*
 * All modifications in this file to the original code are
 * (C) Copyright 1992, ..., 2014 the "DOSEMU-Development-Team".
 *
 * for details see file COPYING in the DOSEMU distribution
 */

/***********************************************
 * File: lredir2.c
 *  Program for Linux DOSEMU drive and printer redirector 
 *  functions.
 *
 * Where sensible this tries to use the same syntax as MOUNT
 * in DOSBOX.
 *
 * NOTES:
 *  LREDIR supports the following commands:
 * Mount a directory as a drive:
 *  LREDIR drive-letter directory
 * Unmount a drive:
 *  LREDIR -u drive-letter
 *
 ***********************************************/

/**
 * TODO
 *   implement drive redirection, ideally keepign to the dosbox syntax at
 *     http://www.dosbox.com/wiki/MOUNT
 *
 *   printer redirection
 */

#include "config.h"
#include <stdio.h>    /* printf  */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdbool.h>

#include "builtins.h"

/*
#include "emu.h"
#include "memory.h"
#include "msetenv.h"
#include "doshelpers.h"
#include "utilities.h"
#include "lredir.h"
#include "builtins.h"
#include "disks.h"
*/

#define printf	com_printf
#define	intr	com_intr
#define	strncmpi strncasecmp
#define FP_OFF(x) DOSEMU_LMHEAP_OFFS_OF(x)
#define FP_SEG(x) DOSEMU_LMHEAP_SEG


//// - added for get_redirection from lredir
typedef unsigned char uint8;
typedef unsigned int uint16;

#define CARRY_FLAG    1 /* carry bit in flags register */
#define CC_SUCCESS    0

#define MAX_RESOURCE_STRING_LENGTH  36  /* 16 + 16 + 3 for slashes + 1 for NULL */
#define MAX_RESOURCE_PATH_LENGTH   128  /* added to support Linux paths */
#define MAX_DEVICE_STRING_LENGTH     5  /* enough for printer strings */

#define DOS_GET_REDIRECTION    0x5F02

#define REDIR_PRINTER_TYPE    3
#define REDIR_DISK_TYPE       4

#define READ_ONLY_DRIVE_ATTRIBUTE 1  /* same as NetWare Lite */

//// - /added for get_redirection from lredir

static int usage (void);
static int list_redirections (bool, bool);
static int list_drives (char *drives, char *paths, int num_paths);
static int list_printers (char *printer_list, int num_printers);
static uint16 get_redirection(uint16 redirIndex, char *deviceStr, char **presourceStr,
                      uint8 * deviceType, uint16 * deviceParameter);

struct DriveRedirection
{
  // TODO - add drive type here as well
  char drive;
  char *host_path;
  char *mfs_path;
};

struct PrinterRedirection
{
  char *guest_printer;
  char *host_printer;
};

int lredir2_main(int argc, char **argv)
{
  argv++;
  if(!--argc) {
     return list_redirections(true, true);
  }
  return usage();
}

static int usage(void)
{
  printf(
    "Usage: LREDIR2 [[-u] [drive-letter] [/unix/path]]\n"
  );
  return 1;
}

/**
 * Output drive and printer redirections
 */
static int list_redirections(bool show_drives, bool show_printers) {
  if (show_drives) {
    printf("drives\n");
    list_drives(NULL, NULL, 0);
  }
  if (show_printers) {
    printf("printers\n");
    list_printers(NULL, 0);
  }
  return 0;
}

/*************************************
 * display_redirections
 *  show my current drive redirections
 * NOTES:
 *  I show the read-only attribute for each drive
 *    which is returned in deviceParam.
 *************************************/
// FROM LREDIR
static void
old_display_redirections(void)
{
  // TODO
  // Drive H is mounted as local directory /home/username
    int driveCount;
    uint16 redirIndex, deviceParam, ccode;
    uint8 deviceType;

    char deviceStr[MAX_DEVICE_STRING_LENGTH];
    char *resourceStr;

    redirIndex = 0;
    driveCount = 0;

    while ((ccode = get_redirection(redirIndex, deviceStr, &resourceStr,
                           &deviceType, &deviceParam)) == CC_SUCCESS) {
      /* only print disk redirections here */
      if (deviceType == REDIR_DISK_TYPE) {
        if (driveCount == 0) {
          printf("Current mounted drives are:\n");
        }
        driveCount++;
        printf("Drive %-2s is mounted as local directory %-20s ", deviceStr, resourceStr);

        /* read attribute is returned in the device parameter */
        if (deviceParam & 0x80) {
	  if ((deviceParam & 0x7f) > 1) {
	    printf("CDROM:%d ", (deviceParam & 0x7f) - 1);
      }
      else {
      if (((deviceParam & 0x7f) != 0) == READ_ONLY_DRIVE_ATTRIBUTE)
	    printf("attrib = READ ONLY\n");
	  else
	    printf("attrib = READ/WRITE\n");
        }
      }
      }

      free(resourceStr);
      redirIndex++;
    }

    if (driveCount == 0) {
      printf("No drives are currently redirected to Linux.\n");
    }
}

/**
 * Output list of drive redirections
 *
 * @param *drives NULL or array of drive letters to check 
 */
// TODO add filtering for paths too
static int list_drives (char *drives, char *paths, int num_paths) {
  if (drives == NULL || num_paths == 0) {
    printf("  TODO - List all drive redirections\n");
  }
  else {
    printf("  TODO - List filtered drive redirections\n");
  }
  //// spike - show redirection of drive C
  printf("test:\n");
  old_display_redirections();
  //// /spike - show redirection of drive C
  return 0;
}

/**
 * Output list of printer redirections.
 *
 * @param printer_list if set only show these printers.
 */
static int list_printers (char *printers, int num_printers) {
  if (printers == NULL || num_printers == 0) {
    printf("  TODO - List all printer redirections\n");
  }
  else {
    printf("  TODO - List filtered printer redirections\n");
  }
  return 0;
}

/********************************************
 * get_redirection - get next entry from list of redirected devices
 * ON ENTRY:
 *  redirIndex has the index of the next device to return
 *    this should start at 0, and be incremented between calls
 *    to retrieve all elements of the redirection list
 * ON EXIT:
 *  returns CC_SUCCESS if the operation was successful, and
 *  deviceStr has a string with the device name:
 *    either disk or printer (ex. 'D:' or 'LPT1')
 *  resourceStr has a string with the server and name of resource
 *    (ex. 'TIM\TOOLS')
 *  deviceType indicates the type of device which was redirected
 *    3 = printer, 4 = disk
 *  deviceParameter has my rights to this resource
 * NOTES:
 *
 ********************************************/
// FROM LREDIR
static uint16 get_redirection(uint16 redirIndex, char *deviceStr, char **presourceStr,
                      uint8 * deviceType, uint16 * deviceParameter)
{
    uint16 ccode;
    uint8 deviceTypeTemp;
    char slashedResourceStr[MAX_RESOURCE_PATH_LENGTH];
    struct REGPACK preg = REGPACK_INIT;
    char *dStr, *sStr;

    dStr = lowmem_alloc(16);
    preg.r_ds = FP_SEG(dStr);
    preg.r_si = FP_OFF(dStr);
    sStr = lowmem_alloc(128);
    preg.r_es = FP_SEG(sStr);
    preg.r_di = FP_OFF(sStr);
    preg.r_bx = redirIndex;
    preg.r_ax = DOS_GET_REDIRECTION;
    intr(0x21, &preg);
    strcpy(deviceStr,dStr);
    lowmem_free(dStr, 16);
    strcpy(slashedResourceStr,sStr);
    lowmem_free(sStr, 128);

    ccode = preg.r_ax;
    deviceTypeTemp = preg.r_bx & 0xff;       /* save device type before C ruins it */
    *deviceType = deviceTypeTemp;
    *deviceParameter = preg.r_cx;

    /* copy back unslashed portion of resource string */
    if (preg.r_flags & CARRY_FLAG) {
      return (ccode);
    }
    else {
      /* eat the leading slashes */
      *presourceStr = strdup(slashedResourceStr + 2);
      return (CC_SUCCESS);
    }
}

