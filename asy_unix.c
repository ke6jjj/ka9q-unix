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

struct asy Asy[ASY_MAX];

static int asy_open_dev(const char *path, int cts, long speed);
static int asy_open_socket(const char *spec);
static int asy_modem_bits(int fd, int setbits, int clearbits, int *readbits);
static void pasy(struct asy *asyp);
static void *asy_io_read_proc(void *asyp);
static void *asy_io_write_proc(void *asyp);
static void asy_tx(int dummy0, void *app, void *dummy1);
static int asy_write_int(struct asy *asyp, const void *buf,unsigned short cnt);

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
	int ttyfd;
	struct asy *ap;
	void *dummy;
	char *procname;

	if(dev >= ASY_MAX)
		return -1;

	ap = &Asy[dev];

	if (strchr(path, ':') == NULL) {
		if ((ttyfd = asy_open_dev(path, cts, speed)) == -1) {
			goto CantOpenDevice;
		}
		ap->is_real_tty = 1;
	} else {
		if ((ttyfd = asy_open_socket(path)) == -1) {
			goto CantOpenDevice;
		}
		ap->is_real_tty = 0;
	}

	/* Set up receiver FIFO */
	if ((ap->fifo.buf = malloc(bufsize)) == NULL) {
		kprintf("Can't allocate read ring buffer.\n");
		goto CantAllocReadBuf;
	}
	ap->fifo.bufsize = bufsize;
	ap->fifo.wp = ap->fifo.buf;
	ap->fifo.rp = ap->fifo.buf;
	ap->fifo.cnt = 0;
	ap->fifo.hiwat = 0;
	ap->fifo.overrun = 0;

	if (pthread_mutex_init(&ap->read_lock, NULL) != 0) {
		kprintf("Can't init read lock: %s\n", strerror(errno));
		goto CantInitReadLock;
	}
	if (pthread_mutex_init(&ap->write_lock, NULL) != 0) {
		kprintf("Can't init write lock: %s\n", strerror(errno));
		goto CantInitWriteLock;
	}
	if (pthread_cond_init(&ap->write_ready, NULL) != 0) {
		kprintf("Can't init write cond: %s\n", strerror(errno));
		goto CantInitWriteCond;
	}
	pthread_mutex_lock(&ap->read_lock);
	pthread_mutex_lock(&ap->write_lock);
	if (pthread_create(&ap->read_thread, NULL, asy_io_read_proc, ap) != 0){
		kprintf("Can't start read thread: %s\n", strerror(errno));
		goto CantStartReadThread;
	}
	if (pthread_create(&ap->write_thread, NULL, asy_io_write_proc, ap)!=0){
		kprintf("Can't start write thread: %s\n", strerror(errno));
		goto CantStartWriteThread;
	}

	ap->ttyfd = ttyfd;
	ap->iface = ifp;
	ap->trigchar = trigchar;
	ap->cts = cts;
	ap->speed = speed;
	ap->dma.data = NULL;
	ap->dma.cnt = 0;
	ap->dma.busy = 0;
	ap->write_exit = 0;
	ap->txq = NULL;

	/* drop read and write locks, which will start I/O */
	pthread_mutex_unlock(&ap->read_lock);
	pthread_mutex_unlock(&ap->write_lock);

	/* Spawn the transmit deque process */
	procname = if_name(ifp, " asytx");
        ap->txproc = newproc(procname, 768, asy_tx, 0, ap, NULL, 0);
	free(procname);
	if (ap->txproc == NULL) {
		kprintf("Can't start asy tx process.\n");
		goto CantStartTxProc;
	}


	return 0;

CantStartTxProc:
CantStartWriteThread:
	pthread_cancel(ap->read_thread);
	pthread_join(ap->read_thread, &dummy);
CantStartReadThread:
	pthread_cond_destroy(&ap->write_ready);
CantInitWriteCond:
	pthread_mutex_destroy(&ap->write_lock);
CantInitWriteLock:
	pthread_mutex_destroy(&ap->read_lock);
CantInitReadLock:
	free(ap->fifo.buf);
	ap->fifo.buf = NULL;
CantAllocReadBuf:
CantOpenDevice:
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
	int i_state;
	struct asy *asyp;
	struct termios tc;

	if(dev >= ASY_MAX)
		return -1;
	asyp = &Asy[dev];
	if(asyp->iface == NULL)
		return -1;

	if(bps == 0)
		return -1;

	if (asyp->is_real_tty) {
		if (tcgetattr(asyp->ttyfd, &tc) != 0)
			return -1;
		if (cfsetspeed(&tc, bps) != 0)
			return -1;
		if (tcsetattr(asyp->ttyfd, TCSAFLUSH, &tc) != 0)
			return -1;
	}

	asyp->speed = bps;

	return 0;
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
	int param, bits, setbits, clearbits;

	switch(cmd){
	case PARAM_SPEED:
		if(set)
			asy_speed(ifp->dev,val);
		return ap->speed;
	case PARAM_DTR:
		setbits = (set && val) ? TIOCM_DTR : 0;
		clearbits = (set && !val) ? 0 : TIOCM_DTR;
		if (ap->is_real_tty) {
			asy_modem_bits(ap->ttyfd,setbits,clearbits,&bits);
			return (bits & TIOCM_DTR) ? TRUE : FALSE;
		}
		return TRUE;
	case PARAM_RTS:
		setbits = (set && val) ? TIOCM_RTS : 0;
		clearbits = (set && !val) ? 0 : TIOCM_RTS;
		if (ap->is_real_tty) {
			asy_modem_bits(ap->ttyfd,setbits,clearbits,&bits);
			return (bits & TIOCM_RTS) ? TRUE : FALSE;
		}
		return TRUE;
	case PARAM_DOWN:
		if (ap->is_real_tty)
			asy_modem_bits(ap->ttyfd,0,TIOCM_RTS|TIOCM_DTR,NULL);
		return FALSE;
	case PARAM_UP:
		if (ap->is_real_tty)
			asy_modem_bits(ap->ttyfd,TIOCM_RTS|TIOCM_DTR,0,NULL);
		return TRUE;
	}
	return -1;
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

	return asy_write_int(asyp, buf, cnt);
}

static int
asy_write_int(struct asy *asyp, const void *buf, unsigned short cnt)
{
	int tmp, i_state;
	struct iface *ifp;
	struct dma *dp;

	if((ifp = asyp->iface) == NULL)
		return -1;

	dp = &asyp->dma;

	i_state = disable();
	if(dp->busy) {
		restore(i_state);
		return -1;	/* Already busy in another process */
	}

	pthread_mutex_lock(&asyp->write_lock);

	dp->data = (uint8 *)buf;
	dp->cnt = cnt;
	dp->busy = 1;

	pthread_cond_signal(&asyp->write_ready);
	pthread_mutex_unlock(&asyp->write_lock);
	restore(i_state);

	/* Wait for completion */
	for(;;){
		i_state = disable();
		tmp = dp->busy;
		restore(i_state);
		if(tmp == 0)
			break;
		kwait(&asyp->dma);
	}
	ifp->lastsent = secclock();
	return cnt;
}

/* Blocking read from asynch line
 * Returns character or -1 if aborting
 */
int
get_asy(int dev)
{
	uint8 c;
	int tmp;

	if((tmp = asy_read(dev,&c,1)) == 1)
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
	int mcr;

	printf("%s:",asyp->iface->name);
	if(asyp->trigchar != -1)
		kprintf(" [trigger 0x%02x]",asyp->trigchar);
	if(asyp->cts)
		kprintf(" [cts flow control]");

	kprintf(" %lu bps\n",asyp->speed);

	if (asyp->is_real_tty) {
		if (asy_modem_bits(asyp->ttyfd, 0, 0, &mcr) != 0)
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
	
	kprintf(" RX: chars %lu", asyp->rxchar);
	kprintf(" sw over %lu sw hi %u\n",asyp->fifo.overrun,asyp->fifo.hiwat);
	asyp->fifo.hiwat = 0;

	kprintf(" TX: chars %lu %s\n",
	 asyp->txchar, asyp->dma.busy ? " BUSY" : "");
}

/* Send a message on the specified serial line */
int
asy_send(dev,bpp)
int dev;
struct mbuf **bpp;
{
	struct asy *asyp;
	int wasempty;

	if(dev < 0 || dev >= ASY_MAX){
		free_p(bpp);
		return -1;
	}

	enqueue(&Asy[dev].txq, bpp);

	return 0;
}

int
asy_read(
int dev,
void *buf,
unsigned short cnt
)
{
	int tmp, i_state;
	struct fifo *fp;
	uint8 *obp;

	if (cnt == 0)
		return 0;

	if (dev < 0 || dev >= ASY_MAX) {
		kerrno = kEINVAL;
		return -1;
	}

	fp = &Asy[dev].fifo;

	for (;;) {
		i_state = disable();
		tmp = fp->cnt;
		if (tmp != 0)
			break;
		restore(i_state);
		if ((kerrno = kwait(fp)) != 0)
			return -1;
	}

	obp = (uint8 *)buf;

	if (cnt > tmp)
		cnt = tmp;	/* Limit to data on hand */
	fp->cnt -= cnt;
	for (tmp = cnt; tmp > 0; tmp--) {
		*obp++ = *fp->rp++;
		if (fp->rp >= &fp->buf[fp->bufsize])
			fp->rp = fp->buf;
	}
	restore(i_state);

	return cnt;
}

static int
asy_open_dev(const char *path, int cts, long speed)
{
	int ttyfd;
	struct termios tc;

	if ((ttyfd = open(path, O_RDWR)) == -1) {
		kprintf("Can't open '%s': %s\n",path,strerror(errno));
		goto CantOpenDevice;
	}
	if (tcgetattr(ttyfd, &tc) != 0) {
		kprintf("Can't get terminal attributes: %s\n", strerror(errno));
		goto CantFetchTc;
	}
	cfmakeraw(&tc);
	tc.c_cflag = CS8|CREAD|CLOCAL;
	if (cts)
		tc.c_cflag |= CRTSCTS;
	if (cfsetspeed(&tc, speed) != 0) {
		kprintf("Can't set baud rate: %s\n", strerror(errno));
		goto InvalidSpeed;
	}
	if (tcsetattr(ttyfd, TCSANOW, &tc) != 0) {
		kprintf("Can't set terminal attributes: %s\n", strerror(errno));
		goto CantSetTc;
	}

	return ttyfd;

CantSetTc:
InvalidSpeed:
CantFetchTc:
	close(ttyfd);
CantOpenDevice:
	return -1;
}

static int
asy_open_socket(const char *spec)
{
	char *hostname, *service;
	const char *sep;
	struct addrinfo hints, *res0, *res;
	int fd, error;
	
	sep = (const char *) strrchr(spec, ':');
	if (sep == NULL) {
		kprintf("Host specification missing port\n");
		goto BadChar;
	}

	service = strdup(sep+1);
	if (service == NULL) {
		kprintf("Out of memory.\n");
		goto BadServiceAlloc;
	}

	hostname = (char *) malloc(sep - spec + 1);
	if (hostname == NULL) {
		kprintf("Out of memory.\n");
		goto BadHostnameAlloc;
	}

	memcpy(hostname, spec, sep - spec);
	hostname[sep - spec] = '\0';

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags =
		AI_ADDRCONFIG | /* Use IPv4/IPv6 depending on availability */
		AI_NUMERICSERV; /* Port should be a number */
	hints.ai_canonname = NULL;
	hints.ai_next = NULL;

	error = getaddrinfo(hostname, service, &hints, &res0);
	if (error != 0) {
		kprintf("getaddrinfo() failure: %s\n", gai_strerror(error));
		goto GetAddrInfoFailed;
	}

	/* Try each provided address in turn */

	for (res = res0; res != NULL; res = res->ai_next) {
		fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd == -1) {
			kprintf("socket() failed.\n");
			goto SocketFailed;
		}

		error = connect(fd, res->ai_addr, res->ai_addrlen);
		if (error >= 0)
			break;

		close(fd);
	}

	if (error < 0) {
		kprintf("connect() failed: %s\n", strerror(errno));
		goto ConnectFailed;
	}

	freeaddrinfo(res);
	free(hostname);
	free(service);

	return fd;

ConnectFailed:
	close(fd);
SocketFailed:
	freeaddrinfo(res);
GetAddrInfoFailed:
	free(hostname);
BadHostnameAlloc:
	free(service);
BadServiceAlloc:
BadChar:
	return -1;
}

static int
asy_modem_bits(int fd, int setbits, int clearbits, int *readbits)
{
	if (setbits != 0 && ioctl(fd, TIOCMBIS, &setbits) != 0)
		return -1;
	if (clearbits != 0 && ioctl(fd, TIOCMBIC, &clearbits) != 0)
		return -1;
	if (readbits != NULL && ioctl(fd, TIOCMGET, readbits) != 0)
		return -1;
	return 0;
}

static void *
asy_io_read_proc(void *asyp)
{
	struct asy *ap = asyp;
	ssize_t cnt, tmp;
	uint8 buf[1024], *ibp;
	struct fifo *fp;
	int sig;

	/* Wait until we are allowed to proceed */
	pthread_mutex_lock(&ap->read_lock);

	/* Go ahead and drop the read lock now that we've started */
	pthread_mutex_unlock(&ap->read_lock);

	fp = &ap->fifo;

	for (;;) {
		cnt = read(ap->ttyfd, buf, sizeof(buf));
		if (cnt == -1)
			break;

		interrupt_enter();

		/* Search read data for interesting characters if asked */
		sig = ap->trigchar == -1 || memchr(buf, ap->trigchar, cnt);

		/* Compute room left in FIFO */
		tmp = fp->bufsize - fp->cnt;
		if (cnt > tmp) {
			/* Not enough room in FIFO */
			fp->overrun += cnt - tmp;
			cnt = tmp;
		}

		/* Update FIFO room */
		fp->cnt += cnt;

		/* Copy read data into FIFO */
		ibp = buf;
		for (tmp = cnt; tmp > 0; tmp--) {
			*(fp->wp++) = *ibp++;
			if (fp->wp >= &fp->buf[fp->bufsize])
				fp->wp = fp->buf;
		}

		/* Update statistics */
		ap->rxchar += cnt;
		if (fp->cnt > fp->hiwat)
			fp->hiwat = fp->cnt;

		/* Wakeup any sleepers if an interesting event has happened */
		if (sig)
			ksignal(fp, 1);

		interrupt_leave();
	}

	return NULL;
		
}

static void *
asy_io_write_proc(void *asyp)
{
	struct asy *ap = (struct asy *)asyp;
	int leave;
	uint8 *data;
	size_t sz;
	ssize_t cnt;

	pthread_mutex_lock(&ap->write_lock);
	pthread_mutex_unlock(&ap->write_lock);

	for (;;) {
		pthread_mutex_lock(&ap->write_lock);
		while (!ap->write_exit && !ap->dma.busy)
			pthread_cond_wait(&ap->write_ready, &ap->write_lock);
		leave = ap->write_exit;
		data = ap->dma.data;
		sz = ap->dma.cnt;
		pthread_mutex_unlock(&ap->write_lock);

		if (leave)
			break;

		cnt = write(ap->ttyfd, data, sz);
		if (cnt <= 0)
			break;

		interrupt_enter();
		ap->txchar += cnt;
		ap->dma.busy = 0;
		ksignal(&ap->dma, 1);
		interrupt_leave();
	}

	return NULL;
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
			asy_write_int(ap, bp->data, bp->cnt);
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
	void *dummy;

	if (dev < 0 || dev >= ASY_MAX) {
		kerrno = kEINVAL;
		return -1;
	}

	ap = &Asy[dev];

	if(ap->iface == NULL) {
		kerrno = kEINVAL;
		return -1;	/* Not allocated */		
	}

	ifp = ap->iface;
	ap->iface = NULL;

	i_state = disable();
	pthread_cancel(ap->read_thread);
	restore(i_state);

	pthread_mutex_lock(&ap->write_lock);
	ap->write_exit = 1;
	pthread_cond_signal(&ap->write_ready);
	pthread_mutex_unlock(&ap->write_lock);

	pthread_join(ap->read_thread, &dummy);
	pthread_join(ap->write_thread, &dummy);

	close(ap->ttyfd);

	free(ap->fifo.buf);

	killproc(&ap->txproc);

	return 0;
}
