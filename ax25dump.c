/* AX25 header tracing
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "stdio.h"
#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "lapb.h"
#include "trace.h"
#include "socket.h"

static char *decode_type(uint type);

/* Dump an AX.25 packet header */
void
ax25_dump(
kFILE *fp,
struct mbuf **bpp,
int check	/* Not used */
){
	char tmp[AXBUF];
	char frmr[3];
	int control,pid,seg;
	uint type;
	int unsegmented;
	struct ax25 hdr;
	uint8 *hp;

	kfprintf(fp,"AX25: ");
	/* Extract the address header */
	if(ntohax25(&hdr,bpp) < 0){
		/* Something wrong with the header */
		kfprintf(fp," bad header!\n");
		return;
	}
	kfprintf(fp,"%s",pax25(tmp,hdr.source));
	kfprintf(fp,"->%s",pax25(tmp,hdr.dest));
	if(hdr.ndigis > 0){
		kfprintf(fp," v");
		for(hp = hdr.digis[0]; hp < &hdr.digis[hdr.ndigis][0];
		 hp += AXALEN){
			/* Print digi string */
			kfprintf(fp," %s%s",pax25(tmp,hp),
			 (hp[ALEN] & REPEATED) ? "*":"");
		}
	}
	if((control = PULLCHAR(bpp)) == -1)
		return;

	kputc(' ',fp);
	type = ftype(control);
	kfprintf(fp,"%s",decode_type(type));
	/* Dump poll/final bit */
	if(control & PF){
		switch(hdr.cmdrsp){
		case LAPB_COMMAND:
			kfprintf(fp,"(P)");
			break;
		case LAPB_RESPONSE:
			kfprintf(fp,"(F)");
			break;
		default:
			kfprintf(fp,"(P/F)");
			break;
		}
	}
	/* Dump sequence numbers */
	if((type & 0x3) != U)	/* I or S frame? */
		kfprintf(fp," NR=%d",(control>>5)&7);
	if(type == I || type == UI){	
		if(type == I)
			kfprintf(fp," NS=%d",(control>>1)&7);
		/* Decode I field */
		if((pid = PULLCHAR(bpp)) != -1){	/* Get pid */
			if(pid == PID_SEGMENT){
				unsegmented = 0;
				seg = PULLCHAR(bpp);
				kfprintf(fp,"%s remain %u",seg & SEG_FIRST ?
				 " First seg;" : "",seg & SEG_REM);
				if(seg & SEG_FIRST)
					pid = PULLCHAR(bpp);
			} else
				unsegmented = 1;

			switch(pid){
			case PID_SEGMENT:
				kputc('\n',fp);
				break;	/* Already displayed */
			case PID_ARP:
				kfprintf(fp," pid=ARP\n");
				arp_dump(fp,bpp);
				break;
			case PID_NETROM:
				kfprintf(fp," pid=NET/ROM\n");
				/* Don't verify checksums unless unsegmented */
				netrom_dump(fp,bpp,unsegmented);
				break;
			case PID_IP:
				kfprintf(fp," pid=IP\n");
				/* Don't verify checksums unless unsegmented */
				ip_dump(fp,bpp,unsegmented);
				break;
			case PID_X25:
				kfprintf(fp," pid=X.25\n");
				break;
			case PID_TEXNET:
				kfprintf(fp," pid=TEXNET\n");
				break;
			case PID_NO_L3:
				kfprintf(fp," pid=Text\n");
				break;
			default:
				kfprintf(fp," pid=0x%x\n",pid);
			}
		}
	} else if(type == FRMR && pullup(bpp,frmr,3) == 3){
		kfprintf(fp,": %s",decode_type(ftype(frmr[0])));
		kfprintf(fp," Vr = %d Vs = %d",(frmr[1] >> 5) & MMASK,
			(frmr[1] >> 1) & MMASK);
		if(frmr[2] & W)
			kfprintf(fp," Invalid control field");
		if(frmr[2] & X)
			kfprintf(fp," Illegal I-field");
		if(frmr[2] & Y)
			kfprintf(fp," Too-long I-field");
		if(frmr[2] & Z)
			kfprintf(fp," Invalid seq number");
		kputc('\n',fp);
	} else
		kputc('\n',fp);

}
static char *
decode_type(uint type)
{
	switch(type){
	case I:
		return "I";
	case SABM:
		return "SABM";
	case DISC:
		return "DISC";
	case DM:
		return "DM";
	case UA:
		return "UA";
	case RR:
		return "RR";
	case RNR:
		return "RNR";
	case REJ:
		return "REJ";
	case FRMR:
		return "FRMR";
	case UI:
		return "UI";
	default:
		return "[invalid]";
	}
}

/* Return 1 if this packet is directed to us, 0 otherwise. Note that
 * this checks only the ultimate destination, not the digipeater field
 */
int
ax_forus(
struct iface *iface,
struct mbuf *bp
){
	struct mbuf *bpp;
	uint8 dest[AXALEN];

	/* Duplicate the destination address */
	if(dup_p(&bpp,bp,0,AXALEN) != AXALEN){
		free_p(&bpp);
		return 0;
	}
	if(pullup(&bpp,dest,AXALEN) < AXALEN)
		return 0;
	if(addreq(dest,iface->hwaddr))
		return 1;
	else
		return 0;
}

