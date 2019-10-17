/* Generic serial line interface routines
 * Copyright 1992 Phil Karn, KA9Q
 */
#include "top.h"

#include "lib/std/stdio.h"
#include "global.h"
#include "proc.h"
#include "iface.h"
#include "net/slhc/slhc.h"
#ifdef UNIX
#include "asy_unix.h"
#else
#include "msdos/n8250.h"
#endif
#include "asy.h"
#include "commands.h"

static int asy_detach(struct iface *ifp);

#ifdef UNIX
/* Attach a serial interface to the system
 * argv[0]: hardware type, must be "asy"
 * argv[1]: device pathname OR host:port specification
 * argv[2]: mode, may be:
 *		"slip" (point-to-point SLIP)
 *		"kissui" (AX.25 UI frame format in SLIP for raw TNC)
 *		"ax25ui" (same as kissui)
 *		"kissi" (AX.25 I frame format in SLIP for raw TNC)
 *		"ax25i" (same as kissi)
 *		"nrs" (NET/ROM format serial protocol)
 *		"ppp" (Point-to-Point Protocol, RFC1171, RFC1172)
 * argv[3]: interface label, e.g., "sl0"
 * argv[4]: receiver ring buffer size in bytes
 * argv[5]: maximum transmission unit, bytes
 * argv[6]: interface speed, e.g, "9600"
 * argv[7]: optional flags,
 *		'v' for Van Jacobson TCP header compression (SLIP only,
 *		    use ppp command for VJ compression with PPP);
 *		'c' for cts flow control
 */
#else
/* Attach a serial interface to the system
 * argv[0]: hardware type, must be "asy"
 * argv[1]: I/O address, e.g., "0x3f8"
 * argv[2]: vector, e.g., "4", or "fp1" for port 1 on a 4port card
 * argv[3]: mode, may be:
 *		"slip" (point-to-point SLIP)
 *		"kissui" (AX.25 UI frame format in SLIP for raw TNC)
 *		"ax25ui" (same as kissui)
 *		"kissi" (AX.25 I frame format in SLIP for raw TNC)
 *		"ax25i" (same as kissi)
 *		"nrs" (NET/ROM format serial protocol)
 *		"ppp" (Point-to-Point Protocol, RFC1171, RFC1172)
 * argv[4]: interface label, e.g., "sl0"
 * argv[5]: receiver ring buffer size in bytes
 * argv[6]: maximum transmission unit, bytes
 * argv[7]: interface speed, e.g, "9600"
 * argv[8]: optional flags,
 *		'v' for Van Jacobson TCP header compression (SLIP only,
 *		    use ppp command for VJ compression with PPP);
 *		'c' for cts flow control
 *		'r' for rlsd (cd) detection
 */
#endif
int
asy_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp;
	int dev;
	int trigchar = -1;
	int cts;
#ifndef UNIX
	int rlsd;
	int base;
	int irq;
	int chain;
#endif
	struct asymode *ap;
	char *cp;
	struct asy *asyp;
#ifndef UNIX
	char *basestr, *irqstr;
#else
	char *devpath;
#endif
	char *encap, *ifname, *rbufsz, *mtusz, *ifspeed, *optflags;

#ifdef UNIX
	devpath = argv[1];
	encap = argv[2];
	ifname = argv[3];
	rbufsz = argv[4];
	mtusz = argv[5];
	ifspeed = argv[6];
	optflags = argv[7];
#else
	basestr = argv[1];
	irqstr = argv[2];
	encap = argv[3];
	ifname = argv[4];
	rbufsz = argv[5];
	mtusz = argv[6];
	ifspeed = argv[7];
	optflags = argv[8];
#endif
	if(if_lookup(ifname) != NULL){
		kprintf("Interface %s already exists\n",ifname);
		return -1;
	}
	if(setencap(NULL,encap) == -1){
		kprintf("Unknown encapsulation %s\n",encap);
		return -1;
	}
	/* Find unused asy control block */
	for(dev=0;dev < ASY_MAX;dev++){
		if(Asy[dev].iface == NULL)
			break;
	}
	if(dev >= ASY_MAX){
		kprintf("Too many asynch controllers\n");
		return -1;
	}
	asyp = &Asy[dev];

#ifndef UNIX
	base = htoi(basestr);
	if(*irqstr == 's'){
		/* This is a port on a 4port card with shared interrupt */
		for(i=0;i<FPORT_MAX;i++){
			if(base >= Fport[i].base && base < Fport[i].base+32){
				n = (base - Fport[i].base) >> 3;
				Fport[i].asy[n] = asyp;
				break;
			}
		}
		if(i == FPORT_MAX){
			kprintf("%x not a known 4port address\n");
			return -1;
		}
		irq = -1;
	} else
		irq = atoi(irqstr);
#endif

	/* Create interface structure and fill in details */
	ifp = (struct iface *)callocw(1,sizeof(struct iface));
	ifp->addr = Ip_addr;
	ifp->name = strdup(ifname);
	ifp->mtu = atoi(mtusz);
	ifp->dev = dev;
	ifp->stop = asy_detach;
	setencap(ifp,encap);

	/* Look for the interface mode in the table */
	for(ap = Asymode;ap->name != NULL;ap++){
		if(STRICMP(encap,ap->name) == 0){
			trigchar = ap->trigchar;
			if((*ap->init)(ifp) != 0){
				kprintf("%s: mode %s Init failed\n",
				 ifp->name,encap);
				if_detach(ifp);
				return -1;
			}
			break;
		}
	}
	if(ap->name == NULL){
		kprintf("Mode %s unknown for interface %s\n",encap,ifname);
		if_detach(ifp);
		return -1;
	}
	/* Link in the interface */
	ifp->next = Ifaces;
	Ifaces = ifp;

	cts = 0;
#ifndef UNIX
 	rlsd = 0;
#endif

#ifdef UNIX
	if(argc > 7){
		if(strchr(optflags,'c') != NULL) {
			cts = 1;
		}
	}
	if (asy_init(dev,ifp,devpath,atol(rbufsz),trigchar,atol(ifspeed),cts)
		!= 0)
	{
		/* UNIX asy_init will write a complaint if it can't init */
		if_detach(ifp);
		return -1;
	}
#else
	if(argc > 8){
		if(strchr(optflags,'c') != NULL)
			cts = 1;
		if(strchr(optflags,'r') != NULL)
			rlsd = 1;
	}
	if(strchr(irqstr,'c') != NULL)
		chain = 1;
	else
		chain = 0;
	asy_init(dev,ifp,base,irq,atol(rbufsz),
		trigchar,atol(ifspeed),cts,rlsd,chain);
#endif
	cp = if_name(ifp," tx");
	ifp->txproc = newproc(cp,768,if_tx,0,ifp,NULL,0);
	free(cp);
	return 0;
}

static int
asy_detach(ifp)
struct iface *ifp;
{
	struct asymode *ap;

	if(ifp == NULL)
		return -1;
	asy_stop(ifp);

	/* Call mode-dependent routine */
	for(ap = Asymode;ap->name != NULL;ap++){
		if(ifp->iftype != NULL
		 && STRICMP(ifp->iftype->name,ap->name) == 0
		 && ap->free != NULL){
			(*ap->free)(ifp);
		}
	}
	return 0;
}
