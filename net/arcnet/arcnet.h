/* Generic ARCnet constants and templates */

#ifndef	_KA9Q_ARCNET_H
#define	_KA9Q_ARCNET_H

#include "global.h"
#include "mbuf.h"
#include "iface.h"

#define	AADDR_LEN	1
/* Format of an ARCnet header */
struct arc {
	uint8 source[AADDR_LEN];
	uint8 dest[AADDR_LEN];
	uint8 type;
};
#define	ARCLEN	3

/* ARCnet broadcast address */
extern uint8 ARC_bdcst[];

/* ARCnet type fields */
#define	ARC_IP		0xf0	/* Type field for IP */
#define	ARC_ARP		0xf1	/* Type field for ARP */

/* In file arcnet.c: */
void htonarc(struct arc *arc,struct mbuf **data);
int ntoharc(struct arc *arc,struct mbuf **bpp);
char *parc(char *out,uint8 *addr);
int garc(uint8 *out,char *cp);
int anet_send(struct mbuf **bp,struct iface *iface,int32 gateway,uint8 tos);
int anet_output(struct iface *iface,uint8 dest[],uint8 source[],uint type,
	struct mbuf **data);
void aproc(struct iface *iface,struct mbuf **bp);

#endif	/* _KA9Q_ARCNET_H */
