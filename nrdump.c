/* NET/ROM header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "stdio.h"
#include "global.h"
#include "mbuf.h"
#include "netrom.h"
#include "nr4.h"
#include "trace.h"

/* Display NET/ROM network and transport headers */
void
netrom_dump(fp,bpp,check)
kFILE *fp;
struct mbuf **bpp;
int check;
{
	uint8 src[AXALEN],dest[AXALEN];
	char tmp[AXBUF];
	uint8 thdr[NR4MINHDR];
	int i;

	if(bpp == NULL || *bpp == NULL)
		return;
	/* See if it is a routing broadcast */
	if((*(*bpp)->data) == NR3NODESIG) {
		(void)PULLCHAR(bpp);		/* Signature */
		pullup(bpp,tmp,ALEN);
		tmp[ALEN] = '\0';
		kfprintf(fp,"NET/ROM Routing: %s\n",tmp);
		for(i = 0;i < NRDESTPERPACK;i++) {
			if (pullup(bpp,src,AXALEN) < AXALEN)
				break;
			kfprintf(fp,"        %12s",pax25(tmp,src));
			pullup(bpp,tmp,ALEN);
			tmp[ALEN] = '\0';
			kfprintf(fp,"%8s",tmp);
			pullup(bpp,src,AXALEN);
			kfprintf(fp,"    %12s",pax25(tmp,src));
			tmp[0] = PULLCHAR(bpp);
			kfprintf(fp,"    %3u\n",(tmp[0]));
		}
		return;
	}
	/* Decode network layer */
	pullup(bpp,src,AXALEN);
	kfprintf(fp,"NET/ROM: %s",pax25(tmp,src));

	pullup(bpp,dest,AXALEN);
	kfprintf(fp,"->%s",pax25(tmp,dest));

	i = PULLCHAR(bpp);
	kfprintf(fp," ttl %d\n",i);

	/* Read first five bytes of "transport" header */
	pullup(bpp,thdr,NR4MINHDR);
	switch(thdr[4] & NR4OPCODE){
 	case NR4OPPID:	/* network PID extension */
		if (thdr[0] == NRPROTO_IP && thdr[1] == NRPROTO_IP) {
 			ip_dump(fp,bpp,check) ;
			return;
		}
 		else
 			kfprintf(fp,"         protocol family %x, proto %x",
			 thdr[0], thdr[1]) ;
 		break ;
	case NR4OPCONRQ:	/* Connect request */
		kfprintf(fp,"         conn rqst: ckt %d/%d",(thdr[0]),(thdr[1]));
		i = PULLCHAR(bpp);
		kfprintf(fp," wnd %d",i);
		pullup(bpp,src,AXALEN);
		kfprintf(fp," %s",pax25(tmp,src));
		pullup(bpp,dest,AXALEN);
		kfprintf(fp,"@%s",pax25(tmp,dest));
		break;
	case NR4OPCONAK:	/* Connect acknowledgement */
		kfprintf(fp,"         conn ack: ur ckt %d/%d my ckt %d/%d",
		 thdr[0], thdr[1], thdr[2],thdr[3]);
		i = PULLCHAR(bpp);
		kfprintf(fp," wnd %d",i);
		break;
	case NR4OPDISRQ:	/* Disconnect request */
		kfprintf(fp,"         disc: ckt %d/%d",
		 thdr[0],thdr[1]);
		break;
	case NR4OPDISAK:	/* Disconnect acknowledgement */
		kfprintf(fp,"         disc ack: ckt %d/%d",
		 thdr[0],thdr[1]);
		break;
	case NR4OPINFO:	/* Information (data) */
		kfprintf(fp,"         info: ckt %d/%d",
		 thdr[0],thdr[1]);
		kfprintf(fp," txseq %d rxseq %d",
		 thdr[2],thdr[3]);
		break;
	case NR4OPACK:	/* Information acknowledgement */
		kfprintf(fp,"         info ack: ckt %d/%d",
		 thdr[0],thdr[1]);
		kfprintf(fp," txseq %d rxseq %d",thdr[2],thdr[3]);
		break;
	default:
 		kfprintf(fp,"         unknown transport type %d",
		 thdr[4] & 0x0f) ;
		break;
	}
	if(thdr[4] & NR4CHOKE)
		kfprintf(fp," CHOKE");
	if(thdr[4] & NR4NAK)
		kfprintf(fp," NAK");
	if(thdr[4] & NR4MORE)
		kfprintf(fp," MORE");
	kputc('\n',fp);
}

