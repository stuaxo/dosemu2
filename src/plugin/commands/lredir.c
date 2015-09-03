/*
 * All modifications in this file to the original code are
 * (C) Copyright 1992, ..., 2014 the "DOSEMU-Development-Team".
 *
 * for details see file COPYING in the DOSEMU distribution
 */

/***********************************************
 * File: LREDIR.C
 *  Program for Linux DOSEMU disk redirector functions
 * Written: 10/29/93 by Tim Bird
 * Fixes: 1/7/95 by Tim Josling TEJ: check correct # args
 *      :                            display read/write status correctly
 * Changes: 3/19/95 by Kang-Jin Lee
 *  Removed unused variables
 *  Changes to eliminate warnings
 *  Display read/write status when redirecting drive
 *  Usage information is more verbose
 *
 * Changes: 11/27/97 Hans Lermen
 *  new detection stuff,
 *  rewrote to use plain C and intr(), instead of asm and fiddling with
 *  (maybe used) machine registers.
 *
 * Changes: 990703 Hans Lermen
 *  Checking for DosC and exiting with error, if not redirector capable.
 *  Printing DOS errors in clear text.
 *
 * Changes: 010211 Bart Oldeman
 *   Make it possible to "reredirect": LREDIR drive filepath
 *   can overwrite the direction for the drive.
 *
 * Changes: 20010402 Hans Lermen
 *   Ported to buildin_apps, compiled directly into dosemu
 *
 * Changes 201509 Stuart Axon
 *   Move to same style as unix.c
 *   Fixes to parameter handling to follow the comments.
 *   Remove ancient check for dosc
 *   Remove internal impementation of getcwd
 *   Return ERRORLEVEL if usage shown
 *
 *
 * NOTES:
 *  LREDIR supports the following commands:
 *  LREDIR drive filepath
 *    redirects the indicated drive to the specified linux filepath
 *    drive = the drive letter followed by a colon (ex. 'E:')
 *    filepath has linux, fs and a file path (ex. 'LINUX\FS\USR\SRC')
 *  LREDIR DEL drive
 *    cancels redirection of the indicated drive
 *  LREDIR
 *    shows drives that are currently redirected (mapped)
 *  LREDIR HELP or LREDIR ?
 *    show usage information for LREDIR
 ***********************************************/


#include "config.h"
#include <stdio.h>    /* printf  */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>

#include "emu.h"
#include "memory.h"
#include "msetenv.h"
#include "doshelpers.h"
#include "utilities.h"
#include "lredir.h"
#include "builtins.h"
#include "disks.h"

#define printf	com_printf
#define	intr	com_intr
#define	strncmpi strncasecmp
#define FP_OFF(x) DOSEMU_LMHEAP_OFFS_OF(x)
#define FP_SEG(x) DOSEMU_LMHEAP_SEG

typedef unsigned char uint8;
typedef unsigned int uint16;

#define CARRY_FLAG    1 /* carry bit in flags register */
#define CC_SUCCESS    0

#define DOS_GET_LIST_OF_LISTS  0x5200
#define DOS_GET_SDA_POINTER    0x5D06
#define DOS_GET_REDIRECTION    0x5F02
#define DOS_GET_CWD            0x4700
#define DOS_REDIRECT_DEVICE    0x5F03
#define DOS_CANCEL_REDIRECTION 0x5F04

#define MAX_RESOURCE_STRING_LENGTH  36  /* 16 + 16 + 3 for slashes + 1 for NULL */
#define MAX_RESOURCE_PATH_LENGTH   128  /* added to support Linux paths */
#define MAX_DEVICE_STRING_LENGTH     5  /* enough for printer strings */

#define REDIR_PRINTER_TYPE    3
#define REDIR_DISK_TYPE       4

#define READ_ONLY_DRIVE_ATTRIBUTE 1  /* same as NetWare Lite */

#define KEYWORD_DEL   "DELETE"
#define KEYWORD_DEL_COMPARE_LENGTH  3

#define KEYWORD_HELP   "HELP"
#define KEYWORD_HELP_COMPARE_LENGTH  4

#define DEFAULT_REDIR_PARAM   0

#include "doserror.h"

static int usage (void);
static FAR_PTR /* char far * */ get_list_of_lists(void);
static FAR_PTR /* char far * */ get_sda_pointer(void);
static void init_mfs(void);
static uint16 redirect_device(char *deviceStr, char *resourceStr, uint8 deviceType,
                              uint16 deviceParameter);
static uint16 get_redirection(uint16 redirIndex, char *deviceStr, char **presourceStr,
                              uint8 * deviceType, uint16 * deviceParameter);
static uint16 cancel_redirection(char *deviceStr);
static void display_redirections(void);
static void delete_drive_redirection(char *deviceStr);
static int find_redirection_by_device(char *deviceStr, char **presourceStr);
static int find_fat_redirection_by_device(char *deviceStr, char **presourceStr);


int lredir_main(int argc, char **argv)
{
    uint16 ccode = 0;
    uint16 deviceParam;
    uint8 deviceType = REDIR_DISK_TYPE;
    int carg, ret;
#if 0
    unsigned long dversion;
#endif

    char deviceStr[MAX_DEVICE_STRING_LENGTH];
    char deviceStr2[MAX_DEVICE_STRING_LENGTH];
    char *resourceStr, *resourceStr2;

    char cwdStr[FILENAME_MAX];
    char *cwd;

    /* initialize the MFS, just in case the user didn't run EMUFS.SYS */
    init_mfs();

    /* need to parse the command line */
    /* if no parameters, then just show current mappings */
    if (argc == 1) {
      display_redirections();
      return(0);
    }

    /* tej one parm is either error or HELP/-help etc */
    if (argc == 2 && strncmpi(argv[1], KEYWORD_HELP, KEYWORD_HELP_COMPARE_LENGTH) == 0) {
      return usage();
    }

    if (strncmpi(argv[1], KEYWORD_DEL, KEYWORD_DEL_COMPARE_LENGTH) == 0) {
      delete_drive_redirection(argv[2]);
      return(0);
    }

    /* assume the command is to redirect a drive */
    /* read the drive letter and resource string */
    carg = 3;
    if (argc > 2 && (argv[2][1] == ':' || (argv[2][0] == '.' &&
	    argv[2][1] == '\\'))) {
      char *argv2;
      /* lredir c: d: */
      if (argv[2][1] == '\\') {
        cwd = getcwd(cwdStr, FILENAME_MAX);
        if (cwd != NULL) {
          printf("Error: unable to get CWD\n");
          goto MainExit;
        }
        ret = asprintf(&argv2, "%s\\%s", cwd, argv[2] + 2);
        assert(ret != -1);
      } else {
        argv2 = strdup(argv[2]);
      }
      strncpy(deviceStr, argv[1], sizeof(deviceStr) - 1);
      deviceStr[sizeof(deviceStr) - 1] = 0;
      if (deviceStr[1] == ':')		// may be LPTx
        deviceStr[2] = 0;
      strncpy(deviceStr2, argv2, sizeof(deviceStr2) - 1);
      deviceStr2[sizeof(deviceStr2) - 1] = 0;
      if (deviceStr2[1] == ':')		// may be cwd
        deviceStr2[2] = 0;
      if ((argc > 3 && toupperDOS(argv[3][0]) == 'F') ||
	((ccode = find_redirection_by_device(deviceStr2, &resourceStr2)) != CC_SUCCESS)) {
        if ((ccode = find_fat_redirection_by_device(deviceStr2, &resourceStr2)) != CC_SUCCESS) {
          printf("Error: unable to find redirection for drive %s\n", deviceStr2);
	  goto MainExit;
	}
      }
      if (strlen(argv2) > 3) {
        ret = asprintf(&resourceStr, "%s%s", resourceStr2, argv2 + 3);
        assert(ret != -1);
        free(resourceStr2);
      } else {
        resourceStr = resourceStr2;
      }
      free(argv2);
    } else if (argc > 1 && strchr(argv[1], '\\')) {
	int nextDrive;
        resourceStr = strdup(argv[1]);
	nextDrive = find_drive(&resourceStr);
	if (nextDrive == -26) {
		printf("Cannot redirect (maybe no drives available).");
		return(0);
	} else if (nextDrive == -27) {
		printf("Cannot canonicalize drive root path.\n");
		return(0);
	}
        deviceStr[0] = -nextDrive + 'A';
	deviceStr[1] = ':';
	deviceStr[2] = '\0';
	carg = 2;
    } else if (argc > 2 && strlen(argv[2]) > 1) {
        strncpy(deviceStr, argv[1], sizeof(deviceStr) - 1);
        deviceStr[sizeof(deviceStr) - 1] = 0;
        if (deviceStr[1] == ':')		// may be LPTx
          deviceStr[2] = 0;
        resourceStr = strdup(argv[2]);
    } else if (argc > 1 && isdigit(argv[1][3])) {	// lredir lptX
        strncpy(deviceStr, argv[1], sizeof(deviceStr) - 1);
        deviceStr[sizeof(deviceStr) - 1] = 0;
        resourceStr = strdup(argv[1] + 3);
	carg = 2;
    } else {
	return usage();
    }
    deviceParam = DEFAULT_REDIR_PARAM;

    if (argc > carg) {
      if (toupperDOS(argv[carg][0]) == 'R') {
        deviceParam = 1;
      } else if (toupperDOS(argv[carg][0]) == 'C') {
	int cdrom = 1;
	if (argc > carg+1 && argv[carg+1][0] >= '1' && argv[carg+1][0] <= '4')
	  cdrom = argv[carg+1][0] - '0';
        deviceParam = 1 + cdrom;
      } else if (toupperDOS(argv[carg][0]) == 'P') {
        char *old_rs = resourceStr;
        ret = asprintf(&resourceStr, "%s\\%s", "LINUX\\PRN", old_rs);
        assert(ret != -1);
        free(old_rs);
        deviceType = REDIR_PRINTER_TYPE;
      } else {
	printf("lredir: Unknown parameter %c\n", argv[carg][0]);
	return(0);
      }
    }

    /* upper-case both strings */
    strupperDOS(deviceStr);
    strupperDOS(resourceStr);

    /* now actually redirect the drive */
    ccode = redirect_device(deviceStr, resourceStr, deviceType,
                           deviceParam);

    /* duplicate redirection: try to reredirect */
    if (ccode == 0x55) {
      delete_drive_redirection(deviceStr);
      ccode = redirect_device(deviceStr, resourceStr, deviceType,
                             deviceParam);
    }

    if (ccode) {
      printf("Error %x (%s)\nwhile redirecting drive %s to %s\n",
             ccode, decode_DOS_error(ccode), deviceStr, resourceStr);
      free(resourceStr);
      goto MainExit;
    }
    if ((argc <= 2 || argv[2][1] != ':') && argc >= 2 && argv[1][1] != ':')
	msetenv("DOSEMU_LASTREDIR", deviceStr);

    printf("%s = %s", deviceStr, resourceStr);
    free(resourceStr);
    if (deviceParam > 1)
      printf(" CDROM:%d", deviceParam - 1);
    printf(" attrib = ");
    if ((deviceParam != 0) == READ_ONLY_DRIVE_ATTRIBUTE)
      printf("READ ONLY\n");
    else
      printf("READ/WRITE\n");

MainExit:
    return (ccode);
}

static int usage (void)
{
  printf("Usage: LREDIR [[drive:] LINUX\\FS\\path [R] | [C [n]] | HELP]\n");
  printf("Redirect a drive to the Linux file system.\n\n");
  printf("LREDIR X: LINUX\\FS\\tmp\n");
  printf("  Redirect drive X: to /tmp of Linux file system for read/write\n");
  printf("  If R is specified, the drive will be read-only\n");
  printf("  If C is specified, (read-only) CDROM n is used (n=1 by default)\n");
  printf("  ${home} represents user's home directory\n\n");
  printf("  If drive is not specified, the next available drive will be used.\n\n");
  printf("LREDIR X: Y:\n");
  printf("  Redirect drive X: to where the drive Y: is redirected.\n");
  printf("  If F is specified, the path for Y: is taken from its emulated "
         "FAT volume.\n\n");
  printf("LREDIR DEL drive:\n");
  printf("  delete a drive redirection\n\n");
  printf("LREDIR\n");
  printf("  show current drive redirections\n\n");
  printf("LREDIR HELP\n");
  printf("  show this help screen\n");

  return (1);
}

static FAR_PTR /* char far * */
get_list_of_lists(void)
{
    FAR_PTR LOL;
    struct REGPACK preg = REGPACK_INIT;

    preg.r_ax = DOS_GET_LIST_OF_LISTS;
    intr(0x21, &preg);
    LOL = MK_FP(preg.r_es, preg.r_bx);
    return (LOL);
}

static FAR_PTR /* char far * */
get_sda_pointer(void)
{
    FAR_PTR SDA;
    struct REGPACK preg = REGPACK_INIT;

    preg.r_ax = DOS_GET_SDA_POINTER;
    intr(0x21, &preg);
    SDA = MK_FP(preg.r_ds, preg.r_si);

    return (SDA);
}

/********************************************
 * init_mfs - call Emulator to initialize MFS
 ********************************************/
/* tej - changed return type to void as nothing returned */
static void init_mfs(void)
{
    FAR_PTR LOL;
    FAR_PTR SDA;
    unsigned char _osmajor;
    unsigned char _osminor;
    state_t preg;

    LOL = get_list_of_lists();
    SDA = get_sda_pointer();

    /* now get the DOS version */
    pre_msdos();
    HI(ax) = 0x30;
    call_msdos();
    _osmajor = LO(ax);
    _osminor = HI(ax);
    post_msdos();

    /* get DOS version into CX */
    preg.ecx = _osmajor | (_osminor <<8);

    preg.edx = FP_OFF16(LOL);
    preg.es = FP_SEG16(LOL);
    preg.esi = FP_OFF16(SDA);
    preg.ds = FP_SEG16(SDA);
    preg.ebx = 0x500;
    mfs_helper(&preg);
}

/********************************************
 * redirect_device - redirect a device to a remote resource
 * ON ENTRY:
 *  deviceStr has a string with the device name:
 *    either disk or printer (ex. 'D:' or 'LPT1')
 *  resourceStr has a string with the server and name of resource
 *    (ex. 'TIM\TOOLS')
 *  deviceType indicates the type of device being redirected
 *    3 = printer, 4 = disk
 *  deviceParameter is a value to be saved with this redirection
 *  which will be returned on get_redirectionList
 * ON EXIT:
 *  returns CC_SUCCESS if the operation was successful,
 *  otherwise returns the DOS error code
 * NOTES:
 *  deviceParameter is used in DOSEMU to return the drive attribute
 *  It is not actually saved and returned as specified by the redirector
 *  specification.  This type of usage is common among commercial redirectors.
 ********************************************/
static uint16 redirect_device(char *deviceStr, char *resourceStr, uint8 deviceType,
                      uint16 deviceParameter)
{
    char slashedResourceStr[MAX_RESOURCE_PATH_LENGTH];
    struct REGPACK preg = REGPACK_INIT;
    char *dStr, *sStr;

    /* prepend the resource string with slashes */
    strcpy(slashedResourceStr, "\\\\");
    strcat(slashedResourceStr, resourceStr);

    /* should verify strings before sending them down ??? */
    dStr = com_strdup(deviceStr);
    preg.r_ds = FP_SEG(dStr);
    preg.r_si = FP_OFF(dStr);
    sStr = com_strdup(slashedResourceStr);
    preg.r_es = FP_SEG(sStr);
    preg.r_di = FP_OFF(sStr);
    preg.r_cx = deviceParameter;
    preg.r_bx = deviceType;
    preg.r_ax = DOS_REDIRECT_DEVICE;
    intr(0x21, &preg);
    com_strfree(dStr);
    com_strfree(sStr);

    if (preg.r_flags & CARRY_FLAG) {
      return (preg.r_ax);
    }
    else {
      return (CC_SUCCESS);
    }
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

/********************************************
 * cancel_redirection - delete a device mapped to a remote resource
 * ON ENTRY:
 *  deviceStr has a string with the device name:
 *    either disk or printer (ex. 'D:' or 'LPT1')
 * ON EXIT:
 *  returns CC_SUCCESS if the operation was successful,
 *  otherwise returns the DOS error code
 * NOTES:
 *
 ********************************************/
static uint16 cancel_redirection(char *deviceStr)
{
    struct REGPACK preg = REGPACK_INIT;
    char *dStr;

    dStr = com_strdup(deviceStr);
    preg.r_ds = FP_SEG(dStr);
    preg.r_si = FP_OFF(dStr);
    preg.r_ax = DOS_CANCEL_REDIRECTION;
    intr(0x21, &preg);
    com_strfree(dStr);

    if (preg.r_flags & CARRY_FLAG) {
      return (preg.r_ax);
    }
    else {
      return (CC_SUCCESS);
    }
}

/*************************************
 * display_redirections
 *  show my current drive redirections
 * NOTES:
 *  I show the read-only attribute for each drive
 *    which is returned in deviceParam.
 *************************************/
static void
display_redirections(void)
{
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
          printf("Current Drive Redirections:\n");
        }
        driveCount++;
        printf("%-2s = %-20s ", deviceStr, resourceStr);

        /* read attribute is returned in the device parameter */
        if (deviceParam & 0x80) {
	  if ((deviceParam & 0x7f) > 1)
	    printf("CDROM:%d ", (deviceParam & 0x7f) - 1);
          if (((deviceParam & 0x7f) != 0) == READ_ONLY_DRIVE_ATTRIBUTE)
	    printf("attrib = READ ONLY\n");
	  else
	    printf("attrib = READ/WRITE\n");
        }
      }

      free(resourceStr);
      redirIndex++;
    }

    if (driveCount == 0) {
      printf("No drives are currently redirected to Linux.\n");
    }
}

static void
delete_drive_redirection(char *deviceStr)
{
    uint16 ccode;

    /* convert device string to upper case */
    strupperDOS(deviceStr);
    ccode = cancel_redirection(deviceStr);
    if (ccode) {
      printf("Error %x (%s)\ncanceling redirection on drive %s\n",
             ccode, decode_DOS_error(ccode), deviceStr);
      }
    else {
      printf("Redirection for drive %s was deleted.\n", deviceStr);
    }
}

static int find_redirection_by_device(char *deviceStr, char **presourceStr)
{
    uint16 redirIndex = 0, deviceParam, ccode;
    uint8 deviceType;
    char dStr[MAX_DEVICE_STRING_LENGTH];
    char dStrSrc[MAX_DEVICE_STRING_LENGTH];

    snprintf(dStrSrc, MAX_DEVICE_STRING_LENGTH, "%s", deviceStr);
    strupperDOS(dStrSrc);
    while ((ccode = get_redirection(redirIndex, dStr, presourceStr,
                           &deviceType, &deviceParam)) == CC_SUCCESS) {
      if (strcmp(dStrSrc, dStr) == 0)
        break;
      redirIndex++;
    }

    return ccode;
}

static int find_fat_redirection_by_device(char *deviceStr, char **presourceStr)
{
    struct DINFO *di;
    char *dir;
    fatfs_t *f;
    int ret;

    if (!(di = (struct DINFO *)lowmem_alloc(sizeof(struct DINFO))))
	return 0;
    pre_msdos();
    LWORD(eax) = 0x6900;
    LWORD(ebx) = toupperDOS(deviceStr[0]) - 'A' + 1;
    REG(ds) = FP_SEG(di);
    LWORD(edx) = FP_OFF(di);
    call_msdos();
    if (REG(eflags) & CF) {
	post_msdos();
	lowmem_free((void *)di, sizeof(struct DINFO));
	printf("error retrieving serial, %#x\n", LWORD(eax));
	return -1;
    }
    post_msdos();
    f = get_fat_fs_by_serial(READ_DWORDP((unsigned char *)&di->serial));
    lowmem_free((void *)di, sizeof(struct DINFO));
    if (!f || !(dir = f->dir)) {
	printf("error identifying FAT volume\n");
	return -1;
    }

    ret = asprintf(presourceStr, "LINUX\\FS%s", dir);
    assert(ret != -1);

    return CC_SUCCESS;
}

