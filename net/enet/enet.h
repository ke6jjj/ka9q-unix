#ifndef	_KA9Q_NET_ENET_H
#define _KA9Q_NET_ENET_H

/* Generic Ethernet constants and templates */
#include "global.h"
#include "net/core/mbuf.h"
#include "net/core/iface.h"

#define	EADDR_LEN	6
/* Format of an Ethernet header */
struct ether {
	uint8 dest[EADDR_LEN];
	uint8 source[EADDR_LEN];
	uint type;
};
#define	ETHERLEN	14

/* Ethernet broadcast address */
extern uint8 Ether_bdcst[];

/* Ethernet type fields */
#define	IP_TYPE		0x800	/* Type field for IP */
#define	ARP_TYPE	0x806	/* Type field for ARP */
#define	REVARP_TYPE	0x8035	/* Type field for reverse ARP */

#define	RUNT		60	/* smallest legal size packet, no fcs */
#define	GIANT		1514	/* largest legal size packet, no fcs */

#define	MAXTRIES	16	/* Maximum number of transmission attempts */

/* In file enet.c: */
char *pether(char *out,uint8 *addr);
int gether(uint8 *out,char *cp);
int enet_send(struct mbuf **bpp,struct iface *iface,int32 gateway,uint8 tos);
int enet_output(struct iface *iface,uint8 dest[],uint8 source[],uint type,
	struct mbuf **bpp);
void eproc(struct iface *iface,struct mbuf **bpp);

/* In enethdr.c: */
void htonether(struct ether *ether,struct mbuf **data);
int ntohether(struct ether *ether,struct mbuf **bpp);

#endif	/* _KA9Q_NET_ENET_H */
