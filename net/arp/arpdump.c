/* ARP packet tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "lib/std/stdio.h"
#include "global.h"
#include "../../mbuf.h"
#include "../../trace.h"

#include "lib/inet/netuser.h"

#include "net/arp/arp.h"

void
arp_dump(fp,bpp)
kFILE *fp;
struct mbuf **bpp;
{
	struct arp arp;
	struct arp_type *at;
	int is_ip = 0;
	char tmp[25];

	if(bpp == NULL || *bpp == NULL)
		return;
	kfprintf(fp,"ARP: len %d",len_p(*bpp));
	if(ntoharp(&arp,bpp) == -1){
		kfprintf(fp," bad packet\n");
		return;
	}
	if(arp.hardware < NHWTYPES)
		at = &Arp_type[arp.hardware];
	else
		at = NULL;

	/* Print hardware type in Ascii if known, numerically if not */
	kfprintf(fp," hwtype %s",smsg(Arptypes,NHWTYPES,arp.hardware));

	/* Print hardware length only if unknown type, or if it doesn't match
	 * the length in the known types table
	 */
	if(at == NULL || arp.hwalen != at->hwalen)
		kfprintf(fp," hwlen %u",arp.hwalen);

	/* Check for most common case -- upper level protocol is IP */
	if(at != NULL && arp.protocol == at->iptype){
		kfprintf(fp," prot IP");
		is_ip = 1;
	} else {
		kfprintf(fp," prot 0x%x prlen %u",arp.protocol,arp.pralen);
	}
	switch(arp.opcode){
	case ARP_REQUEST:
		kfprintf(fp," op REQUEST");
		break;
	case ARP_REPLY:
		kfprintf(fp," op REPLY");
		break;
	case REVARP_REQUEST:
		kfprintf(fp," op REVERSE REQUEST");
		break;
	case REVARP_REPLY:
		kfprintf(fp," op REVERSE REPLY");
		break;
	default:
		kfprintf(fp," op %u",arp.opcode);
		break;
	}
	kfprintf(fp,"\n");
	kfprintf(fp,"sender");
	if(is_ip)
		kfprintf(fp," IPaddr %s",inet_ntoa(arp.sprotaddr));
	kfprintf(fp," hwaddr %s\n",at->format(tmp,arp.shwaddr));

	kfprintf(fp,"target");
	if(is_ip)
		kfprintf(fp," IPaddr %s",inet_ntoa(arp.tprotaddr));
	kfprintf(fp," hwaddr %s\n",at->format(tmp,arp.thwaddr));
}
