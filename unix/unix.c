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
#include "core/display.h"
#include "lib/std/errno.h"
#include "net/core/iface.h"
#include "core/proc.h"
#include "core/session.h"
#include "lib/std/stdio.h"

#include "unix/display_crs.h"
#include "unix/timer_unix.h"
#include "unix/asy_unix.h"

/* Initialize the machine-dependent I/O (misnomer)
 *
 * This is just another stab at initializattion. In the MS-DOS implementation
 * this function would allocate a heap, set up the file mode to default to
 * O_BINARY, increase the size of the file table (not likely needed here),
 * set up the control-C handler and chain important interrupt handlers.
 *
 * We will use this opportunity to start several imporant threads which
 * will function as the "keyboard" and timer interrupts.
 */
void
ioinit(int hinit)
{

	/* Initialize random number generator */
	srandomdev();

	if (unix_timer_start() != 0) {
		fprintf(stderr, "Can't start timer thread: %s\n",
			strerror(errno));
		exit(1);
	}

	/* Start up CURSES */
	curses_display_start();

	/* start keyboard proc */
	curses_keyboard_start();
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

	curses_keyboard_stop();

	kfcloseall();

	/* curses deinit */
	curses_display_stop();
}

/* Display-tending process */
void
display(int i,void *v1,void *v2)
{
	struct session *sp;
	struct display *dp;
	static struct display *lastdp;
	static long lastkdebug;

	for(;;){
		sp = Current;
		if(sp == NULL || sp->output == NULL
		 || (dp = (struct display *)sp->output->ptr) == NULL){
			/* Something weird happened */
			ppause(500L);
			continue;
		}
#if 0
		statline(dp,sp);
#endif
		dupdate(dp);
		lastdp = dp;
		lastkdebug = Kdebug;
#if 0
		kalarm(100L);	/* Poll status every 100 ms */
#endif
		kwait(dp);
#if 0
		kalarm(0L);
#endif
	}
}

#if 0
/* Compose status line for bottom of screen */
static void
statline(struct display *dp,struct session *sp)
{
	int attr;
	char buf[81];
	struct text_info text_info;
	int unack;
	int s1;
	int s = -1;

	/* Determine attribute to use */
	gettextinfo(&text_info);
	if(text_info.currmode == MONO)
		attr = 0x07;	/* Regular white on black */
	else
		attr = 0x02;	/* Green on black */

	if(sp->network != NULL && (s = kfileno(sp->network)) != -1){
		unack = socklen(s,1);
		if(sp->type == FTP && (s1 = kfileno(sp->cb.ftp->data)) != -1)
			unack += socklen(s1,1);
	}
	memset(buf,' ',80);
	buf[80] = '\0';
	sprintf(&buf[0],"%2d: %s",sp->index,sp->name);

	if(dp->flags.scrollbk){
		int off;
		off = dp->virttop - dp->realtop;
		if(off < 0)
			off += dp->size;
		sprintf(&buf[54],"Scroll:%-5u",off);
	} else if(s != -1 && unack != 0){
		sprintf(&buf[54],"Unack: %-5u",unack);
	} else
		sprintf(&buf[54],"            ");
	sprintf(&buf[66],"F8:nxt F10:cmd");
	statwrite(0,buf,sizeof(buf)-1,attr);
}
#endif

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
