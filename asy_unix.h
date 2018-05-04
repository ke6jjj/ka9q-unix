/* UNIX termios serial port NOS driver, for using serial devices hosted on
 * locally on a UNIX machine. This code was forked from the National
 * Semiconductor 8250/16550 code written by Phil Karn.
 *
 * Copyright 1991 Phil Karn, KA9Q
 * Copyright 2017 Jeremy Cooper, KE6JJJ.
 *
 * Asynchronous devices are traditionally interrupt driven in NOS. But since
 * this is a UNIX driver there are no interrupts to receive. Instead, we will
 * simulate interrupt-like behavior with a continuously running I/O thread
 * which uses the select() call to block until there is I/O to process.
 *
 * The thread will interface with the rest of the NOS code entirely through
 * the network interface queuing, dequeueing and ksignal() calls and it will
 * treat the "disable()" and "restore()" interrupt blocking methods as a lock
 * barrier.
 *
 * asy_read  - TOP - read info buffer, wait until at least one available
 * asy_write - TOP - write a buffer, wait until complete
 *
 * This header defines the private interfaces shared between the
 * UNIX-dependent setup code and the I/O threads for async interfaces.
 */
#ifndef ASY_UNIX_H
#define ASY_UNIX_H

#include "top.h"

#include <pthread.h>

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef _PROC_H
#include "proc.h"
#endif

#ifndef	_IFACE_H
#include "iface.h"
#endif

/* Output pseudo-dma control structure */
struct dma {
	uint8 *data;		/* current output pointer */
	unsigned short cnt;	/* byte count remaining */
	volatile uint8 busy;	/* transmitter active */
};

/* Read fifo control structure */
struct fifo {
	uint8 *buf;		/* Ring buffer */
	unsigned bufsize;	/* Size of ring buffer */
	uint8 *wp;		/* Write pointer */
	uint8 *rp;		/* Read pointer */
	volatile unsigned short cnt;	/* count of characters in buffer */
	unsigned short hiwat;	/* High water mark */
	long overrun;		/* count of sw fifo buffer overruns */
};

/* Asynch controller control block */
struct asy {
	struct iface *iface;
	struct fifo fifo;
	int trigchar;		/* Fifo trigger character */

	pthread_t read_thread;	/* Device input->fifo thread */
	pthread_mutex_t read_lock; /* Read thread start gate */

	pthread_t write_thread;	/* DMA->device output thread */
	pthread_mutex_t write_lock; /* Write thread start gate */
	pthread_cond_t write_ready; /* Write request is waiting */
	int write_exit;		/* Exit writing thread */
	struct dma dma;

	int ttyfd;		/* Device file descriptor */
	int is_real_tty; 	/* tty vs. socket */     
	int cts;		/* CTS/RTS handshaking enabled */
	int speed;		/* current baudrate */

	long rxchar;		/* Received characters */
	long txchar;		/* Transmitted characters */

	struct proc *txproc;
	struct mbuf *txq;
};

extern int Nasy;		/* Actual number of asynch lines */
extern struct asy Asy[];

int asy_shutdown(int dev);

#endif /* ASY_UNIX_H */
