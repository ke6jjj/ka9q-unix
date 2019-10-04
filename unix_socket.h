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
 * This header defines the private interfaces shared between the
 * UNIX-dependent setup code and the I/O threads for async interfaces.
 */
#ifndef KA9Q_UNIX_SOCKET_H
#define KA9Q_UNIX_SOCKET_H

#include "top.h"

#include <pthread.h>

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef _PROC_H
#include "proc.h"
#endif

/* Output pseudo-dma control structure */
struct unix_socket_dma {
	uint8 *data;		/* current output pointer */
	unsigned short cnt;	/* byte count remaining */
	volatile uint8 busy;	/* transmitter active */
};

/* Read fifo control structure */
struct unix_socket_fifo {
	uint8 *buf;		/* Ring buffer */
	unsigned bufsize;	/* Size of ring buffer */
	uint8 *wp;		/* Write pointer */
	uint8 *rp;		/* Read pointer */
	volatile unsigned short cnt;	/* count of characters in buffer */
	unsigned short hiwat;	/* High water mark */
	long overrun;		/* count of sw fifo buffer overruns */
};

/* Unix socket control block */
struct unix_socket_entry {

	pthread_t read_thread;	/* Device input->fifo thread */
	pthread_mutex_t read_lock; /* Read thread start gate */

	pthread_t write_thread;	/* DMA->device output thread */
	pthread_mutex_t write_lock; /* Write thread start gate */
	pthread_cond_t write_ready; /* Write request is waiting */
	int write_exit;		/* Exit writing thread */
	int trigchar;

	struct unix_socket_dma dma;
	struct unix_socket_fifo fifo;

	int ttyfd;		/* Device file descriptor */
	int is_real_tty; 	/* tty vs. socket */

	int cts;		/* CTS/RTS handshaking enabled */
	int speed;		/* current baudrate */

	long rxchar;		/* Received characters */
	long txchar;		/* Transmitted characters */
};

extern	struct unix_socket_entry * unix_socket_create(const char *path,
	    uint bufsize, int trigchar, long speed, int cts);
extern	int unix_socket_shutdown(struct unix_socket_entry *us);
extern	int unix_socket_read(struct unix_socket_entry *us, void *buf,
	    unsigned short cnt);
extern	int unix_socket_write(struct unix_socket_entry *us, const void *buf,
	    unsigned short cnt);

extern	int unix_socket_tx_dma_busy(struct unix_socket_entry *us);
extern	int unix_socket_get_trigchar(struct unix_socket_entry *us);
extern	int unix_socket_set_trigchar(struct unix_socket_entry *us, int trigchar);

extern	int unix_socket_set_line_speed(struct unix_socket_entry *us, long bps);
extern	int32 unix_socket_ioctl(struct unix_socket_entry *us, int cmd,
	    int set, int32 val);
extern	int unix_socket_modem_bits(struct unix_socket_entry *us, int setbits,
	    int clearbits, int *readbits);

#endif /* KA9Q_UNIX_SOCKET_H */
