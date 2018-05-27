/* UNIX "timer" driver for NOS, for reckoning time and delivering timer events
 * when NOS is hosted locally on a UNIX machine.
 *
 * Copyright 2017 Jeremy Cooper, KE6JJJ.
 *
 * Timer devices are traditionally interrupt driven in NOS. But since
 * this is a UNIX driver there are no interrupts to receive. Instead, we will
 * simulate interrupt-like behavior with a thread that sleeps until a timer
 * "tick" has elapsed. It will then wake up the "timer" NOS process, simulating
 * a PC hardware tick.
 *
 * The thread will interface with the rest of the NOS code entirely through
 * the "Tick" global variable and the ksignal() calls. It will treat the
 * "disable()" and "restore()" interrupt blocking methods as a lock
 * barrier.
 */
#include "top.h"

#ifndef UNIX
#error "This file should only be built on POSIX/UNIX systems."
#endif

#include <sys/time.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "global.h"
#include "proc.h"
#include "timer.h"
#include "timer_unix.h"
#include "nosunix.h"

static void *timer_proc(void *dummy);

/* The global timer ticking thread */
static pthread_t g_timer_thread;

/* Startup time */
static struct timeval g_start_time;

/* The global timer tick count */
int Tick;

/* Initialize the timer tick thread */
int
unix_timer_start(void)
{
	gettimeofday(&g_start_time, NULL);
	return pthread_create(&g_timer_thread, NULL, timer_proc, NULL);
}

/* Stop timer tick thread */
int
unix_timer_stop(void)
{
	void *dummy;

	pthread_cancel(g_timer_thread);
	return pthread_join(g_timer_thread, &dummy);
}

int32
msclock(void)
{
	struct timeval now;
	int64_t duration_u;

	gettimeofday(&now, NULL);
	duration_u = ((int64_t)(now.tv_sec - g_start_time.tv_sec)) * 1000000;
	duration_u += (now.tv_usec - g_start_time.tv_usec);
	return duration_u / 1000;
}

int32
secclock(void)
{
	return msclock() / 1000;
}

int32
rdclock(void)
{
	return msclock() / MSPTICK;
}
	
static void *
timer_proc(void *dummy)
{
	struct timeval last, next, now;
	int64_t duration_u;

	gettimeofday(&last, NULL);

	for (;;) {
		next.tv_usec = last.tv_usec + MSPTICK * 1000;
		next.tv_sec = last.tv_sec;
		if (next.tv_usec > 1000000) {
			next.tv_sec++;
			next.tv_usec -= 1000000;
		}
		do {
			gettimeofday(&now, NULL);
			duration_u = (next.tv_sec - now.tv_sec) * 1000000;
			duration_u += (next.tv_usec - now.tv_usec);
			if (duration_u > 0)
				usleep(duration_u);
		} while (duration_u > 0);
		interrupt_enter();
		Tick++;
		ksignal(&Tick,1);
		interrupt_leave();
		last.tv_sec = next.tv_sec;
		last.tv_usec = next.tv_usec;
	}

	return NULL;
}
