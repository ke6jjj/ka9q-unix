/*
 * Copyright 2026 Jeremy Cooper, KE6JJJ
 *
 * It can be helpful to run NOS as a pure daemon, in which it has no
 * command console and writes logging messages to stderr. This file contains
 * an implementation of a "headless" logging display, from which no input
 * can be read and to which all output will be written to the host stderr.
 */
#include "top.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "lib/std/errno.h"
#include "global.h"
#include "core/display.h"
#include "core/proc.h"

#include "unix/display_hdl.h"

/* Create a new "headless", logging-only display.
 * This is a UNIX-only feature which turns a KA9Q process into more of a
 * background daemon that does not have any interactive foreground console.
 * Writes to this display will go directly to the hosted stderr descriptor,
 * which often might go straight to the system log (via the `daemon` utility
 * on BSD or `systemd` on Linux).
 */
struct display *
newdisplay_headless(int *do_buffered, const char **desired_eol)
{
	struct display *dp;

	dp = (struct display *)calloc(1,sizeof(struct display));
	dp->cookie = D_COOKIE_HEADLESS;
	*do_buffered = FALSE;
	*desired_eol = "\n";

	return dp;
}

void
closedisplay_headless(struct display *dp)
{
	free(dp);
}

void
displaywrite_headless(struct display *dp, const void *buf, int cnt)
{
	(void) write(STDERR_FILENO, buf, cnt);
}
