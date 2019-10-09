#ifndef	_KA9Q_TRACE_H
#define	_KA9Q_TRACE_H

#include "mbuf.h"
#include "iface.h"

/* Definitions for packet dumping */

/* Table of trace subcommands */
struct tracecmd {
	char *name;	/* Name of trace subcommand */
	int val;	/* New setting for these bits */
	int mask;	/* Mask of bits affected in trace word */
};
extern struct tracecmd Tracecmd[];	/* Defined in trace.c */

/* List of address testing and tracing functions for each interface.
 * Entries are placed in this table by conditional compilation in main.c.
 */
struct trace {
	int (*addrtest)(struct iface *iface,struct mbuf *bp);
	void (*tracef)(kFILE *,struct mbuf **,int);
};

extern struct trace Tracef[];

/* In trace.c: */
void dump(struct iface *ifp,int direction,struct mbuf *bp);
void raw_dump(struct iface *ifp,int direction, struct mbuf *bp);
void trace_log(struct iface *ifp,char *fmt, ...);
void shuttrace(void);
int tprintf(struct iface *ifp,char *fmt,...);
void hex_dump(kFILE *fp,struct mbuf **bpp);

/* In arcdump.c: */
void arc_dump(kFILE *fp,struct mbuf **bpp,int check);
int arc_forus(struct iface *iface,struct mbuf *bp);

/* In arpdump.c: */
void arp_dump(kFILE *fp,struct mbuf **bpp);

/* In ax25dump.c: */
void ax25_dump(kFILE *fp,struct mbuf **bpp,int check);
int ax_forus(struct iface *iface,struct mbuf *bp);

/* In enetdump.c: */
void ether_dump(kFILE *fp,struct mbuf **bpp,int check);
int ether_forus(struct iface *iface,struct mbuf *bp);

/* In icmpdump.c: */
void icmp_dump(kFILE *fp,struct mbuf **bpp,int32 source,int32 dest,int check);

/* In ipdump.c: */
void ipip_dump(kFILE *fp,struct mbuf **bpp,int32 source,int32 dest,int check);
void ip_dump(kFILE *fp,struct mbuf **bpp,int check);

/* In kissdump.c: */
void ki_dump(kFILE *fp,struct mbuf **bpp,int check);
int ki_forus(struct iface *iface,struct mbuf *bp);

/* In nrdump.c: */
void netrom_dump(kFILE *fp,struct mbuf **bpp,int check);

/* In pppdump.c: */
void ppp_dump(kFILE *fp,struct mbuf **bpp,int check);

/* In ripdump.c: */
void rip_dump(kFILE *fp,struct mbuf **bpp);

/* In secdump.c: */
void esp_dump(kFILE *fp,struct mbuf **bpp,int32 source,int32 dest,int check);
void ah_dump(kFILE *fp,struct mbuf **bpp,int32 source,int32 dest,int check);

/* In slcompdump.c: */
void sl_dump(kFILE *fp,struct mbuf **bpp,int check);
void vjcomp_dump(kFILE *fp,struct mbuf **bpp,int unused);

/* In tcpdump.c: */
void tcp_dump(kFILE *fp,struct mbuf **bpp,int32 source,int32 dest,int check);

/* In udpdump.c: */
void udp_dump(kFILE *fp,struct mbuf **bpp,int32 source,int32 dest,int check);

#endif	/* _KA9Q_TRACE_H */
