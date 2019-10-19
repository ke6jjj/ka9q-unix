/* Driver for BSD user-mode "TUN" packet tunnel devices.
 * Copyright 2018 Jeremy Cooper.
 */
#include "top.h"

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <net/if_tun.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#include "lib/std/stdio.h"
#include "global.h"
#include "core/proc.h"
#include "net/core/mbuf.h"
#include "net/core/iface.h"
#include "core/trace.h"
#include "config.h"

#include "lib/inet/netuser.h"
#include "unix/nosunix.h"
#include "net/tun/tundrvr.h"

/* Maximum number of fragments to tolerate in an outgoing packet */
#define MAX_FRAGS	10

struct tundrvr {
	int fd;			/* Opened TUN device descriptor */
	struct iface *iface;

	struct iovec write_vec[MAX_FRAGS];
	uint32 overflows;

	/* These members are to be protected by the interrupt lock */
	uint8          *read_buf;
	size_t          read_buf_sz;
	size_t          read_buf_cnt;
	int             read_buf_busy;
	pthread_cond_t  read_buf_avl; /* Read thread done with buffer */
	pthread_t       read_thread;
};

static struct tundrvr Tundrvr[TUN_MAX];

static int tun_raw(struct iface *iface, struct mbuf **bpp);
static void tun_rx(int dev,void *p1,void *p2);
static int tun_stop(struct iface *iface);
static void *tun_io_read_proc(void *);

/* Attach a tun driver to the system
 * argv[0]: hardware type, must be "tun"
 * argv[1]: device path name, e.g., "/dev/tun0"
 * argv[2]: interface label, e.g., "tun0"
 * argv[3]: maximum transmission unit, bytes, e.g., "1500"
 */
int
tun_attach(int argc, char *argv[], void *p)
{
	struct iface *if_tun;
	struct tundrvr *tun;
	size_t i;
	struct tuninfo tinfo;
	int mtu;
	void *dummy;
	char *cp;

	for(i=0;i<TUN_MAX;i++){
		if(Tundrvr[i].iface == NULL)
			break;
	}
	if(i >= TUN_MAX){
		kprintf("Too many tun drivers\n");
		goto TooMany;
	}
	tun = &Tundrvr[i];
	if(if_lookup(argv[2]) != NULL){
		kprintf("Interface %s already exists\n",argv[2]);
		goto AlreadyExists;
	}
	mtu = atoi(argv[3]);
	if (mtu < 0 || mtu > TUNMRU) {
		kprintf("MTU %d is invalid for tun devices.\n", mtu);
		goto BadMTU;
	}
	if ((tun->fd = open(argv[1], O_RDWR, 0)) == -1) {
		kprintf("Can't open tun device: %s\n", strerror(errno));
		goto OpenFailed;
	}
	if (ioctl(tun->fd, TUNGIFINFO, &tinfo) == -1) {
		kprintf("Can't get info: %s\n", strerror(errno));
		goto GetTunInfoFailed;
	}
	tinfo.baudrate = 10000000; /* Pretend to be 10base */
	tinfo.mtu = mtu;
	if (ioctl(tun->fd, TUNSIFINFO, &tinfo) == -1) {
		kprintf("Can't set info: %s\n", strerror(errno));
		goto SetTunInfoFailed;
	}
	if ((tun->read_buf = (uint8 *) malloc(mtu)) == NULL) {
		kprintf("Out of memory for read buffer.\n");
		goto ReadBufAllocFailed;
	}
	tun->read_buf_sz = mtu;
	tun->read_buf_busy = 0;
	if (pthread_cond_init(&tun->read_buf_avl, NULL) != 0) {
		kprintf("Can't init read cond: %s\n", strerror(errno));
		goto CantInitReadCond;
	}
	if (pthread_create(&tun->read_thread,NULL,tun_io_read_proc,tun) != 0) {
		kprintf("Can't start read thread: %s\n", strerror(errno));
		goto CantStartReadThread;
	}

	if_tun = (struct iface *)callocw(1,sizeof(struct iface));
	if (if_tun == NULL) {
		kprintf("Can't allocate interface.\n");
		goto MallocIfaceFailed;
	}

	if_tun->name = strdup(argv[2]);
	/* Interface routines will free this on shutdown */
	if_tun->hwaddr = NULL;
	if_tun->mtu = mtu;
	if_tun->dev = i;
	if_tun->raw = tun_raw;
	if_tun->stop = tun_stop;
	tun->iface = if_tun;

	setencap(if_tun,"None");

	if_tun->next = Ifaces;
	Ifaces = if_tun;
	cp = if_name(if_tun," tx");
	if_tun->txproc = newproc(cp,768,if_tx,if_tun->dev,if_tun,NULL,0);
	free(cp);
	cp = if_name(if_tun," rx");
	if_tun->rxproc = newproc(cp,768,tun_rx,if_tun->dev,if_tun,tun,0);
	free(cp);

	return 0;

MallocIfaceFailed:
	pthread_cancel(tun->read_thread);
	pthread_join(tun->read_thread, &dummy);
CantStartReadThread:
	pthread_cond_destroy(&tun->read_buf_avl);
CantInitReadCond:
	free(tun->read_buf);
ReadBufAllocFailed:
SetTunInfoFailed:
GetTunInfoFailed:
	close(tun->fd);
OpenFailed:
BadMTU:
TooMany:
AlreadyExists:
	return -1;
}

/* Send raw packet (caller provides header) */
static int
tun_raw(struct iface *iface, struct mbuf **bpp)
{
	struct tundrvr *tun;
	size_t i;
	ssize_t res;
	struct mbuf *frag;

	iface->rawsndcnt++;
	iface->lastsent = secclock();

	dump(iface,IF_TRACE_OUT,*bpp);
	tun = &Tundrvr[iface->dev];

	/*
	 * Efficiently transmit the packet fragments to the TUN interface
	 * by using the UNIX writev() interface. First, assemble the
	 * gather vector.
	 */
	for (frag = *bpp, i = 0; frag != NULL && i < MAX_FRAGS; i++) {
		tun->write_vec[i].iov_base = frag->data;
		tun->write_vec[i].iov_len = frag->cnt;
		frag = frag->next;
	}

	if (frag != NULL) {
		/* Too many fragments */
		tun->overflows++;
		free_p(bpp);
		return -1;
	}

	/* Transmit the packet via the gather vector */
	res = writev(tun->fd, &tun->write_vec[0], i);

	free_p(bpp);
	
	return res > 0 ? 0 : -1;
}

static void *
tun_io_read_proc(void *tunp)
{
	struct tundrvr *tun = (struct tundrvr *) tunp;
	ssize_t res;

	interrupt_enter();
	for (;;) {
		/*
		 * Wait for read buffer to become unbusy.
	 	 * We use this scheme so that if NOS gets busy, we simply
		 * let packets queue up in the host kernel, where there is
		 * more space for them. The alternative is to read them
		 * here and possibly drop them if there's no room in NOS
		 * to accomodate.
		 */
		while (tun->read_buf_busy)
			interrupt_cond_wait(&tun->read_buf_avl);
		interrupt_leave();

		res = read(tun->fd, tun->read_buf, tun->read_buf_sz);
		if (res == -1)
			break;

		/*
		 * It would be great to check if the packet has been truncated
		 * here. But the tun interface doesn't easily support a method
		 * to check what the real size would have been. Hopefully,
		 * though, the MTU on the interface will be obeyed by the
		 * kernel and no packets larger than the MTU will ever be
		 * passed to us.
		 */
		interrupt_enter();
		tun->read_buf_cnt = res;
		tun->read_buf_busy = 1;
		ksignal(&tun->read_buf, 1);
	}

	return NULL;
}

/* Shut down the packet interface */
static int
tun_stop(struct iface *iface)
{
	struct tundrvr *tun;
	void *dummy;

	tun = &Tundrvr[iface->dev];
	tun->iface = NULL;
	close(tun->fd);
	pthread_cancel(tun->read_thread);
	pthread_join(tun->read_thread, &dummy);
	pthread_cond_destroy(&tun->read_buf_avl);
	free(tun->read_buf);
	return 0;
}

static void
tun_rx(int dev,void *p1,void *p2)
{
	struct iface *iface = (struct iface *)p1;
	struct tundrvr *tun = (struct tundrvr *)p2;
	struct mbuf *bp;
	int i_state;

	for (;;) {
		while (tun->read_buf_busy == 0)
			if (kwait(&tun->read_buf) != 0)
				return;

		/*
		 * Allocate a buffer for the packet, anticipating that
		 * the network hopper will want to insert an interface
		 * descriptor on the packet as well.
		 */
		bp = ambufw(tun->read_buf_cnt+sizeof(struct iface *));
		bp->data += sizeof(struct iface *);

		/* Copy the data into the packet buffer */
		memcpy(bp->data, tun->read_buf, tun->read_buf_cnt);
		bp->cnt = tun->read_buf_cnt;

		/* Return the buffer to the read thread */
		i_state = disable();
		tun->read_buf_busy = 0;
		pthread_cond_signal(&tun->read_buf_avl);
		restore(i_state);

		/* Pass the packet to the network stack */
		net_route(iface,&bp);
	}
}
