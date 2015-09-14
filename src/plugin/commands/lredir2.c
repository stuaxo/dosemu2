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

#define printf	com_printf

static int usage (void);


int lredir2_main(int argc, char **argv)
{
  return usage();
}

static int usage(void)
{
  printf(
    "Usage: LREDIR2 [[-u] [drive-letter] [/unix/path]]\n"
  );
  return (1);
}


