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

/* Interface list header */
struct iface *Ifaces = &Loopback;

/* Loopback pseudo-interface */
struct iface Loopback = {
	&Encap,		/* Link to next entry */
	"loopback",	/* name		*/
	0x7f000001L,	/* addr		127.0.0.1 */
	0xffffffffL,	/* broadcast	255.255.255.255 */
	0xffffffffL,	/* netmask	255.255.255.255 */
	MAXINT16,	/* mtu		No limit */
	0,		/* trace	*/
	NULL,	/* trfp		*/
	NULL,		/* forw		*/
	NULL,	/* rxproc	*/
	NULL,	/* txproc	*/
	NULL,	/* supv		*/
	NULL,	/* outq		*/
	0,		/* outlim	*/
	0,		/* txbusy	*/
	NULL,		/* dstate	*/
	NULL,		/* dtickle	*/
	NULL,		/* dstatus	*/
	0,		/* dev		*/
	NULL,		/* (*ioctl)	*/
	NULL,		/* (*iostatus)	*/
	NULL,		/* (*stop)	*/
	NULL,	/* hwaddr	*/
	NULL,		/* extension	*/
	0,		/* xdev		*/
	&Iftypes[0],	/* iftype	*/
	NULL,		/* (*send)	*/
	NULL,		/* (*output)	*/
	NULL,		/* (*raw)	*/
	NULL,		/* (*status)	*/
	NULL,		/* (*discard)	*/
	NULL,		/* (*echo)	*/
	0,		/* ipsndcnt	*/
	0,		/* rawsndcnt	*/
	0,		/* iprecvcnt	*/
	0,		/* rawrcvcnt	*/
	0,		/* lastsent	*/
	0,		/* lastrecv	*/
};
/* Encapsulation pseudo-interface */
struct iface Encap = {
	NULL,
	"encap",	/* name		*/
	kINADDR_ANY,	/* addr		0.0.0.0 */
	0xffffffffL,	/* broadcast	255.255.255.255 */
	0xffffffffL,	/* netmask	255.255.255.255 */
	MAXINT16,	/* mtu		No limit */
	0,		/* trace	*/
	NULL,	/* trfp		*/
	NULL,		/* forw		*/
	NULL,	/* rxproc	*/
	NULL,	/* txproc	*/
	NULL,	/* supv		*/
	NULL,	/* outq		*/
	0,		/* outlim	*/
	0,		/* txbusy	*/
	NULL,		/* dstate	*/
	NULL,		/* dtickle	*/
	NULL,		/* dstatus	*/
	0,		/* dev		*/
	NULL,		/* (*ioctl)	*/
	NULL,		/* (*iostatus)	*/
	NULL,		/* (*stop)	*/
	NULL,	/* hwaddr	*/
	NULL,		/* extension	*/
	0,		/* xdev		*/
	&Iftypes[0],	/* iftype	*/
	ip_encap,	/* (*send)	*/
	NULL,		/* (*output)	*/
	NULL,		/* (*raw)	*/
	NULL,		/* (*status)	*/
	NULL,		/* (*discard)	*/
	NULL,		/* (*echo)	*/
	0,		/* ipsndcnt	*/
	0,		/* rawsndcnt	*/
	0,		/* iprecvcnt	*/
	0,		/* rawrcvcnt	*/
	0,		/* lastsent	*/
	0,		/* lastrecv	*/
};

char Noipaddr[] = "IP address field missing, and ip address not set\n";

/*
 * General purpose interface transmit task, one for each device that can
 * send IP datagrams. It waits on the interface's IP output queue (outq),
 * extracts IP datagrams placed there in priority order by ip_route(),
 * and sends them to the device's send routine.
 */
void
if_tx(int dev,void *arg1,void *unused)
{
	struct mbuf *bp;	/* Buffer to send */
	struct iface *iface;	/* Pointer to interface control block */
	struct qhdr qhdr;

	iface = arg1;
	for(;;){
		while(iface->outq == NULL)
			kwait(&iface->outq);

		iface->txbusy = 1;
		bp = dequeue(&iface->outq);
		pullup(&bp,&qhdr,sizeof(qhdr));
		if(iface->dtickle != NULL && (*iface->dtickle)(iface) == -1){
#ifdef	notdef	/* Confuses some non-compliant hosts */
			struct ip ip;

			/* Link redial failed; bounce with unreachable */
			ntohip(&ip,&bp);
			icmp_output(&ip,bp,ICMP_DEST_UNREACH,ICMP_HOST_UNREACH,
			 NULL);
#endif
			free_p(&bp);
		} else {
			(*iface->send)(&bp,iface,qhdr.gateway,qhdr.tos);
		}
		iface->txbusy = 0;

		/* Let other tasks run, just in case send didn't block */
		kwait(NULL);
	}
}
/* Process packets in the Hopper */
void
network(int i,void *v1,void *v2)
{
	struct mbuf *bp;
	char i_state;
	struct iftype *ift;
	struct iface *ifp;

loop:
	for(;;){
		i_state = disable();
		bp = Hopper;
		if(bp != NULL){
			bp = dequeue(&Hopper);
			restore(i_state);
			break;
		}
		restore(i_state);
		kwait(&Hopper);
	}
	/* Process the input packet */
	pullup(&bp,&ifp,sizeof(ifp));
	if(ifp != NULL){
		ifp->rawrecvcnt++;
		ifp->lastrecv = secclock();
		ift = ifp->iftype;
	} else {
		ift = &Iftypes[0];
	}
	dump(ifp,IF_TRACE_IN,bp);
	
	if(ift->rcvf != NULL)
		(*ift->rcvf)(ifp,&bp);
	else
		free_p(&bp);	/* Nowhere to send it */

	/* Let everything else run - this keeps the system from wedging
	 * when we're hit by a big burst of packets
	 */
	kwait(NULL);
	goto loop;
}

/* put mbuf into Hopper for network task
 * returns 0 if OK
 */
int
net_route(struct iface *ifp,struct mbuf **bpp)
{
	if(bpp == NULL || *bpp == NULL)
		return 0;	/* bogus */
	pushdown(bpp,&ifp,sizeof(ifp));
	enqueue(&Hopper,bpp);
	return 0;
}

/* Null send and output routines for interfaces without link level protocols */
int
nu_send(struct mbuf **bpp,struct iface *ifp,int32 gateway,uint8 tos)
{
	return (*ifp->raw)(ifp,bpp);
}
int
nu_output(struct iface *ifp,uint8 *dest,uint8 *src,uint type,struct mbuf **bpp)
{
	return (*ifp->raw)(ifp,bpp);
}

/* Function to set encapsulation mode */
int
setencap(struct iface *ifp,char *mode)
{
	struct iftype *ift;

	for(ift = &Iftypes[0];ift->name != NULL;ift++)
		if(STRNICMP(ift->name,mode,strlen(mode)) == 0)
			break;
	if(ift->name == NULL)
		return -1;

	if(ifp != NULL){
		ifp->iftype = ift;
		ifp->send = ift->send;
		ifp->output = ift->output;
	}
	return 0;
}

/* Detach a specified interface */
int
if_detach(struct iface *ifp)
{
	struct iface *iftmp;
	struct route *rp,*rptmp;
	int i,j;

	if(ifp == &Loopback || ifp == &Encap)
		return -1;

	/* Drop all routes that point to this interface */
	if(R_default.iface == ifp)
		rt_drop(0L,0);	/* Drop default route */

	for(i=0;i<HASHMOD;i++){
		for(j=0;j<32;j++){
			for(rp = Routes[j][i];rp != NULL;rp = rptmp){
				/* Save next pointer in case we delete this entry */
				rptmp = rp->next;
				if(rp->iface == ifp)
					rt_drop(rp->target,rp->bits);
			}
		}
	}
	/* Unforward any other interfaces forwarding to this one */
	for(iftmp = Ifaces;iftmp != NULL;iftmp = iftmp->next){
		if(iftmp->forw == ifp)
			iftmp->forw = NULL;
	}
	/* Call device shutdown routine, if any */
	if(ifp->stop != NULL)
		(*ifp->stop)(ifp);

	killproc(&ifp->rxproc);
	killproc(&ifp->txproc);
	killproc(&ifp->supv);

	/* Free allocated memory associated with this interface */
	if(ifp->name != NULL)
		free(ifp->name);
	if(ifp->hwaddr != NULL)
		free(ifp->hwaddr);
	/* Remove from interface list */
	if(ifp == Ifaces){
		Ifaces = ifp->next;
	} else {
		/* Search for entry just before this one
		 * (necessary because list is only singly-linked.)
		 */
		for(iftmp = Ifaces;iftmp != NULL ;iftmp = iftmp->next)
			if(iftmp->next == ifp)
				break;
		if(iftmp != NULL && iftmp->next == ifp)
			iftmp->next = ifp->next;
	}
	/* Finally free the structure itself */
	free(ifp);
	return 0;
}

/* Given the ascii name of an interface, return a pointer to the structure,
 * or NULL if it doesn't exist
 */
struct iface *
if_lookup(char *name)
{
	struct iface *ifp;

	for(ifp = Ifaces; ifp != NULL; ifp = ifp->next)
		if(strcmp(ifp->name,name) == 0)
			break;
	return ifp;
}

/* Return iface pointer if 'addr' belongs to one of our interfaces,
 * NULL otherwise.
 * This is used to tell if an incoming IP datagram is for us, or if it
 * has to be routed.
 */
struct iface *
ismyaddr(int32 addr)
{
	struct iface *ifp;

	if(addr == kINADDR_ANY)
		return &Loopback;
	for(ifp = Ifaces; ifp != NULL; ifp = ifp->next)
		if(addr == ifp->addr)
			break;
	return ifp;
}

/* return buffer with name + comment */
char *
if_name(struct iface *ifp,char *comment)
{
	char *result;

	result = mallocw(strlen(ifp->name) + strlen(comment) + 1);
	strcpy(result,ifp->name);
	strcat(result,comment);
	return result;
}

/* Raw output routine that tosses all packets. Used by dialer, tip, etc */
int
bitbucket(struct iface *ifp,struct mbuf **bpp)
{
	free_p(bpp);
	return 0;
}
