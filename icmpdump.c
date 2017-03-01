/* ICMP header tracing
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "stdio.h"
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "netuser.h"
#include "icmp.h"
#include "trace.h"
#include "ip.h"

/* Dump an ICMP header */
void
icmp_dump(fp,bpp,source,dest,check)
kFILE *fp;
struct mbuf **bpp;
int32 source,dest;
int check;		/* If 0, bypass checksum verify */
{
	struct icmp icmp;
	uint csum;

	if(bpp == NULL || *bpp == NULL)
		return;
	csum = cksum(NULL,*bpp,len_p(*bpp));
	
	ntohicmp(&icmp,bpp);
	
	kfprintf(fp,"ICMP: type %s",smsg(Icmptypes,ICMP_TYPES,icmp.type));

	switch(icmp.type){
	case ICMP_DEST_UNREACH:
		kfprintf(fp," code %s",smsg(Unreach,NUNREACH,icmp.code));
		break;
	case ICMP_REDIRECT:
		kfprintf(fp," code %s",smsg(Redirect,NREDIRECT,icmp.code));
		kfprintf(fp," new gateway %s",inet_ntoa(icmp.args.address));
		break;
	case ICMP_TIME_EXCEED:
		kfprintf(fp," code %s",smsg(Exceed,NEXCEED,icmp.code));
		break;
	case ICMP_PARAM_PROB:
		kfprintf(fp," pointer %u",icmp.args.pointer);
		break;
	case ICMP_ECHO:
	case ICMP_ECHO_REPLY:
	case ICMP_INFO_RQST:
	case ICMP_INFO_REPLY:
	case ICMP_TIMESTAMP:
	case ICMP_TIME_REPLY:
		kfprintf(fp," id %u seq %u",icmp.args.echo.id,icmp.args.echo.seq);
		break;
	case ICMP_IPSP:
		kfprintf(fp," %s",smsg(Said_icmp,NIPSP,icmp.code));
		break;
	}
	if(check && csum != 0){
		kfprintf(fp," CHECKSUM ERROR (%u)",csum);
	}
	kputc('\n',fp);
	/* Dump the offending IP header, if any */
	switch(icmp.type){
	case ICMP_DEST_UNREACH:
	case ICMP_TIME_EXCEED:
	case ICMP_PARAM_PROB:
	case ICMP_QUENCH:
	case ICMP_REDIRECT:
	case ICMP_IPSP:
		kfprintf(fp,"Returned ");
		ip_dump(fp,bpp,0);
	}
}

