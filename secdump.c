/* Security packet tracing
 * Copyright 1993 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "trace.h"
#include "ipsec.h"
#include "photuris.h"

void
esp_dump(fp,bpp,source,dest,check)
FILE *fp;
struct mbuf **bpp;
int32 source,dest;
int check;
{
	int32 spi;

	spi = pull32(bpp);
	fprintf(fp,"ESP: SPI %lx\n",spi);
}

void
ah_dump(fp,bpp,source,dest,check)
FILE *fp;
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
	fprintf(fp,"AH: SPI %lx",spi);
	if(authlen != 0){
		fprintf(fp," auth ");
		auth = mallocw(authlen);
		pullup(bpp,auth,authlen);
		for(i=0;i<authlen;i++)
			fprintf(fp,"%02x",auth[i]);
	}
	switch(protocol){
	case IP4_PTCL:
	case IP_PTCL:
		fprintf(fp," prot IP\n");
		ip_dump(fp,bpp,check);
		break;
	case TCP_PTCL:
		fprintf(fp," prot TCP\n");
		tcp_dump(fp,bpp,source,dest,check);
		break;
	case UDP_PTCL:
		fprintf(fp," prot UDP\n");
		udp_dump(fp,bpp,source,dest,check);
		break;
	case ICMP_PTCL:
		fprintf(fp," prot ICMP\n");
		icmp_dump(fp,bpp,source,dest,check);
		break;
	case ESP_PTCL:
		fprintf(fp," prot ESP\n");
		esp_dump(fp,bpp,source,dest,check);
		break;
	case AH_PTCL:
		fprintf(fp," prot AH\n");
		ah_dump(fp,bpp,source,dest,check);
		break;
	default:
		fprintf(fp," prot %u\n",protocol);
		break;
	}

}


