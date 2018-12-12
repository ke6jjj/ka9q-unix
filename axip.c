/* Driver for AX.25 exchanges over UDP (and later, IP).
 * Copyright 2018 Jeremy Cooper.
 */
#include "top.h"

#include "stdio.h"
#include "global.h"
#include "proc.h"
#include "mbuf.h"
#include "netuser.h"
#include "iface.h"
#include "trace.h"
#include "config.h"
#include "ax25.h"
#include "axip.h"
#include "cmdparse.h"
#include "socket.h"
#include "errno.h"

#define DEST_HASH_SIZE 23

typedef struct axudp_map_entry axudp_map_entry;
typedef struct axudp_dev axudp_dev;

enum AxUDPIfFlags {
	AXUDP_AUTOMAP = 1,
	AXUDP_AUTOMAP_BROADCAST = 2
};

enum {
	CALCULATE = DEST_HASH_SIZE + 1
};

struct axudp_dev {
	/* The system interface provided by this device */
	struct iface    *iface;
	/* The DNS lookup and dynamic cleaner process */
	struct proc     *clean;
	int              clean_exit;
	/* The UDP socket used for the exchange */
	int              s;
	/* Flags for operation. (AUTOMAP being one) */
	int              flags;
	/* Cleaning task time between rescans */
	int              clean_time;

	/* Statistics */
	/* If stopped due to error, the error */
	int              error;
	uint32           recv_packets;
	uint32           recv_bad_crc;
	uint32           recv_bad_ax25_hdr;
	uint32           recv_out_of_mem;
	uint32           send_packets;
	uint32           send_out_of_mem;
	uint32           send_no_dest;
	uint32           send_bad_ax25_hdr;

	/* Destination IP/PORT pairs, hash by remote call+ssid */
	axudp_map_entry *desthash[DEST_HASH_SIZE];
};

enum MapEntryFlags {
	ENTRY_VALID =      1, /* Destination socket address is valid */
	ENTRY_STATIC =     2, /* Statically assigned; don't change or delete */
        ENTRY_DNS_LOCKED = 4, /* Currently undergoing DNS lookup */
        ENTRY_DELETE = 8,     /* Locked by DNS, but needs to be deleted. */
	ENTRY_BROADCAST = 16,  /* Send AX.25 broadcasts here */
};

/*
 * A station to UDP tunnel mapping.
 *
 * Since the AX UDP interface can talk to more than one remote station, this
 * structure maps a callsign to an Internet address.  Although a callsign can
 * only be mapped to a single address, an address can support more than one
 * callsign. Associations can be statically assigned or can be learned
 * dynamically.
 *
 * Mappings are stored in a map, keyed by callsign.
 */
struct axudp_map_entry {
	/* The destination callsign */
	uint8  destcall[AXALEN];
	/* The IP address at which to reach it */
	struct ksockaddr_in destaddr;
	/* If needed, the hostname at which to reach it, queried on schedule */
	char  *hostname;
	/* Flags for keeping track of status */
	int flags;
	/* time last heard */
	int32 time;
	/* Usage count */
	uint32 sndcnt;
	uint32 rcvcnt;

	/* Destination call hash linkage */
	axudp_map_entry  *next, *prev;
};

static struct axudp_dev Axudp_dev[AXUDP_MAX];

static int axudp_raw(struct iface *iface, struct mbuf **bpp);
static int axudp_stop(struct iface *iface);
static void axudp_rx(int dev,void *p1,void *p2);
static void axudp_clean(int dev,void *p1,void *p2);
static int axudp_broadcast(axudp_dev *dev, struct mbuf **bpp);

static int attempt_automap(axudp_dev *dev, const uint8 *dstcall,
	const struct ksockaddr *from, uint bucket);
static int resolve_entry(axudp_map_entry *e);

static int add_dest(axudp_dev *dev, const uint8 *call,
	const struct ksockaddr_in *daddr, const char *dhost, int flags,
	int32 time, uint knownbucket);
static axudp_map_entry *find_dest(axudp_dev *dev, const uint8 *call,
	uint *r_bucket);

static uint hash_call(const uint8 *);
static axudp_map_entry *delete_entry(axudp_dev *dev, axudp_map_entry *e,
	uint bucket, int check);
static axudp_map_entry *free_entry(axudp_map_entry *e);

static int is_broadcast(const uint8 *call);
static int parse_keyword(const char *str, int *rset, int *rclear);

static int axudp_cmd_add(int argc, char *argv[], void *p);
static int axudp_cmd_delete(int argc, char *argv[], void *p);
static int axudp_cmd_show(int argc, char *argv[], void *p);
static int axudp_cmd_set(int argc, char *argv[], void *p);

static struct cmds Axip_cmds[] = {
	{ "add",    axudp_cmd_add,    0, 4,
		"add <callsign> <dest-host> <port> [[d]dns] [broadcast]\n"
	        "dns - Host is DNS name, resolve it now.\n"
	        "ddns - Host is a dynamic DNS name and should be"
	        "resolved on schedule.\n"
	        "broadcast - Send broadcasts."
	},
	{ "delete", axudp_cmd_delete, 0, 2, "delete <callsign>" },
	{ "show",   axudp_cmd_show,   0, 0, NULL },
	{ "set",    axudp_cmd_set,    0, 2,
		"set <option> [<option> ...]\n"
		"where option is one of:\n"
		"  [no]automap - [Disable] automatic endpoint mapping.\n"
		"  [no]autobroadcast - [Disable] broadcast to automapped"
		"endpoints."
	},
	{ NULL, NULL, 0, 0, NULL }
};

/* Configure an AX UDP interface.
 * argv[0]: hardware type, must be "axudp"
 * argv[1]: listening IP address, e.g., "44.0.0.2" or "0.0.0.0"
 * argv[2]: listening UDP port, e.g., "10093"
 * argv[3]: interface label, e.g., "axudp0"
 * argv[4..]: flag keywords:
 *         "automap" -> Automatically add senders to destination map.
 *                      (Will not modify static entries).
 *         "autobroadcast" -> When automapping a new source, also set it
 *                            as a broadcast receiver.
 */
int
axudp_attach(int argc, char *argv[], void *p)
{
	struct iface *if_axudp;
	axudp_dev *dev;
	struct ksockaddr_in addr;
	size_t i;
	int port, flags;
	int32 ip_addr;
	void *dummy;
	char *cp;

	for (i=4,flags=0;i<argc;i++){
		if (parse_keyword(argv[i], &flags, NULL) == -1) {
			kprintf("Unknown keyword '%s'.\n", argv[i]);
			goto BadKeyword;
		}
	}
	for(i=0;i<AXUDP_MAX;i++){
		if(Axudp_dev[i].iface == NULL)
			break;
	}
	if(i >= AXUDP_MAX){
		kprintf("Too many axudp interfaces. Increase AXUDP_MAX.\n");
		goto TooMany;
	}
	dev = &Axudp_dev[i];
	memset(dev, 0, sizeof(*dev));
	if(if_lookup(argv[3]) != NULL){
		kprintf("Interface %s already exists\n",argv[3]);
		goto AlreadyExists;
	}
	port = atoi(argv[2]);
	if (port < 0 || port > 0xffff) {
		kprintf("UDP port %d is invalid. Must be 0-65535\n", port);
		goto BadPort;
	}
	if ((dev->s = ksocket(kAF_INET, kSOCK_DGRAM, 0)) == -1) {
		perror("Can't open UDP socket");
		goto OpenFailed;
	}
	addr.sin_family = kAF_INET;
	addr.sin_addr.s_addr = aton(argv[1]);
	addr.sin_port = port;
	if (kbind(dev->s, (struct ksockaddr *)&addr, sizeof(addr)) == -1) {
		perror("Can't bind");
		goto BindFailed;
	}
	if_axudp = (struct iface *)callocw(1,sizeof(struct iface));
	if (if_axudp == NULL) {
		kprintf("Can't allocate interface.\n");
		goto MallocIfaceFailed;
	}

	if_axudp->name = strdup(argv[3]); if (if_axudp->name == NULL) {
		kprintf("Ifname dup failed.\n");
		goto IfNameDupFailed;
	}
	/* Interface routines will free this on shutdown */
	if_axudp->hwaddr = NULL;
	if_axudp->mtu = 65535;
	if_axudp->dev = i;
	if_axudp->raw = axudp_raw;
	if_axudp->stop = axudp_stop;
	dev->iface = if_axudp;
	dev->flags = flags;
	dev->clean_time = 300;

	/*
	 * To transmit IP packets on this interface, they must be first
	 * wrapped in AX.25.
	 */
	setencap(if_axudp,"AX25UI");

	if_axudp->next = Ifaces;
	Ifaces = if_axudp;
	cp = if_name(if_axudp," tx");
	if_axudp->txproc = newproc(cp,768,if_tx,if_axudp->dev,if_axudp,NULL,0);
	free(cp);
	cp = if_name(if_axudp," rx");
	if_axudp->rxproc = newproc(cp,768,axudp_rx,0,dev,NULL,0);
	cp = if_name(if_axudp," clean");
	dev->clean = newproc(cp,768,axudp_clean,0,dev,NULL,0);
	free(cp);

	return 0;

IfNameDupFailed:
	free(if_axudp);
MallocIfaceFailed:
BindFailed:
	kclose(dev->s);
OpenFailed:
BadPort:
TooMany:
AlreadyExists:
BadKeyword:
	return -1;
}

/* Send raw packet (caller provides header) */
static int
axudp_raw(struct iface *iface, struct mbuf **bpp)
{
	axudp_dev *dev;
	struct mbuf *copy;
	struct ax25 hdr;
	uint plen;
	uint8 *dstcall;
	int numaddrs, bcast, res;
	axudp_map_entry *e;

	iface->rawsndcnt++;
	iface->lastsent = secclock();

	dump(iface,IF_TRACE_OUT,*bpp);
	dev = &Axudp_dev[iface->dev];

	/*
	 * We have to do several things to the packet to route
	 * it properly. This means making a copy of it for now, but
	 * this is not as expensive as it seems because the mbuf routines
	 * support shallow copies.
	 */
	plen = len_p(*bpp);
	if (dup_p(&copy, *bpp, 0, plen) != plen) {
		dev->send_out_of_mem++;
		goto DupError;
	}

	/*
	 * Decode the AX.25 header so that we can find the packet's
	 * immediate destination. Note that the immediate destination is not
	 * necessarily the final destination. Digipeater hops take
	 * precedence.
	 */
	numaddrs = ntohax25(&hdr, &copy);

	/* Done with shallow copy */
	free_p(&copy);

	if (numaddrs == -1) {
		dev->send_bad_ax25_hdr++;
		goto BadHeaderDecode;
	}
	else if (numaddrs == 2 || hdr.ndigis == hdr.nextdigi)
		dstcall = hdr.dest;
	else
		dstcall = hdr.digis[hdr.nextdigi];
	
	/*
	 * Determine if the destination is a broadcast or unicast address.
	 */
	bcast = is_broadcast(dstcall);
	
	/*
	 * If the destination isn't a broadcast, we'll have to look up
	 * the UDP destination by the AX.25 destination.
	 */
	if (!bcast) {
		e = find_dest(dev, dstcall, NULL);
		if (e == NULL || !(e->flags & ENTRY_VALID)) {
			dev->send_no_dest++;
			goto NoEndpoint;
		}
	} else {
		e = NULL;
	}

	/*
	 * A CRC needs to be calculated and appended to the packet.
	 */
	if (crc_append_mbuf(bpp, 0, plen) != 0)
		goto CRCAppendFailed;

	if (e != NULL) {
		res = send_mbuf(dev->s, bpp, 0, (struct ksockaddr*)&e->destaddr,
			sizeof(e->destaddr));
		e->sndcnt++;
		dev->send_packets++;
	} else {
		res = axudp_broadcast(dev, bpp);
	}

	return res;

CRCAppendFailed:
NoEndpoint:
BadHeaderDecode:
DupError:
	free_p(&copy);
	free_p(bpp);
	return -1;
}

/* Shut down the packet interface */
static int
axudp_stop(struct iface *iface)
{
	axudp_dev *dev;
	axudp_map_entry *e;
	void *dummy;
	size_t i;

	dev = &Axudp_dev[iface->dev];
	kclose(dev->s);

	/*
	 * Shutdown the DNS lookup process cleanly; it may have
	 * allocated memory that needs to be freed.
	 */
	dev->clean_exit = 1;
	alert(dev->clean, kEABORT);
	kwait(dev->clean);

	for (i = 0; i < DEST_HASH_SIZE; i++) {
		for (e = dev->desthash[i]; e != NULL;) {
			e = free_entry(e);	
		}
	}

	return 0;
}

static void
axudp_rx(int dev_i,void *p1,void *p2)
{
	struct ax25 hdr;
	axudp_dev *dev = (axudp_dev *)p1;
	axudp_map_entry *e;
	struct mbuf *bp, *copy;
	struct ksockaddr from;
	int cnt, numaddrs, fromlen;
	uint plen, bucket;
	const uint8 *srccall;

	for (;;) {
        	cnt = recv_mbuf(dev->s, &bp, 0, &from, &fromlen);
		if (cnt < 0) {
			/* Error. Socket shut down? */
			free_p(&bp);
			dev->error = kerrno;
			break;
		}
		dev->recv_packets++;
		plen = len_p(bp);
		if (crc_check_mbuf(bp, 0, plen) != 0) {
			dev->recv_bad_crc++;
			goto BadCRC;
		}
		plen -= 2;
		/* Remove CRC bytes */
		trim_mbuf(&bp, plen);
		if (dup_p(&copy, bp, 0, plen) != plen) {
			dev->recv_out_of_mem++;
			goto BadDup;
		}
		numaddrs = ntohax25(&hdr, &copy);
		free_p(&copy);
		if (numaddrs == -1) {
			dev->recv_bad_ax25_hdr++;
			goto BadHeaderDecode;
		}
		else if (numaddrs == 2 || hdr.nextdigi == 0)
			srccall = hdr.source;
		else
			srccall = hdr.digis[hdr.nextdigi-1];
	
		/*
		 * If the source a nominal broadcast address?
		 */
		if (!is_broadcast(srccall)) {
			/*
			 * Not broadcast; potentially update source
			 * tunnel statistics.
			 */
			e = find_dest(dev, srccall, &bucket);
			if (e == NULL) {
				if (dev->flags & AXUDP_AUTOMAP)
					attempt_automap(dev, srccall, &from,
						bucket);
			} else {
				e->time = secclock();
				e->rcvcnt++;
				if (!(e->flags & ENTRY_STATIC)
				    && from.sa_family == kAF_INET) {
					memcpy(&e->destaddr, &from,
						sizeof(from));
				}
			}
		}
		/* Pass the packet to the network stack */
		net_route(dev->iface,&bp);
		continue;
BadHeaderDecode:
BadDup:
BadCRC:
		free_p(&bp);
	}

}

static void
axudp_clean(int dev_i,void *p1,void *p2)
{
	axudp_dev *dev = (axudp_dev *)p1;
	axudp_map_entry *e;
	size_t i;

	for (;!dev->clean_exit;) {
		for (i = 0; i < DEST_HASH_SIZE; i++) {
			for (e = dev->desthash[i]; e != NULL;){
				if (e->hostname != NULL) {
					switch (resolve_entry(e)) {
					case -1:
						goto RestartLoop;
					case -2:
						e = delete_entry(dev, e, i, 0);
						break;
					default:
						e = e->next;
					}
				} else {
					e = e->next;
				}
			}
		}
		ppause(dev->clean_time * 1000L);
RestartLoop:
		continue;
	}
}

static int
resolve_entry(axudp_map_entry *e)
{
	int32 ip;

	e->flags |= ENTRY_DNS_LOCKED;
	ip = resolve(e->hostname);
	e->flags &= ~(ENTRY_DNS_LOCKED);

	if (ip == 0 && kerrno == kEABORT) {
		/* Abort the entire process */
		return -1;
	}
	if (e->flags & ENTRY_DELETE) {
		/* Someone asked for this entry to be deleted while
		 * we held the lock.
		 */
		return -2;
	}
	if (ip != 0) {
		e->destaddr.sin_family = kAF_INET;
		e->destaddr.sin_addr.s_addr = ip;
		e->flags |= ENTRY_VALID;
	}
	return 0;
}

static int
axudp_broadcast(axudp_dev *dev, struct mbuf **bpp)
{
	size_t i;
	axudp_map_entry *e;
	
	for (i = 0; i < DEST_HASH_SIZE; i++) {
		for (e = dev->desthash[i]; e != NULL; e = e->next){
			if (e->flags & (ENTRY_VALID|ENTRY_BROADCAST)) {
				incref_p(*bpp);
				send_mbuf(dev->s, bpp, 0,
					(struct ksockaddr*)&e->destaddr,
					sizeof(e->destaddr));
				e->sndcnt++;
				dev->send_packets++;
			}
		}
	}
	free_p(bpp);

	return 0;
}
	
static int
attempt_automap(axudp_dev *dev, const uint8 *dstcall,
	const struct ksockaddr *from, uint bucket)
{
	int flags;

	if (dev->flags & AXUDP_AUTOMAP_BROADCAST)
		flags = ENTRY_VALID|ENTRY_BROADCAST;
	else
		flags = ENTRY_VALID;

	if (from->sa_family != kAF_INET)
		return -1;

	return add_dest(dev, dstcall, (const struct ksockaddr_in *)from, NULL,
		flags, secclock(), bucket);
}

static int
add_dest(axudp_dev *dev, const uint8 *call, const struct ksockaddr_in *daddr,
	const char *dhost, int flags, int32 time, uint knownbucket)
{
	uint h;
	axudp_map_entry *e;

	if (knownbucket == CALCULATE)
		h = hash_call(call);
	else
		h = knownbucket;

	e = (axudp_map_entry *) malloc(sizeof(axudp_map_entry));
	if (e == NULL)
		goto MallocFailed;

	memset(e, 0, sizeof(*e));
	memcpy(e->destcall, call, AXALEN);

	if (daddr != NULL)
		memcpy(&e->destaddr, daddr, sizeof(e->destaddr));
	if (dhost != NULL) {
		e->hostname = strdup(dhost);
		if (e->hostname == NULL)
			goto DestHostDupFailed;
	}

	e->time = time;
	e->flags = flags;

	/* Link into hash */
	e->prev = NULL;
	e->next = dev->desthash[h];
	if (dev->desthash[h] != NULL)
		dev->desthash[h]->prev = e;
	dev->desthash[h] = e;

	return 0;

DestHostDupFailed:
	free(e);
MallocFailed:
	return -1;
	
}

static axudp_map_entry *
delete_entry(axudp_dev *dev, axudp_map_entry *e, uint bucket, int check)
{
	if (check && e->flags & ENTRY_DNS_LOCKED) {
		/* Entry is currently involved in a DNS lookup, defer */
		e->flags |= ENTRY_DELETE;
		return e->next;
	}
	if (e->next != NULL)
		e->next->prev = e->prev;
	if (e->prev != NULL)
		e->prev->next = e->next;
	else
		dev->desthash[bucket] = e->next;
	return free_entry(e);
}

static axudp_map_entry *
free_entry(axudp_map_entry *e)
{
	axudp_map_entry *next = e->next;
	if (e->hostname != NULL)
		free(e->hostname);
	free(e);
	return next;
}

static axudp_map_entry *
find_dest(axudp_dev *dev, const uint8 *call, uint *r_bucket)
{
	uint h;
	axudp_map_entry *e;

	h = hash_call(call);

	for (e = dev->desthash[h]; e != NULL; e = e->next)
		if ((e->flags & ENTRY_DELETE) == 0&&addreq(call, e->destcall))
			break;

	if (r_bucket != NULL)
		*r_bucket = h;

	return e;
}

static uint
hash_call(const uint8 *call)
{
	int i;
	uint h;

	for (i = 0, h = 0; i < ALEN; i++) {
		h += call[i];
		h *= 3;
	}
	h += call[ALEN] & SSID;

	return h % DEST_HASH_SIZE;
}

static int
is_broadcast(const uint8 *call)
{
	uint8 (*mpp)[AXALEN];

	for (mpp = Ax25multi; (*mpp)[0] != '\0'; mpp++) {
		if (addreq(call, *mpp))
			return 1;
	}
	return 0;
}

int
doaxudp(int argc, char *argv[], void *p)
{
	struct iface *iface;
	axudp_dev *dev;

	if ((iface = if_lookup(argv[1])) == NULL){
		kprintf("No such interface '%s'.\n", argv[1]);
		return -1;
	}
	if (iface->raw != axudp_raw) {
		kprintf("%s is not an AXUDP interface.\n", argv[1]);
		return -1;
	}
	dev = &Axudp_dev[iface->dev];
	/* Forward original command name so subcmd() usage is correct */
	argv[1] = argv[0];
	argv++;
	argc--;
	return subcmd(Axip_cmds, argc, argv, dev);
}

static int
axudp_cmd_show(int argc, char *argv[], void *p)
{
	axudp_dev *dev = (axudp_dev *)p;
	char human[32];
	const char *ipaddr;
	size_t i, found;
	axudp_map_entry *e;
	struct ksockaddr_in *sin;
	int port;

	kprintf("AXUDP Interface: %s\n", dev->iface->name);
	kprintf("Receive - Packets: %-9"PRIu32" CRC Errors: %-9"PRIu32"\n",
		dev->recv_packets, dev->recv_bad_crc);
	kprintf("   Bad AX.25 hdrs: %-9"PRIu32" OutOfMem  : %-9"PRIu32"\n",
		dev->recv_bad_ax25_hdr, dev->recv_out_of_mem);
	kprintf("Sent    - Packets: %-9"PRIu32"\n", dev->send_packets);
	kprintf("   Bad AX.25 hdrs: %-9"PRIu32" OutOfMem  : %-9"PRIu32"\n",
		dev->send_bad_ax25_hdr, dev->send_out_of_mem);
	kprintf("       No mapping: %-9"PRIu32"\n", dev->send_no_dest);
	kprintf("Endpoint table:\n");
	for (i = 0, found = 0; i < DEST_HASH_SIZE; i++) {
		for (e = dev->desthash[i]; e != NULL; e = e->next) {
			if (e->flags & ENTRY_DELETE)
				continue;

			found++;

			pax25(human, e->destcall);
			sin = &e->destaddr;
			if (sin->sin_family == kAF_INET) {
				if (e->flags & ENTRY_VALID) {
					ipaddr = inet_ntoa(sin->sin_addr.s_addr);
				} else {
					ipaddr = "---Not resolved---";
				}
				port = sin->sin_port;
			} else {
				ipaddr = "?? AF unknown ??";
				port = -1;
			}
			kprintf("  %-9s %-15s %-5d", human, ipaddr, port);
			if (e->flags & ENTRY_STATIC)
				kprintf(" (static)");
			if (e->flags & ENTRY_BROADCAST)
				kprintf(" (broadcast)");
			if (e->hostname != NULL)
				kprintf(" (ddns=%s)", e->hostname);
			kprintf("\n");
		}
	}
	if (found == 0)
		kprintf("  No endpoints configured.\n");
	return 0;
}

/*
 * usage: (axudp ax0) add <callsign> <dest-host> <port> [ddns] [broadcast]
 */
static int
axudp_cmd_add(int argc, char *argv[], void *p)
{
	axudp_dev *dev = (axudp_dev *)p;
	uint8 call[AXALEN];
	char *hostname;
	int is_dns, is_ddns, is_broadcast, flags, port;
	uint h;
	size_t i;
	struct ksockaddr_in destaddr;
	axudp_map_entry *e;

	for (i=4, is_dns=is_ddns=is_broadcast=0; i < argc; i++) {
		if (STRICMP(argv[i], "ddns") == 0)
			is_ddns = 1;
		else if (STRICMP(argv[i], "dns") == 0)
			is_dns = 1;
		else if (STRICMP(argv[i], "broadcast") == 0)
			is_broadcast = 1;
		else {
			kprintf("Unknown keyword '%s'.\n", argv[i]);
			return -1;
		}
	}
	if (setcall(call, argv[1]) != 0) {
		kprintf("Invalid callsign.\n");
		return -2;
	}
	hostname = argv[2];
	port = atoi(argv[3]);
	if (port < 0 || port > 65535) {
		kprintf("Invalid port number.\n");
		return -3;
	}
	destaddr.sin_family = kAF_INET;
	destaddr.sin_port = port;
	flags = ENTRY_STATIC;
	if (is_dns) {
		/* Resolve hostname */
		destaddr.sin_addr.s_addr = resolve(hostname);
		if (destaddr.sin_addr.s_addr == 0) {
			kprintf("Unable to resolve hostname.\n");
			return -4;
		}
		/* Don't resolve dynamically */
		flags |= ENTRY_VALID;
		hostname = NULL;
	} else if (!is_ddns) {
		destaddr.sin_addr.s_addr = aton(hostname);
		if (destaddr.sin_addr.s_addr == 0) {
			kprintf("Invalid IP address.\n");
			return -5;
		}
		/* Don't resolve dynamically */
		flags |= ENTRY_VALID;
		hostname = NULL;
	}

	e = find_dest(dev, call, &h);
	if (e != NULL)
		delete_entry(dev, e, h, 1 /*multi-proc safe*/);
	if (add_dest(dev, call, &destaddr, hostname, flags, secclock(), h)!=0){
		kprintf("Out of memory.\n");
		return -6;
	}
	if (is_ddns)
		/* Tell DNS resolver process to restart */
		alert(dev->clean, kEABORT);

	return 0;
}

/*
 * usage: (axudp ax0) delete <callsign>
 */
static int
axudp_cmd_delete(int argc, char *argv[], void *p)
{
	axudp_dev *dev = (axudp_dev *)p;
	uint8 call[AXALEN];
	axudp_map_entry *e;
	uint h;

	if (setcall(call, argv[1]) != 0) {
		kprintf("Invalid callsign.\n");
		return -1;
	}
	e = find_dest(dev, call, &h);
	if (e == NULL)
		return -2;
	delete_entry(dev, e, h, 1);
	return 0;
}

static int
axudp_cmd_set(int argc, char *argv[], void *p)
{
	axudp_dev *dev = (axudp_dev *)p;
	int set, clear, i;

	for (i = 1, set = clear = 0; i < argc; i++) {
		if (parse_keyword(argv[i], &set, &clear) != 0) {
			kprintf("Unknown keyword '%s'\n", argv[i]);
			return -1;
		}
	}

	dev->flags |= set;
	dev->flags &= ~(clear);

	return 0;
}
	
static int
parse_keyword(const char *str, int *rset, int *rclear)
{
	int val, not;

	not = 0;
	if (STRNICMP(str, "no", 2) == 0) {
		str += 2;
		not = 1;
	}
		
	if (STRICMP(str, "automap") == 0)
		val = AXUDP_AUTOMAP;
	else if (STRICMP(str, "autobroadcast") == 0)
		val = AXUDP_AUTOMAP_BROADCAST;
	else
		return -1;

	if (not && rclear != NULL)
		*rclear |= val;
	if (!not && rset != NULL)
		*rset |= val;

	return 0;
}
