/* Driver for BSD user-mode "TAP" Ethernet devices.
 * Copyright 2018 Jeremy Cooper.
 */
#include "../../top.h"

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <net/if_tap.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#include "../../stdio.h"
#include "../../global.h"
#include "../../proc.h"
#include "../../mbuf.h"
#include "../../iface.h"
#include "../../trace.h"
#include "../../config.h"

#include "../enet/enet.h"
#include "../arp/arp.h"
#include "../../lib/inet/netuser.h"
#include "../../unix/nosunix.h"

#include "tapdrvr.h"

/* Maximum number of fragments to tolerate in an outgoing packet */
#define MAX_FRAGS	10

struct tapdrvr {
	int fd;			/* Opened TAP device descriptor */
	struct iface *iface;

	uint8 hwaddr[EADDR_LEN];

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

static struct tapdrvr Tapdrvr[TAP_MAX];

static int tap_raw(struct iface *iface, struct mbuf **bpp);
static void tap_rx(int dev,void *p1,void *p2);
static int tap_stop(struct iface *iface);
static void *tap_io_read_proc(void *);

/* Attach a tap driver to the system
 * argv[0]: hardware type, must be "tap"
 * argv[1]: device path name, e.g., "/dev/tap0"
 * argv[2]: interface label, e.g., "tap0"
 * argv[3]: ethernet address to use, e.g., "00:11:22:33:44:55:66"
 * argv[4]: maximum transmission unit, bytes, e.g., "1500"
 */
int
tap_attach(int argc, char *argv[], void *p)
{
	struct iface *if_tap;
	struct tapdrvr *tap;
	size_t i;
	int count;
	struct tapinfo tinfo;
	int mtu;
	void *dummy;
	char *cp;

	for(i=0;i<TAP_MAX;i++){
		if(Tapdrvr[i].iface == NULL)
			break;
	}
	if(i >= TAP_MAX){
		kprintf("Too many tap drivers\n");
		goto TooMany;
	}
	tap = &Tapdrvr[i];
	if(if_lookup(argv[2]) != NULL){
		kprintf("Interface %s already exists\n",argv[2]);
		goto AlreadyExists;
	}
	count = gether(tap->hwaddr, argv[3]);
	if (count != 1) {
		kprintf("Invalid local address '%s'.\n", argv[3]);
		goto BadEAddr;
	}
	if(tap->hwaddr[0] & 1)
		kprintf("Warning! '%s' is a multicast address:", argv[3]);
	mtu = atoi(argv[4]);
	if (mtu < 0 || mtu > TAPMRU) {
		kprintf("MTU %d is invalid for tap devices.\n", mtu);
		goto BadMTU;
	}
	if ((tap->fd = open(argv[1], O_RDWR, 0)) == -1) {
		kprintf("Can't open tap device: %s\n", strerror(errno));
		goto OpenFailed;
	}
	if (ioctl(tap->fd, TAPGIFINFO, &tinfo) == -1) {
		kprintf("Can't get info: %s\n", strerror(errno));
		goto GetTapInfoFailed;
	}
	tinfo.baudrate = 10000000; /* Pretend to be 10base */
	tinfo.mtu = mtu;
	if (ioctl(tap->fd, TAPSIFINFO, &tinfo) == -1) {
		kprintf("Can't set info: %s\n", strerror(errno));
		goto SetTapInfoFailed;
	}
	if ((tap->read_buf = (uint8 *) malloc(mtu)) == NULL) {
		kprintf("Out of memory for read buffer.\n");
		goto ReadBufAllocFailed;
	}
	tap->read_buf_sz = mtu;
	tap->read_buf_busy = 0;
	if (pthread_cond_init(&tap->read_buf_avl, NULL) != 0) {
		kprintf("Can't init read cond: %s\n", strerror(errno));
		goto CantInitReadCond;
	}
	if (pthread_create(&tap->read_thread,NULL,tap_io_read_proc,tap) != 0) {
		kprintf("Can't start read thread: %s\n", strerror(errno));
		goto CantStartReadThread;
	}

	if_tap = (struct iface *)callocw(1,sizeof(struct iface));
	if (if_tap == NULL) {
		kprintf("Can't allocate interface.\n");
		goto MallocIfaceFailed;
	}

	if_tap->name = strdup(argv[2]);
	/* Interface routines will free this on shutdown */
	if_tap->hwaddr = malloc(sizeof(tap->hwaddr));
	if (if_tap->hwaddr == NULL) {
		kprintf("Can't allocate if hwaddr.\n");
		goto IfHwAddrAllocFailed;
	}
	memcpy(if_tap->hwaddr, tap->hwaddr, sizeof(tap->hwaddr));
	if_tap->mtu = mtu;
	if_tap->dev = i;
	if_tap->raw = tap_raw;
	if_tap->stop = tap_stop;
	tap->iface = if_tap;

	setencap(if_tap,"Ethernet");

	if_tap->next = Ifaces;
	Ifaces = if_tap;
	cp = if_name(if_tap," tx");
	if_tap->txproc = newproc(cp,768,if_tx,if_tap->dev,if_tap,NULL,0);
	free(cp);
	cp = if_name(if_tap," rx");
	if_tap->rxproc = newproc(cp,768,tap_rx,if_tap->dev,if_tap,tap,0);
	free(cp);

	return 0;

IfHwAddrAllocFailed:
	free(if_tap);
MallocIfaceFailed:
	pthread_cancel(tap->read_thread);
	pthread_join(tap->read_thread, &dummy);
CantStartReadThread:
	pthread_cond_destroy(&tap->read_buf_avl);
CantInitReadCond:
	free(tap->read_buf);
ReadBufAllocFailed:
SetTapInfoFailed:
GetTapInfoFailed:
	close(tap->fd);
OpenFailed:
BadMTU:
TooMany:
AlreadyExists:
BadEAddr:
	return -1;
}

/* Send raw packet (caller provides header) */
static int
tap_raw(struct iface *iface, struct mbuf **bpp)
{
	struct tapdrvr *tap;
	size_t i;
	ssize_t res;
	struct mbuf *frag;

	iface->rawsndcnt++;
	iface->lastsent = secclock();

	dump(iface,IF_TRACE_OUT,*bpp);
	tap = &Tapdrvr[iface->dev];

	/*
	 * Efficiently transmit the packet fragments to the TAP interface
	 * by using the UNIX writev() interface. First, assemble the
	 * gather vector.
	 */
	for (frag = *bpp, i = 0; frag != NULL && i < MAX_FRAGS; i++) {
		tap->write_vec[i].iov_base = frag->data;
		tap->write_vec[i].iov_len = frag->cnt;
		frag = frag->next;
	}

	if (frag != NULL) {
		/* Too many fragments */
		tap->overflows++;
		free_p(bpp);
		return -1;
	}

	/* Transmit the packet via the gather vector */
	res = writev(tap->fd, &tap->write_vec[0], i);

	free_p(bpp);
	
	return res > 0 ? 0 : -1;
}

static void *
tap_io_read_proc(void *tapp)
{
	struct tapdrvr *tap = (struct tapdrvr *) tapp;
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
		while (tap->read_buf_busy)
			interrupt_cond_wait(&tap->read_buf_avl);
		interrupt_leave();

		res = read(tap->fd, tap->read_buf, tap->read_buf_sz);
		if (res == -1)
			break;

		/*
		 * It would be great to check if the packet has been truncated
		 * here. But the tap interface doesn't easily support a method
		 * to check what the real size would have been. Hopefully,
		 * though, the MTU on the interface will be obeyed by the
		 * kernel and no packets larger than the MTU will ever be
		 * passed to us.
		 */
		interrupt_enter();
		tap->read_buf_cnt = res;
		tap->read_buf_busy = 1;
		ksignal(&tap->read_buf, 1);
	}

	return NULL;
}

/* Shut down the packet interface */
static int
tap_stop(struct iface *iface)
{
	struct tapdrvr *tap;
	void *dummy;

	tap = &Tapdrvr[iface->dev];
	tap->iface = NULL;
	close(tap->fd);
	pthread_cancel(tap->read_thread);
	pthread_join(tap->read_thread, &dummy);
	pthread_cond_destroy(&tap->read_buf_avl);
	free(tap->read_buf);
	return 0;
}

static void
tap_rx(int dev,void *p1,void *p2)
{
	struct iface *iface = (struct iface *)p1;
	struct tapdrvr *tap = (struct tapdrvr *)p2;
	struct mbuf *bp;
	int i_state;

	for (;;) {
		while (tap->read_buf_busy == 0)
			if (kwait(&tap->read_buf) != 0)
				return;

		/*
		 * Allocate a buffer for the packet, anticipating that
		 * the network hopper will want to insert an interface
		 * descriptor on the packet as well.
		 */
		bp = ambufw(tap->read_buf_cnt+sizeof(struct iface *));
		bp->data += sizeof(struct iface *);

		/* Copy the data into the packet buffer */
		memcpy(bp->data, tap->read_buf, tap->read_buf_cnt);
		bp->cnt = tap->read_buf_cnt;

		/* Return the buffer to the read thread */
		i_state = disable();
		tap->read_buf_busy = 0;
		pthread_cond_signal(&tap->read_buf_avl);
		restore(i_state);

		/* Pass the packet to the network stack */
		net_route(iface,&bp);
	}
}
