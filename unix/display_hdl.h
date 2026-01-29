#ifndef UNIX_DISPLAY_HDL_H
#define UNIX_DISPLAY_HDL_H
/*
 * Copyright 2026 Jeremy Cooper, KE6JJJ
 *
 * It can be helpful to run NOS as a pure daemon, in which it has no
 * command console and writes logging messages to stderr. This file contains
 * an implementation of a "headless" logging display, from which no input
 * can be read and to which all output will be written to the host stderr.
 */

#include "global.h"
#include "unix/display.h"

struct display * newdisplay_headless(int *do_buffered, const char **eol);
void closedisplay_headless(struct display *dp);
void displaywrite_headless(struct display *dp, const void *buf, int cnt);

#endif
