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
 * which blocks until there is I/O to process.
 *
 * The thread will interface with the rest of the NOS code entirely through
 * the network interface queuing, dequeueing and ksignal() calls and it will
 * treat the "disable()" and "restore()" interrupt blocking methods as a lock
 * barrier.
 *
 * asy_read  - TOP - read info buffer, wait until at least one available
 * asy_write - TOP - write a buffer, wait until complete
 */
#include "top.h"

#ifndef UNIX
#error "This file should only be built on POSIX/UNIX systems."
#endif

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <termios.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#include "stdio.h"
#include <errno.h>
#include "errno.h"
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#include "asy.h"
#include "devparam.h"
#include "dialer.h"
#include "asy_unix.h"
#include "nosunix.h"

#include "unix_socket.h"

struct asy Asy[ASY_MAX];

static void pasy(struct asy *asyp);
static void asy_tx(int dummy0, void *app, void *dummy1);

/* Initialize asynch port "dev" */
int
asy_init(
int dev,
struct iface *ifp,
const char *path,
uint bufsize,
int trigchar,
long speed,
int cts		/* Use CTS flow control */
){
	struct asy *ap;
	char *procname;

	if(dev >= ASY_MAX)
		return -1;

	ap = &Asy[dev];

	ap->socket_entry = unix_socket_create(path, bufsize, trigchar, speed, cts);
	if (ap->socket_entry == NULL) {
		kprintf("can't allocate/connect a unix socket\n");
		goto error;
	}

	ap->iface = ifp;
	ap->txq = NULL;

	/* Spawn the transmit deque process */
	procname = if_name(ifp, " asytx");
        ap->txproc = newproc(procname, 768, asy_tx, 0, ap, NULL, 0);
	free(procname);
	if (ap->txproc == NULL) {
		kprintf("Can't start asy tx process.\n");
		goto error;
	}

	return 0;
error:
	if (ap->socket_entry != NULL) {
		unix_socket_shutdown(ap->socket_entry);
		ap->socket_entry = NULL;
	}
	return -1;
}

int
asy_stop(struct iface *ifp)
{
	return asy_shutdown(ifp->dev);
}


/* Set asynch line speed */
int
asy_speed(
int dev,
long bps
){
	struct asy *asyp;

	if(dev >= ASY_MAX)
		return -1;
	asyp = &Asy[dev];
	if(asyp->iface == NULL)
		return -1;

	if(bps == 0)
		return -1;

	return unix_socket_set_line_speed(asyp->socket_entry, bps);
}

/* Asynchronous line I/O control */
int32
asy_ioctl(
struct iface *ifp,
int cmd,
int set,
int32 val
){
	struct asy *ap = &Asy[ifp->dev];

	return unix_socket_ioctl(ap->socket_entry, cmd, set, val);
}

/* Open an asynch port for direct I/O, temporarily suspending any
 * packet-mode operations. Returns device number for asy_write and get_asy
 */
int
asy_open(char *name)
{
	struct iface *ifp;
	int dev;

	if((ifp = if_lookup(name)) == NULL){
		errno = ENODEV;
		return -1;
	}
	if((dev = ifp->dev) >= ASY_MAX || Asy[dev].iface != ifp){
		errno = EINVAL;
		return -1;
	}
	/* Suspend the packet drivers */
	suspend(ifp->rxproc);
	suspend(ifp->txproc);
	suspend(Asy[dev].txproc);

	/* bring the line up (just in case) */
	if(ifp->ioctl != NULL)
		(*ifp->ioctl)(ifp,PARAM_UP,TRUE,0L);
	return dev;
}

int
asy_close(int dev)
{
	struct iface *ifp;

	if(dev < 0 || dev >= ASY_MAX){
		errno = EINVAL;
		return -1;
	}
	/* Resume the packet drivers */
	if((ifp = Asy[dev].iface) == NULL){
		errno = EINVAL;
		return -1;
	}
	resume(ifp->rxproc);
	resume(ifp->txproc);
	resume(Asy[dev].txproc);
	return 0;
}

int
asy_read(int dev, void *buf, unsigned short cnt)
{
	struct asy *asyp;

	if(dev < 0 || dev >= ASY_MAX)
		return -1;
	asyp = &Asy[dev];

	return unix_socket_read(asyp->socket_entry, buf, cnt);
}

/* Send a buffer on the serial transmitter and wait for completion */
int
asy_write(
int dev,
const void *buf,
unsigned short cnt
){
	struct asy *asyp;

	if(dev < 0 || dev >= ASY_MAX)
		return -1;
	asyp = &Asy[dev];

	return unix_socket_write(asyp->socket_entry, buf, cnt);
}

/* Blocking read from asynch line
 * Returns character or -1 if aborting
 */
int
get_asy(int dev)
{
	struct asy *asyp;
	uint8 c;
	int tmp;

	if(dev < 0 || dev >= ASY_MAX)
		return -1;
	asyp = &Asy[dev];

	if((tmp = unix_socket_read(asyp->socket_entry, &c, 1)) == 1)
		return c;
	else
		return tmp;
}

int
doasystat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct asy *asyp;
	struct iface *ifp;
	int i;

	if(argc < 2){
		for(asyp = Asy;asyp < &Asy[ASY_MAX];asyp++){
			if(asyp->iface != NULL)
				pasy(asyp);
		}
		return 0;
	}
	for(i=1;i<argc;i++){
		if((ifp = if_lookup(argv[i])) == NULL){
			printf("Interface %s unknown\n",argv[i]);
			continue;
		}
		for(asyp = Asy;asyp < &Asy[ASY_MAX];asyp++){
			if(asyp->iface == ifp){
				pasy(asyp);
				break;
			}
		}
		if(asyp == &Asy[ASY_MAX])
			printf("Interface %s not asy\n",argv[i]);
	}

	return 0;
}

static void
pasy(struct asy *asyp)
{
	struct unix_socket_stats stats;
	int mcr;

	printf("%s:",asyp->iface->name);
	if(unix_socket_get_trigchar(asyp->socket_entry) != -1)
		kprintf(" [trigger 0x%02x]",unix_socket_get_trigchar(asyp->socket_entry));
	if(unix_socket_flowcontrol_cts(asyp->socket_entry))
		kprintf(" [cts flow control]");

	kprintf(" %lu bps\n",unix_socket_get_speed(asyp->socket_entry));

	if (unix_socket_is_real_tty(asyp->socket_entry)) {
		if (unix_socket_modem_bits(asyp->socket_entry, 0, 0, &mcr) != 0)
			kprintf("modem bits error: %s\n", strerror(errno));
			return;

		kprintf(" MC: DTR %s  RTS %s  CTS %s  DSR %s  RI %s  CD %s\n",
	 	(mcr & TIOCM_DTR) ? "On" : "Off",
	 	(mcr & TIOCM_RTS) ? "On" : "Off",
	 	(mcr & TIOCM_CTS) ? "On" : "Off",
	 	(mcr & TIOCM_DSR) ? "On" : "Off",
	 	(mcr & TIOCM_RI) ? "On" : "Off",
	 	(mcr & TIOCM_CD) ? "On" : "Off");
	} else {
		kprintf(" TCP socket, no control bits.\n");
	}
	(void) unix_socket_get_stats(asyp->socket_entry, &stats);

	kprintf(" RX: chars %lu", stats.rxchar);
	kprintf(" sw over %lu sw hi %u\n", stats.fifo_overrun, stats.fifo_hiwat);
	kprintf(" TX: chars %lu %s\n", stats.txchar,
	 unix_socket_tx_dma_busy(asyp->socket_entry) ? " BUSY" : "");
}

/* Send a message on the specified serial line */
int
asy_send(dev,bpp)
int dev;
struct mbuf **bpp;
{
	if(dev < 0 || dev >= ASY_MAX){
		free_p(bpp);
		return -1;
	}

	enqueue(&Asy[dev].txq, bpp);

	return 0;
}

static void
asy_tx(int dummy0, void *asyp, void *dummy1)
{
	struct asy *ap = (struct asy *)asyp;
	struct mbuf *bp;

	for (;;) {
		while ((bp = dequeue(&ap->txq)) == NULL)
			kwait(&ap->txq);
	
		while(bp != NULL) {
			/* Send the buffer */
			unix_socket_write(ap->socket_entry, bp->data, bp->cnt);
			/* Now do next buffer on chain */
			free_mbuf(&bp);
		}
	}
}

int
asy_shutdown(int dev)
{
	struct asy *ap;
	struct iface *ifp;
	int i_state;

	if (dev < 0 || dev >= ASY_MAX) {
		kerrno = kEINVAL;
		return -1;
	}

	ap = &Asy[dev];

	if(ap->iface == NULL) {
		kerrno = kEINVAL;
		return -1;	/* Not allocated */		
	}

	i_state = disable();
	if (ap->socket_entry != NULL) {
		unix_socket_shutdown(ap->socket_entry);
		ap->socket_entry = NULL;
	}
	restore(i_state);

	ifp = ap->iface;
	ap->iface = NULL;

	killproc(&ap->txproc);

	return 0;
}

int
asy_tx_dma_busy(int dev)
{
	struct asy *ap;
	if (dev < 0 || dev >= ASY_MAX) {
		kerrno = kEINVAL;
		return -1;
	}

	ap = &Asy[dev];
	if (ap->socket_entry == NULL) {
		return 0;
	}

	return unix_socket_tx_dma_busy(ap->socket_entry);
}

int
asy_set_trigchar(int dev, int trigchar)
{
	struct asy *ap;
	if (dev < 0 || dev >= ASY_MAX) {
		kerrno = kEINVAL;
		return -1;
	}

	ap = &Asy[dev];
	if (ap->socket_entry == NULL) {
		return 0;
	}

	unix_socket_set_trigchar(ap->socket_entry, trigchar);
	return (0);
}

int
asy_get_trigchar(int dev)
{
	struct asy *ap;
	if (dev < 0 || dev >= ASY_MAX) {
		kerrno = kEINVAL;
		return -1;
	}

	ap = &Asy[dev];
	if (ap->socket_entry == NULL) {
		return 0;
	}

	return unix_socket_get_trigchar(ap->socket_entry);
}
