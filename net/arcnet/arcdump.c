/* ARCNET trace routines
 * Copyright 1990 Russ Nelson
 */
#include "top.h"

#include "lib/std/stdio.h"
#include "global.h"
#include "net/core/mbuf.h"
#include "net/arcnet/arcnet.h"
#include "core/trace.h"

void
arc_dump(fp,bpp,check)
kFILE *fp;
struct mbuf **bpp;
int check;	/* Not used */
{
	struct arc ahdr;
	char s[20],d[20];

	ntoharc(&ahdr,bpp);
	parc(s,ahdr.source);
	parc(d,ahdr.dest);
	kfprintf(fp,"Arcnet: len %u %s->%s",ARCLEN + len_p(*bpp),s,d);

	switch(ahdr.type){
		case ARC_IP:
			kfprintf(fp," type IP\n");
			ip_dump(fp,bpp,1);
			break;
		case ARC_ARP:
			kfprintf(fp," type ARP\n");
			arp_dump(fp,bpp);
			break;
		default:
			kfprintf(fp," type 0x%x\n",ahdr.type);
			break;
	}
}
int
arc_forus(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
	return 1;
}
