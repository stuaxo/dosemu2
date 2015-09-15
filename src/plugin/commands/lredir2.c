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

#include <ctype.h>
#include <unistd.h>


#include <stdbool.h>

#define printf	com_printf

static int usage (void);
static int list_redirections (bool, bool);
static int list_drives (char *drives, char *paths, int num_paths);
static int list_printers (char *printer_list, int num_printers);

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

