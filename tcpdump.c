/* TCP header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "stdio.h"
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "tcp.h"
#include "ip.h"
#include "trace.h"

/* TCP segment header flags */
static char *Tcpflags[] = {
	"FIN",	/* 0x01 */
	"SYN",	/* 0x02 */
	"RST",	/* 0x04 */
	"PSH",	/* 0x08 */
	"ACK",	/* 0x10 */
	"URG",	/* 0x20 */
	"CE"	/* 0x40 */
};

/* Dump a TCP segment header. Assumed to be in network byte order */
void
tcp_dump(fp,bpp,source,dest,check)
kFILE *fp;
struct mbuf **bpp;
int32 source,dest;	/* IP source and dest addresses */
int check;		/* 0 if checksum test is to be bypassed */
{
	struct tcp seg;
	struct pseudo_header ph;
	uint csum;
	uint dlen;

	if(bpp == NULL || *bpp == NULL)
		return;

	/* Verify checksum */
	ph.source = source;
	ph.dest = dest;
	ph.protocol = TCP_PTCL;
	ph.length = len_p(*bpp);
	csum = cksum(&ph,*bpp,ph.length);

	ntohtcp(&seg,bpp);

	kfprintf(fp,"TCP: %u->%u Seq x%lx",seg.source,seg.dest,seg.seq,seg.ack);
	if(seg.flags.ack)
		kfprintf(fp," Ack x%lx",seg.ack);
	if(seg.flags.congest)
		kfprintf(fp," %s",Tcpflags[6]);
	if(seg.flags.urg)
		kfprintf(fp," %s",Tcpflags[5]);
	if(seg.flags.ack)
		kfprintf(fp," %s",Tcpflags[4]);
	if(seg.flags.psh)
		kfprintf(fp," %s",Tcpflags[3]);
	if(seg.flags.rst)
		kfprintf(fp," %s",Tcpflags[2]);
	if(seg.flags.syn)
		kfprintf(fp," %s",Tcpflags[1]);
	if(seg.flags.fin)
		kfprintf(fp," %s",Tcpflags[0]);

	kfprintf(fp," Wnd %u",seg.wnd);
	if(seg.flags.urg)
		kfprintf(fp," UP x%x",seg.up);
	/* Print options, if any */
	if(seg.flags.mss)
		kfprintf(fp," MSS %u",seg.mss);
	if(seg.flags.wscale)
		kfprintf(fp," WSCALE %u",seg.wsopt);
	if(seg.flags.tstamp)
		kfprintf(fp," TSTAMP %lu TSECHO %lu",seg.tsval,seg.tsecr);
	if((dlen = len_p(*bpp)) != 0)
		kfprintf(fp," Data %u",dlen);
	if(check && csum != 0)
		kfprintf(fp," CHECKSUM ERROR (%u)",csum);
	kputc('\n',fp);
}

