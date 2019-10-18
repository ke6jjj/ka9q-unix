/* UDP packet tracing
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "lib/std/stdio.h"
#include "global.h"
#include "net/core/mbuf.h"
#include "core/socket.h"
#include "core/trace.h"

#include "net/inet/internet.h"
#include "net/inet/udp.h"
#include "net/inet/ip.h"

/* Dump a UDP header */
void
udp_dump(fp,bpp,source,dest,check)
kFILE *fp;
struct mbuf **bpp;
int32 source,dest;
int check;		/* If 0, bypass checksum verify */
{
	struct udp udp;
	struct pseudo_header ph;
	uint csum;

	if(bpp == NULL || *bpp == NULL)
		return;

	kfprintf(fp,"UDP:");

	/* Compute checksum */
	ph.source = source;
	ph.dest = dest;
	ph.protocol = UDP_PTCL;
	ph.length = len_p(*bpp);
	if((csum = cksum(&ph,*bpp,ph.length)) == 0)
		check = 0;	/* No checksum error */

	ntohudp(&udp,bpp);

	kfprintf(fp," len %u",udp.length);
	kfprintf(fp," %u->%u",udp.source,udp.dest);
	if(udp.length > UDPHDR)
		kfprintf(fp," Data %u",udp.length - UDPHDR);
	if(udp.checksum == 0)
		check = 0;
	if(check)
		kfprintf(fp," CHECKSUM ERROR (%u)",csum);

	kputc('\n',fp);

	switch(udp.dest){
	case IPPORT_RIP:
		rip_dump(fp,bpp);
	}

}

