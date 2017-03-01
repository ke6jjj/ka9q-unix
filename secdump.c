/* Security packet tracing
 * Copyright 1993 Phil Karn, KA9Q
 */
#include "top.h"

#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "trace.h"
#include "ipsec.h"
#include "photuris.h"

void
esp_dump(fp,bpp,source,dest,check)
kFILE *fp;
struct mbuf **bpp;
int32 source,dest;
int check;
{
	int32 spi;

	spi = pull32(bpp);
	kfprintf(fp,"ESP: SPI %lx\n",spi);
}

void
ah_dump(fp,bpp,source,dest,check)
kFILE *fp;
struct mbuf **bpp;
int32 source,dest;
int check;
{
	int32 spi;
	int protocol,authlen,i;
	uint8 *auth;

	protocol = PULLCHAR(bpp);
	authlen = sizeof(int32) * PULLCHAR(bpp);
	pull16(bpp);
	spi = pull32(bpp);
	kfprintf(fp,"AH: SPI %lx",spi);
	if(authlen != 0){
		kfprintf(fp," auth ");
		auth = mallocw(authlen);
		pullup(bpp,auth,authlen);
		for(i=0;i<authlen;i++)
			kfprintf(fp,"%02x",auth[i]);
	}
	switch(protocol){
	case IP4_PTCL:
	case IP_PTCL:
		kfprintf(fp," prot IP\n");
		ip_dump(fp,bpp,check);
		break;
	case TCP_PTCL:
		kfprintf(fp," prot TCP\n");
		tcp_dump(fp,bpp,source,dest,check);
		break;
	case UDP_PTCL:
		kfprintf(fp," prot UDP\n");
		udp_dump(fp,bpp,source,dest,check);
		break;
	case ICMP_PTCL:
		kfprintf(fp," prot ICMP\n");
		icmp_dump(fp,bpp,source,dest,check);
		break;
	case ESP_PTCL:
		kfprintf(fp," prot ESP\n");
		esp_dump(fp,bpp,source,dest,check);
		break;
	case AH_PTCL:
		kfprintf(fp," prot AH\n");
		ah_dump(fp,bpp,source,dest,check);
		break;
	default:
		kfprintf(fp," prot %u\n",protocol);
		break;
	}

}


