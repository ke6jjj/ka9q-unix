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

#include "lib/std/stdio.h"
#include <errno.h>
#include "lib/std/errno.h"
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "devparam.h"

#include "unix/nosunix.h"

#include "unix_socket.h"

static int
unix_socket_open_dev(const char *path, int cts, long speed)
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
unix_socket_open_socket(const char *spec)
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

static void *
unix_socket_io_write_proc(void *param)
{
	struct unix_socket_entry *us = param;
	int leave;
	uint8 *data;
	size_t sz;
	ssize_t cnt;

	pthread_mutex_lock(&us->write_lock);
	pthread_mutex_unlock(&us->write_lock);

	for (;;) {
		pthread_mutex_lock(&us->write_lock);
		while (!us->write_exit && !us->dma.busy)
			pthread_cond_wait(&us->write_ready, &us->write_lock);
		leave = us->write_exit;
		data = us->dma.data;
		sz = us->dma.cnt;
		pthread_mutex_unlock(&us->write_lock);

		if (leave)
			break;

		cnt = write(us->ttyfd, data, sz);
		if (cnt <= 0)
			break;

		interrupt_enter();
		us->txchar += cnt;
		us->dma.busy = 0;
		ksignal(&us->dma, 1);
		interrupt_leave();
	}

	return NULL;
}


static void *
unix_socket_io_read_proc(void *param)
{
	struct unix_socket_entry *us = param;
	ssize_t cnt, tmp;
	uint8 buf[1024], *ibp;
	struct unix_socket_fifo *fp;
	int sig;

	/* Wait until we are allowed to proceed */
	pthread_mutex_lock(&us->read_lock);

	/* Go ahead and drop the read lock now that we've started */
	pthread_mutex_unlock(&us->read_lock);

	fp = &us->fifo;

	for (;;) {
		cnt = read(us->ttyfd, buf, sizeof(buf));
		if (cnt == -1)
			break;

		interrupt_enter();

		/* Search read data for interesting characters if asked */
		sig = us->trigchar == -1 || memchr(buf, us->trigchar, cnt);

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
		us->rxchar += cnt;
		if (fp->cnt > fp->hiwat)
			fp->hiwat = fp->cnt;

		/* Wakeup any sleepers if an interesting event has happened */
		if (sig)
			ksignal(fp, 1);

		interrupt_leave();
	}

	return NULL;
}


/*
 * Create a unix socket handle.
 *
 * This is a pointer structure that contains all of
 * the relevant pieces needed for doing bi-directional
 * IO to a socket.
 *
 * Note: this doesn't yet have a way of signaling to
 * the owner that a read / write failed or the socket
 * has closed.
 */
struct unix_socket_entry *
unix_socket_create(const char *path, uint bufsize, int trigchar, long speed,
    int cts)
{
	struct unix_socket_entry *us;
	int ttyfd;
	void *dummy;

	us = calloc(1, sizeof(*us));

	if (strchr(path, ':') == NULL) {
		if ((ttyfd = unix_socket_open_dev(path, cts, speed)) == -1) {
			goto CantOpenDevice;
		}
		us->is_real_tty = 1;
	} else {
		if ((ttyfd = unix_socket_open_socket(path)) == -1) {
			goto CantOpenDevice;
		}
		us->is_real_tty = 0;
	}

	/* Setup receiver FIFO */
	if ((us->fifo.buf = malloc(bufsize)) == NULL) {
		kprintf("Can't allocate read ring buffer.\n");
		goto CantAllocReadBuf;
	}
	us->fifo.bufsize = bufsize;
	us->fifo.wp = us->fifo.buf;
	us->fifo.rp = us->fifo.buf;
	us->fifo.cnt = 0;
	us->fifo.hiwat = 0;
	us->fifo.overrun = 0;

	/* Setup pthread mutex/cond */
	if (pthread_mutex_init(&us->read_lock, NULL) != 0) {
		kprintf("Can't init read lock: %s\n", strerror(errno));
		goto CantInitReadLock;
	}
	if (pthread_mutex_init(&us->write_lock, NULL) != 0) {
		kprintf("Can't init write lock: %s\n", strerror(errno));
		goto CantInitWriteLock;
	}
	if (pthread_cond_init(&us->write_ready, NULL) != 0) {
		kprintf("Can't init write cond: %s\n", strerror(errno));
		goto CantInitWriteCond;
	}

	/* Grab locks - this will make sure threads don't start too early */
	pthread_mutex_lock(&us->read_lock);
	pthread_mutex_lock(&us->write_lock);

	/* Create pthreads */
	if (pthread_create(&us->read_thread, NULL, unix_socket_io_read_proc, us) != 0){
		kprintf("Can't start read thread: %s\n", strerror(errno));
		goto CantStartReadThread;
	}
	if (pthread_create(&us->write_thread, NULL, unix_socket_io_write_proc, us) != 0){
		kprintf("Can't start write thread: %s\n", strerror(errno));
		goto CantStartWriteThread;
	}

	/* Initialise local state */
	us->ttyfd = ttyfd;
	us->trigchar = trigchar;
	us->cts = cts;
	us->speed = speed;
	us->dma.data = NULL;
	us->dma.cnt = 0;
	us->dma.busy = 0;
	us->write_exit = 0;

	/* Unlock - this starts read/write IO */
	pthread_mutex_unlock(&us->read_lock);
	pthread_mutex_unlock(&us->write_lock);

	/* We're good to go! */
	return us;
CantStartWriteThread:
	pthread_cancel(us->read_thread);
	pthread_join(us->read_thread, &dummy);
CantStartReadThread:
	pthread_cond_destroy(&us->write_ready);
CantInitWriteCond:
	pthread_mutex_destroy(&us->write_lock);
CantInitWriteLock:
	pthread_mutex_destroy(&us->read_lock);
CantInitReadLock:
	free(us->fifo.buf);
	us->fifo.buf = NULL;
CantAllocReadBuf:
CantOpenDevice:
	free(us);
	return NULL;
}

/*
 * Shut down the given socket.  This will signal any
 * blockers that the socket is going away, close things
 * and free up state.
 */
int
unix_socket_shutdown(struct unix_socket_entry *us)
{
	int i_state;
	void *dummy;

	i_state = disable();
	pthread_cancel(us->read_thread);
	restore(i_state);

	pthread_mutex_lock(&us->write_lock);
	us->write_exit = 1;
	pthread_cond_signal(&us->write_ready);
	pthread_mutex_unlock(&us->write_lock);

	pthread_join(us->read_thread, &dummy);
	pthread_join(us->write_thread, &dummy);

	close(us->ttyfd);

	free(us->fifo.buf);
	return 0;
}

/*
 * Blocking read.
 */
int
unix_socket_read(struct unix_socket_entry *us, void *buf, unsigned short cnt)
{
	int tmp, i_state;
	struct unix_socket_fifo *fp;
	uint8 *obp;

	if (cnt == 0)
		return 0;

	fp = &us->fifo;

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
		cnt = tmp;      /* Limit to data on hand */
	fp->cnt -= cnt;
	for (tmp = cnt; tmp > 0; tmp--) {
		*obp++ = *fp->rp++;
		if (fp->rp >= &fp->buf[fp->bufsize])
			fp->rp = fp->buf;
	}
	restore(i_state);

	return cnt;
}

/*
 * Blocking write.
 */
int
unix_socket_write(struct unix_socket_entry *us, const void *buf, unsigned short cnt)
{
	int tmp, i_state;
	struct unix_socket_dma *dp;

	dp = &us->dma;

	i_state = disable();
	if(dp->busy) {
		restore(i_state);
		return -1;      /* Already busy in another process */
	}

	pthread_mutex_lock(&us->write_lock);

	dp->data = (uint8 *)buf;
	dp->cnt = cnt;
	dp->busy = 1;

	pthread_cond_signal(&us->write_ready);
	pthread_mutex_unlock(&us->write_lock);
	restore(i_state);

	/* Wait for completion */
	for(;;){
		i_state = disable();
		tmp = dp->busy;
		restore(i_state);
		if(tmp == 0)
			break;
		kwait(&us->dma);
	}
	return cnt;
}

int
unix_socket_tx_dma_busy(struct unix_socket_entry *us)
{
	return (us->dma.busy != 0);
}

int
unix_socket_get_trigchar(struct unix_socket_entry *us)
{
	return (us->trigchar);
}

int
unix_socket_set_trigchar(struct unix_socket_entry *us, int trigchar)
{
	us->trigchar = trigchar;
	return 0;
}

/* Set asynch line speed */
int
unix_socket_set_line_speed(struct unix_socket_entry *us, long bps)
{
	struct termios tc;

	if(bps == 0)
		return -1;

	if (us->is_real_tty) {
		if (tcgetattr(us->ttyfd, &tc) != 0)
			return -1;
		if (cfsetspeed(&tc, bps) != 0)
			return -1;
		if (tcsetattr(us->ttyfd, TCSAFLUSH, &tc) != 0)
			return -1;
	}

	us->speed = bps;

	return 0;
}

/* Asynchronous line I/O control */
int32
unix_socket_ioctl(struct unix_socket_entry *us, int cmd, int set, int32 val)
{
	int bits, setbits, clearbits;

	switch(cmd){
	case PARAM_SPEED:
		if(set)
			unix_socket_set_line_speed(us, val);
		return us->speed;
	case PARAM_DTR:
		setbits = (set && val) ? TIOCM_DTR : 0;
		clearbits = (set && !val) ? 0 : TIOCM_DTR;
		if (us->is_real_tty) {
			unix_socket_modem_bits(us, setbits, clearbits, &bits);
			return (bits & TIOCM_DTR) ? TRUE : FALSE;
		}
		return TRUE;
	case PARAM_RTS:
		setbits = (set && val) ? TIOCM_RTS : 0;
		clearbits = (set && !val) ? 0 : TIOCM_RTS;
		if (us->is_real_tty) {
			unix_socket_modem_bits(us, setbits,clearbits,&bits);
			return (bits & TIOCM_RTS) ? TRUE : FALSE;
		}
		return TRUE;
	case PARAM_DOWN:
		if (us->is_real_tty)
			unix_socket_modem_bits(us, 0,TIOCM_RTS|TIOCM_DTR,NULL);
		return FALSE;
	case PARAM_UP:
		if (us->is_real_tty)
			unix_socket_modem_bits(us,TIOCM_RTS|TIOCM_DTR,0,NULL);
		return TRUE;
	}
	return -1;
}

/*
 * Fetch and update the modem ioctl configuration.
 *
 * TODO: change this to use KA9Q versions of the IOCTL flags and
 * translate them here.  That way this public API will hide the
 * UNIX ioctl flags.
 */
int
unix_socket_modem_bits(struct unix_socket_entry *us, int setbits,
    int clearbits, int *readbits)
{
	if (us == NULL) {
		return -1;
	}
	if (setbits != 0 && ioctl(us->ttyfd, TIOCMBIS, &setbits) != 0)
		return -1;
	if (clearbits != 0 && ioctl(us->ttyfd, TIOCMBIC, &clearbits) != 0)
		return -1;
	if (readbits != NULL && ioctl(us->ttyfd, TIOCMGET, readbits) != 0)
		return -1;
	return 0;
}

int
unix_socket_is_real_tty(struct unix_socket_entry *us)
{
	if (us == NULL) {
		return 0;
	}
	return us->is_real_tty;
}

int
unix_socket_flowcontrol_cts(struct unix_socket_entry *us)
{
	if (us == NULL) {
		return 0;
	}
	return us->cts;
}

int
unix_socket_get_stats(struct unix_socket_entry *us, struct unix_socket_stats *stats)
{
	if (us == NULL) {
		return -1;
	}
	stats->rxchar = us->rxchar;
	stats->txchar = us->txchar;
	stats->fifo_overrun = us->fifo.overrun;
	stats->fifo_hiwat = us->fifo.hiwat;
	us->fifo.hiwat = 0;
	return 0;
}

long
unix_socket_get_speed(struct unix_socket_entry *us)
{
	if (us == NULL) {
		return -1;
	}
	return us->speed;
}
