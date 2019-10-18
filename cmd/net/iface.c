/* IP interface control and configuration routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "lib/std/stdio.h"
#include "global.h"
#include "net/core/mbuf.h"
#include "core/proc.h"
#include "net/core/iface.h"
#include "net/enet/enet.h"
#include "lib/util/cmdparse.h"
#include "commands.h"
#include "core/trace.h"

#include "net/inet/ip.h"
#include "net/inet/icmp.h"
#include "lib/inet/netuser.h"

static void showiface(struct iface *ifp);
static int mask2width(int32 mask);
static int ifipaddr(int argc,char *argv[],void *p);
static int iflinkadr(int argc,char *argv[],void *p);
static int ifbroad(int argc,char *argv[],void *p);
static int ifnetmsk(int argc,char *argv[],void *p);
static int ifrxbuf(int argc,char *argv[],void *p);
static int ifmtu(int argc,char *argv[],void *p);
static int ifforw(int argc,char *argv[],void *p);
static int ifencap(int argc,char *argv[],void *p);
static int iftxqlen(int argc,char *argv[],void *p);

struct cmds Ifcmds[] = {
	{ "broadcast",		ifbroad,	0,	2,	NULL },
	{ "encapsulation",	ifencap,	0,	2,	NULL },
	{ "forward",		ifforw,		0,	2,	NULL },
	{ "ipaddress",		ifipaddr,	0,	2,	NULL },
	{ "linkaddress",	iflinkadr,	0,	2,	NULL },
	{ "mtu",		ifmtu,		0,	2,	NULL },
	{ "netmask",		ifnetmsk,	0,	2,	NULL },
	{ "txqlen",		iftxqlen,	0,	2,	NULL },
	{ "rxbuf",		ifrxbuf,	0,	2,	NULL },
	{ NULL },
};

/* Given a network mask, return the number of contiguous 1-bits starting
 * from the most significant bit.
 */
static int
mask2width(int32 mask)
{
	int width,i;

	width = 0;
	for(i = 31;i >= 0;i--){
		if(!(mask & (1L << i)))
			break;
		width++;
	}
	return width;
}

/* Display the parameters for a specified interface */
static void
showiface(struct iface *ifp)
{
	char tmp[25];

	kprintf("%-10s IP addr %s MTU %u Link encap %s\n",ifp->name,
	 inet_ntoa(ifp->addr),(int)ifp->mtu,
	 ifp->iftype != NULL ? ifp->iftype->name : "not set");
	if(ifp->iftype != NULL && ifp->iftype->format != NULL && ifp->hwaddr != NULL){
		kprintf("           Link addr %s\n",
		 (*ifp->iftype->format)(tmp,ifp->hwaddr));
	}
	kprintf("           trace 0x%x netmask 0x%08lx broadcast %s\n",
	 ifp->trace,ifp->netmask,inet_ntoa(ifp->broadcast));
	if(ifp->forw != NULL)
	 kprintf("           output forward to %s\n",ifp->forw->name);
	kprintf("           sent: ip %lu tot %lu idle %s qlen %u",
	ifp->ipsndcnt,ifp->rawsndcnt,tformat(secclock() - ifp->lastsent),
	 len_q(ifp->outq));
	if(ifp->outlim != 0)
		kprintf("/%u",ifp->outlim);
	if(ifp->txbusy)
		kprintf(" BUSY");
	kprintf("\n");
	kprintf("           recv: ip %lu tot %lu idle %s\n",
	 ifp->iprecvcnt,ifp->rawrecvcnt,tformat(secclock() - ifp->lastrecv));
}

/* Set interface parameters */
int
doifconfig(int argc,char *argv[],void *p)
{
	struct iface *ifp;
	int i;

	if(argc < 2){
		for(ifp = Ifaces;ifp != NULL;ifp = ifp->next)
			showiface(ifp);
		return 0;
	}
	if((ifp = if_lookup(argv[1])) == NULL){
		kprintf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(argc == 2){
		showiface(ifp);
		if(ifp->show != NULL){
			(*ifp->show)(ifp);
		}
		return 0;
	}
	if(argc == 3){
		kprintf("Argument missing\n");
		return 1;
	}
	for(i=2;i<argc-1;i+=2)
		subcmd(Ifcmds,3,&argv[i-1],ifp);

	return 0;
}

/* Set interface IP address */
static int
ifipaddr(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;

	ifp->addr = resolve(argv[1]);
	return 0;
}


/* Set link (hardware) address */
static int
iflinkadr(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;

	if(ifp->iftype == NULL || ifp->iftype->scan == NULL){
		kprintf("Can't set link address\n");
		return 1;
	}
	if(ifp->hwaddr != NULL)
		free(ifp->hwaddr);
	ifp->hwaddr = mallocw(ifp->iftype->hwalen);
	(*ifp->iftype->scan)(ifp->hwaddr,argv[1]);
	return 0;
}

/* Set interface broadcast address. This is actually done
 * by installing a private entry in the routing table.
 */
static int
ifbroad(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;
	struct route *rp;

	rp = rt_blookup(ifp->broadcast,32);
	if(rp != NULL && rp->iface == ifp)
		rt_drop(ifp->broadcast,32);
	ifp->broadcast = resolve(argv[1]);
	rt_add(ifp->broadcast,32,0L,ifp,1L,0L,1);
	return 0;
}

/* Set the network mask. This is actually done by installing
 * a routing entry.
 */
static int
ifnetmsk(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;
	struct route *rp;

	/* Remove old entry if it exists */
	rp = rt_blookup(ifp->addr & ifp->netmask,mask2width(ifp->netmask));
	if(rp != NULL)
		rt_drop(rp->target,rp->bits);

	ifp->netmask = htol(argv[1]);
	rt_add(ifp->addr,mask2width(ifp->netmask),0L,ifp,0L,0L,0);
	return 0;
}

/* Command to set interface encapsulation mode */
static int
ifencap(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;

	if(setencap(ifp,argv[1]) != 0){
		kprintf("Encapsulation mode '%s' unknown\n",argv[1]);
		return 1;
	}
	return 0;
}

/* Set interface receive buffer size */
static int
ifrxbuf(int argc,char *argv[],void *p)
{
	return 0;	/* To be written */
}

/* Set interface Maximum Transmission Unit */
static int
ifmtu(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;

	ifp->mtu = atoi(argv[1]);
	return 0;
}

/* Set interface forwarding */
static int
ifforw(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;

	ifp->forw = if_lookup(argv[1]);
	if(ifp->forw == ifp)
		ifp->forw = NULL;
	return 0;
}

/* Command to detach an interface */
int
dodetach(int argc,char *argv[],void *p)
{
	struct iface *ifp;

	if((ifp = if_lookup(argv[1])) == NULL){
		kprintf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(if_detach(ifp) == -1)
		kprintf("Can't detach loopback or encap interface\n");
	return 0;
}

static int
iftxqlen(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;

	setint(&ifp->outlim,"TX queue limit",argc,argv);
	return 0;
}

/*
 * dial <iface> <seconds> [device dependent args]	(begin autodialing)
 * dial <iface> 0	(stop autodialing) 
 * dial <iface>	(display status)
 */
int
dodialer(int argc,char *argv[],void *p)
{
	struct iface *ifp;
	int32 timeout;

	if((ifp = if_lookup(argv[1])) == NULL){
		kprintf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(argc < 3){
		if(ifp->iftype->dstat != NULL)
			(*ifp->iftype->dstat)(ifp);
		return 0;
	}
	if(ifp->iftype->dinit == NULL){
		kprintf("Dialing not supported on %s\n",argv[1]);
		return 1;
	}
	timeout = atol(argv[2]) * 1000L;
	return (*ifp->iftype->dinit)(ifp,timeout,argc-3,argv+3);
}

