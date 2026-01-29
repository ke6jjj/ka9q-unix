/* OS and machine-dependent stuff for UNIX, pthreads and CURSES
 *
 * Copyright 1991 Phil Karn, KA9Q
 * Copyright 2017 Jeremy Cooper, KE6JJJ
 */
#include "top.h"

#ifndef UNIX
#error "This file should only be built on POSIX/UNIX systems."
#endif

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>

#include "global.h"
#include "core/asy.h"
#include "commands.h"
#include "lib/std/errno.h"
#include "net/core/iface.h"
#include "lib/std/stdio.h"

#include "unix/display.h"
#include "unix/timer_unix.h"
#include "unix/asy_unix.h"
#include "unix/nosunix.h"

/* Initialize the machine-dependent I/O (misnomer)
 *
 * This is just another stab at initializattion. In the MS-DOS implementation
 * this function would allocate a heap, set up the file mode to default to
 * O_BINARY, increase the size of the file table (not likely needed here),
 * set up the control-C handler and chain important interrupt handlers.
 *
 * We will use this opportunity to start several imporant threads which
 * will function as the "keyboard" and timer interrupts, if asked to do so.
 */
void
ioinit(int hinit, int do_console)
{

	/* Initialize random number generator */
#if HAVE_SRANDOMDEV
	srandomdev();
#else
	srandom(time(NULL));
#endif

	if (unix_timer_start() != 0) {
		fprintf(stderr, "Can't start timer thread: %s\n",
			strerror(errno));
		exit(1);
	}

	unix_display_init(do_console);
}

/* De-install NOS and prepare to return control back to the host
 * operating system.
 */ 
void
iostop(void)
{
	struct iface *ifp, *iftmp;
	void (**fp)(void);
	int i;

	for (ifp = Ifaces; ifp != NULL; ifp = iftmp) {
		iftmp = ifp->next;
		if_detach(ifp);
	}

	/* Call list of shutdown functions */
	for (fp = Shutdown; *fp != NULL; fp++) {
		(**fp)();
	}

	/* Signal all I/O threads to stop, and join them. */
	for (i = 0; i < ASY_MAX; i++)
		asy_shutdown(i);

	unix_display_keyboard_stop();

	kfcloseall();

	unix_display_stop();
}

void
sysreset(void)
{
	exit(0);
}

int
availmem()
{
	/* Without further analysis of the host memory, just return "OK" */
	return 0;
}

uint
lcsum(uint16 *buf,uint cnt)
{
	uint32 sum = 0;

	while(cnt-- != 0)
		sum += *buf++;
	while(sum > 65535)
		sum = (sum & 0xffff) + (sum >> 16);
	return ((sum >> 8) | (sum << 8)) & 0xffff;
}

void *
htop(const char *s)
{
	void *r;
	if (sscanf(s, "%p", &r) != 1)
		/* Pointer didn't parse. Don't let it point randomly */
		r = "BAD POINTER";

	return r;
}

/* Generate a uniformly-distributed random number between 0 and n-1
 * Uses rejection method
 */
int
urandom(n)
unsigned int n;
{
	int32 k, i;

	k = INT32_MAX  - ((int64_t)INT32_MAX+1) % n;
	do {
		i = random() & 0xffffffffL;
		if (i < 0)
			i *= -1;
	} while(i > k);
	return i % n;
}
