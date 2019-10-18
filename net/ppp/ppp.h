#ifndef _KA9Q_NET_PPP_H
#define _KA9Q_NET_PPP_H
/*
 *	This implementation of PPP is declared to be in the public domain.
 *
 *	Jan 91	Bill_Simpson@um.cc.umich.edu
 *		Computer Systems Consulting Services
 *
 *	Acknowledgements and correction history may be found in PPP.C
 */

#include "global.h"
#include "net/core/mbuf.h"
#include "net/core/iface.h"

/* PPP definitions */
#define	PPP_ALLOC	128	/* mbuf allocation increment */


struct ppp_hdr {
	byte_t addr;
#define HDLC_ALL_ADDR		0xff	/* HDLC all-station */
	byte_t control;
#define HDLC_UI			0x03	/* HDLC Unnumbered Information */
	uint protocol;
#define PPP_IP_PROTOCOL		0x0021	/* Internet Protocol */
#define PPP_COMPR_PROTOCOL	0x002d	/* Van Jacobson Compressed TCP/IP */
#define PPP_UNCOMP_PROTOCOL	0x002f	/* Van Jacobson Uncompressed TCP/IP */
#define PPP_IPCP_PROTOCOL	0x8021	/* Internet Protocol Control Protocol */
#define PPP_LCP_PROTOCOL	0xc021	/* Link Control Protocol */
#define PPP_PAP_PROTOCOL	0xc023	/* Password Authentication Protocol */
};
#define PPP_HDR_LEN	4	/* Max bytes for PPP/HDLC envelope header */

/* HDLC envelope constants */
#define HDLC_ENVLEN	8	/* Max bytes for HDLC envelope (outgoing) */

#define HDLC_FLAG	0x7e	/* HDLC async start/stop flag */
#define HDLC_ESC_ASYNC	0x7d	/* HDLC transparency escape flag for async */
#define HDLC_ESC_COMPL	0x20	/* HDLC transparency bit complement mask */

#define HDLC_FCS_START	0xffff	/* Starting bit string for FCS calculation */
#define HDLC_FCS_FINAL	0xf0b8	/* FCS when summed over frame and sender FCS */


/* In ppp.c: */
int ppp_send(struct mbuf **data,struct iface *iface,int32 gateway,
	uint8 tos);
int ppp_output(struct iface *iface, uint8 *dest, uint8 *source,
	uint type, struct mbuf **data);

int ppp_init(struct iface *iface);
int ppp_free(struct iface *iface);
void ppp_proc(struct iface *iface, struct mbuf **bp);

/* In pppcmd.c */
extern int PPPtrace;		/* trace flag */
extern struct iface *PPPiface;	/* iface for trace */

void ppp_show(struct iface *ifp);

int doppp_commands(int argc,char *argv[], void *p);

int doppp_active(int argc, char *argv[], void *p);
int doppp_passive(int argc, char *argv[], void *p);

int doppp_close(int argc, char *argv[], void *p);
int doppp_timeout(int argc, char *argv[], void *p);
int doppp_try(int argc, char *argv[], void *p);

#endif	/* _KA9Q_NET_PPP_H */
