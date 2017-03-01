#ifndef	_UDP_H
#define	_UDP_H

#ifndef	_GLOBAL_H
#include "global.h"
#endif

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef	_IFACE_H
#include "iface.h"
#endif

#ifndef	_INTERNET_H
#include "internet.h"
#endif

#ifndef _IP_H
#include "ip.h"
#endif

#ifndef	_NETUSER_H
#include "netuser.h"
#endif

/* SNMP MIB variables, used for statistics and control. See RFC 1066 */
extern struct mib_entry Udp_mib[];
#define	udpInDatagrams	Udp_mib[1].value.integer
#define	udpNoPorts	Udp_mib[2].value.integer
#define	udpInErrors	Udp_mib[3].value.integer
#define	udpOutDatagrams	Udp_mib[4].value.integer
#define	NUMUDPMIB	4

/* User Datagram Protocol definitions */

/* Structure of a UDP protocol header */
struct udp {
	uint source;	/* Source port */
	uint dest;	/* Destination port */
	uint length;	/* Length of header and data */
	uint checksum;	/* Checksum over pseudo-header, header and data */
};
#define	UDPHDR	8	/* Length of UDP header */

/* User Datagram Protocol control block
 * Each entry on the receive queue consists of the
 * remote ksocket structure, followed by any data
 */
struct udp_cb {
	struct udp_cb *next;
	struct ksocket socket;	/* Local port accepting datagrams */
	void (*r_upcall)(struct iface *iface,struct udp_cb *,int);
				/* Function to call when one arrives */
	struct mbuf *rcvq;	/* Queue of pending datagrams */
	int rcvcnt;		/* Count of pending datagrams */
	int user;		/* User link */
};
extern struct udp_cb *Udps;	/* Hash table for UDP structures */

/* UDP primitives */

/* In udp.c: */
int del_udp(struct udp_cb **up);
struct udp_cb *open_udp(struct ksocket *lsocket,
	void (*r_upcall)(struct iface *iface,struct udp_cb *,int));
int recv_udp(struct udp_cb *up,struct ksocket *fsocket,struct mbuf **bp);
int send_udp(struct ksocket *lsocket,struct ksocket *fsocket,char tos,
	char ttl,struct mbuf **data,uint length,uint id,char df);
void udp_input(struct iface *iface,struct ip *ip,struct mbuf **bp,
	int rxbroadcast,int32 said);
void udp_garbage(int drastic);

/* In udpcmd.c: */
int st_udp(struct udp_cb *udp,int n);

/* In udphdr.c: */
void htonudp(struct udp *udp,struct mbuf **data,struct pseudo_header *ph);
int ntohudp(struct udp *udp,struct mbuf **bpp);
uint udpcksum(struct mbuf *bp);

#endif	/* _UDP_H */
