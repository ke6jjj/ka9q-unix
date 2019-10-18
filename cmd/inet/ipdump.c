/* IP header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"
#include "lib/std/stdio.h"
#include "global.h"
#include "net/core/mbuf.h"
#include "core/trace.h"
#include "core/session.h"
#include "net/core/iface.h"

#include "net/inet/internet.h"
#include "net/inet/ip.h"
#include "lib/inet/netuser.h"

void ipldump(kFILE *fp,struct ip *ip,struct mbuf **bpp,int check);

void
ip_dump(
kFILE *fp,
struct mbuf **bpp,
int check
){
	struct ip ip;
	uint ip_len;
	uint csum;

	if(bpp == NULL || *bpp == NULL)
		return;	

	/* Sneak peek at IP header and find length */
	ip_len = ((*bpp)->data[0] & 0xf) << 2;
	if(ip_len < IPLEN){
		kfprintf(fp,"IP: bad header\n");
		return;
	}
	if(check && (csum = cksum(NULL,*bpp,ip_len)) != 0)
		kfprintf(fp,"IP: CHECKSUM ERROR (%u)",csum);

	ntohip(&ip,bpp);	/* Can't fail, we've already checked ihl */
	ipldump(fp,&ip,bpp,check);
}
void
ipip_dump(
kFILE *fp,
struct mbuf **bpp,
int32 source,
int32 dest,
int check
){
	ip_dump(fp,bpp,check);
}
void
ipldump(fp,ip,bpp,check)
kFILE *fp;
struct ip *ip;
struct mbuf **bpp;
int check;
{
	uint length;
	int i;

	/* Trim data segment if necessary. */
	length = ip->length - (IPLEN + ip->optlen);	/* Length of data portion */
	trim_mbuf(bpp,length);	
	kfprintf(fp,"IP: len %u",ip->length);
	kfprintf(fp," %s",inet_ntoa(ip->source));
	kfprintf(fp,"->%s ihl %u ttl %u",
		inet_ntoa(ip->dest),IPLEN + ip->optlen,ip->ttl);
	if(ip->tos != 0)
		kfprintf(fp," tos %u",ip->tos);
	if(ip->offset != 0 || ip->flags.mf)
		kfprintf(fp," id %u offs %u",ip->id,ip->offset);
	if(ip->flags.df)
		kfprintf(fp," DF");
	if(ip->flags.mf){
		kfprintf(fp," MF");
		check = 0;	/* Bypass host-level checksum verify */
	}
	if(ip->flags.congest){
		kfprintf(fp," CE");
	}
	if(ip->offset != 0){
		kputc('\n',fp);
		return;
	}
	for(i=0;Iplink[i].proto != 0;i++){
		if(Iplink[i].proto == ip->protocol){
			kfprintf(fp," prot %s\n",Iplink[i].name);
			(*Iplink[i].dump)(fp,bpp,ip->source,ip->dest,check);
			return;
		}
	}			
	kfprintf(fp," prot %u\n",ip->protocol);
}
/* Dump a locally sent or received IP datagram to the command interp session */
void
dumpip(iface,ip,bp,spi)
struct iface *iface;
struct ip *ip;
struct mbuf *bp;
int32 spi;
{
	struct mbuf *bpp;

	if(iface != NULL){
		kfprintf(Command->output,"ip_recv(%s)",iface->name);
		if(spi != 0)
			kfprintf(Command->output," spi %lx",spi);
		kfprintf(Command->output,"\n");
	} else
		kfprintf(Command->output,"ip_send\n");

	dup_p(&bpp,bp,0,len_p(bp));
	ipldump(Command->output,ip,&bpp,1);
	free_p(&bpp);
	kfprintf(Command->output,"\n");
}

