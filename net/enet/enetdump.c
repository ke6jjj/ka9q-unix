/* Ethernet header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "../../top.h"

#include "../../stdio.h"
#include "../../global.h"
#include "../../mbuf.h"
#include "../../trace.h"

#include "enet.h"

void
ether_dump(
kFILE *fp,
struct mbuf **bpp,
int check	/* Not used */
){
	struct ether ehdr;
	char s[20],d[20];

	ntohether(&ehdr,bpp);
	pether(s,ehdr.source);
	pether(d,ehdr.dest);
	kfprintf(fp,"Ether: len %u %s->%s",ETHERLEN + len_p(*bpp),s,d);

	switch(ehdr.type){
		case IP_TYPE:
			kfprintf(fp," type IP\n");
			ip_dump(fp,bpp,1);
			break;
		case REVARP_TYPE:
			kfprintf(fp," type REVARP\n");
			arp_dump(fp,bpp);
			break;
		case ARP_TYPE:
			kfprintf(fp," type ARP\n");
			arp_dump(fp,bpp);
			break;
		default:
			kfprintf(fp," type 0x%x\n",ehdr.type);
			break;
	}
}
int
ether_forus(struct iface *iface,struct mbuf *bp)
{
	/* Just look at the multicast bit */

	if(bp->data[0] & 1)
		return 0;
	else
		return 1;
}
