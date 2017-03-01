/* UNIX "timer" driver for NOS, for reckoning time and delivering timer events
 * when NOS is hosted locally on a UNIX machine.
 *
 * Copyright 2017 Jeremy Cooper, KE6JJJ.
 *
 * Timer devices are traditionally interrupt driven in NOS. But since
 * this is a process running on a UNIX host there are no interrupts to
 * receive. Instead, we will simulate interrupt-like behavior with a thread
 * that sleeps until a timer "tick" has elapsed. It will then wake up the
 * "timer" NOS process, simulating a timer interrupt just like NOS would
 * experience on PC hardware.
 *
 * The thread will interface with the rest of the NOS code entirely through
 * the "Tick" global variable and the ksignal() calls. It will treat the
 * "disable()" and "restore()" interrupt blocking methods as a lock
 * barrier.
 */
#ifndef TIMER_UNIX_H
#define TIMER_UNIX_H

#include "top.h"

#ifndef UNIX
#error "This file should only be built on POSIX/UNIX systems."
#endif

/* Initialize the timer tick thread */
int unix_timer_start(void);
/* Stop timer tick thread */
int unix_timer_stop(void);

#endif /* TIMER_UNIX_H */
