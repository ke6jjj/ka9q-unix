#ifndef	_KA9Q_USOCK_H
#define	_KA9Q_USOCK_H

#include "net/core/mbuf.h"
#include "lzw.h"
#include "core/proc.h"
#include "core/sockaddr.h"
#include "net/inet/ip.h"
#include "net/inet/tcp.h"
#include "net/inet/udp.h"

struct loc {
	struct usock *peer;
	struct mbuf *q;
	int hiwat;		/* Flow control point */
	int flags;
#define	LOC_SHUTDOWN	1
};
#define	LOCDFLOW	5	/* dgram ksocket flow-control point, packets */
#define	LOCSFLOW	2048	/* stream ksocket flow control point, bytes */
#define	SOCKBASE	128	/* Start of ksocket indexes */

union sp {
        struct ksockaddr *sa;
        struct ksockaddr_in *in;
        struct ksockaddr_ax *ax;
        struct ksockaddr_nr *nr;
};
struct socklink {
	int type;		/* Socket type */
	int (*ksocket)(struct usock *,int);
	int (*bind)(struct usock *);
	int (*klisten)(struct usock *,int);
	int (*kconnect)(struct usock *);
	int kaccept;
	int (*recv)(struct usock *,struct mbuf **,struct ksockaddr *,int *);
	int (*send)(struct usock *,struct mbuf **,struct ksockaddr *);
	int (*qlen)(struct usock *,int);
	int (*kick)(struct usock *);
	int (*shut)(struct usock *,int);
	int (*kclose)(struct usock *);
	int (*check)(struct ksockaddr *,int);
	char **error;
	char *(*state)(struct usock *);
	int (*status)(struct usock *);
	char *eol;
};
extern struct socklink Socklink[];
union cb {
	struct tcb *tcb;
	struct ax25_cb *ax25;
	struct udp_cb *udp;
	struct raw_ip *rip;
	struct raw_nr *rnr;
	struct nr4cb *nr4;
	struct loc *local;
	void *p;
};
/* User sockets */
struct usock {
	unsigned index;
	struct proc *owner;
	int refcnt;
	char noblock;
	enum {
		NOTUSED,
		TYPE_TCP,
		TYPE_UDP,
		TYPE_AX25I,
		TYPE_AX25UI,
		TYPE_RAW,
		TYPE_NETROML3,
		TYPE_NETROML4,
		TYPE_LOCAL_STREAM,
		TYPE_LOCAL_DGRAM,
	} type;
	struct socklink *sp;
	int rdysock;
	union cb cb;
	struct ksockaddr *name;
	int namelen;
	struct ksockaddr *peername;
	int peernamelen;
	uint8 errcodes[4];	/* Protocol-specific error codes */
	uint8 tos;		/* Internet type-of-service */
	int flag;		/* Mode flags, defined in socket.h */
};
extern char *(*Psock[])(struct ksockaddr *);
extern char Badsocket[];
extern char *Socktypes[];
extern struct usock **Usock;
extern unsigned Nsock;

struct usock *itop(int s);
void st_garbage(int red);
int so_ip_autobind(struct usock *up);

/* In axsocket.c: */
int so_ax_sock(struct usock *up,int protocol);
int so_ax_bind(struct usock *up);
int so_ax_listen(struct usock *up,int backlog);
int so_ax_conn(struct usock *up);
int so_ax_recv(struct usock *up,struct mbuf **bpp,struct ksockaddr *from,
	int *fromlen);
int so_ax_send(struct usock *up,struct mbuf **bp,struct ksockaddr *to);
int so_ax_qlen(struct usock *up,int rtx);
int so_ax_kick(struct usock *up);
int so_ax_shut(struct usock *up,int how);
int so_ax_close(struct usock *up);
int checkaxaddr(struct ksockaddr *name,int namelen);
int so_axui_sock(struct usock *up,int protocol);
int so_axui_bind(struct usock *up);
int so_axui_conn(struct usock *up);
int so_axui_recv(struct usock *up,struct mbuf **bpp,struct ksockaddr *from,
	int *fromlen);
int so_axui_send(struct usock *up,struct mbuf **bp,struct ksockaddr *to);
int so_axui_qlen(struct usock *up,int rtx);
int so_axui_shut(struct usock *up,int how);
int so_axui_close(struct usock *up);
char *axpsocket(struct ksockaddr *p);
char *axstate(struct usock *up);
int so_ax_stat(struct usock *up);


/* In ipsocket.c: */
int so_ip_sock(struct usock *up,int protocol);
int so_ip_conn(struct usock *up);
int so_ip_recv(struct usock *up,struct mbuf **bpp,struct ksockaddr *from,
	int *fromlen);
int so_ip_send(struct usock *up,struct mbuf **bp,struct ksockaddr *to);
int so_ip_qlen(struct usock *up,int rtx);
int so_ip_close(struct usock *up);
int checkipaddr(struct ksockaddr *name,int namelen);
char *ippsocket(struct ksockaddr *p);

/* In locsocket.c: */
int so_los(struct usock *up,int protocol);
int so_lod(struct usock *up,int protocol);
int so_lo_recv(struct usock *up,struct mbuf **bpp,struct ksockaddr *from,
	int *fromlen);
int so_los_send(struct usock *up,struct mbuf **bp,struct ksockaddr *to);
int so_lod_send(struct usock *up,struct mbuf **bp,struct ksockaddr *to);
int so_lod_qlen(struct usock *up,int rtx);
int so_los_qlen(struct usock *up,int rtx);
int so_loc_shut(struct usock *up,int how);
int so_loc_close(struct usock *up);
char *lopsocket(struct ksockaddr *p);
int so_loc_stat(struct usock *up);

/* In nrsocket.c: */
int so_n3_sock(struct usock *up,int protocol);
int so_n4_sock(struct usock *up,int protocol);
int so_n4_listen(struct usock *up,int backlog);
int so_n3_conn(struct usock *up);
int so_n4_conn(struct usock *up);
int so_n3_recv(struct usock *up,struct mbuf **bpp,struct ksockaddr *from,
	int *fromlen);
int so_n4_recv(struct usock *up,struct mbuf **bpp,struct ksockaddr *from,
	int *fromlen);
int so_n3_send(struct usock *up,struct mbuf **bp,struct ksockaddr *to);
int so_n4_send(struct usock *up,struct mbuf **bp,struct ksockaddr *to);
int so_n3_qlen(struct usock *up,int rtx);
int so_n4_qlen(struct usock *up,int rtx);
int so_n4_kick(struct usock *up);
int so_n4_shut(struct usock *up,int how);
int so_n3_close(struct usock *up);
int so_n4_close(struct usock *up);
int checknraddr(struct ksockaddr *name,int namelen);
char *nrpsocket(struct ksockaddr *p);
char *nrstate(struct usock *up);
int so_n4_stat(struct usock *up);

/* In tcpsock.c: */
int so_tcp(struct usock *up,int protocol);
int so_tcp_listen(struct usock *up,int backlog);
int so_tcp_conn(struct usock *up);
int so_tcp_recv(struct usock *up,struct mbuf **bpp,struct ksockaddr *from,
	int *fromlen);
int so_tcp_send(struct usock *up,struct mbuf **bp,struct ksockaddr *to);
int so_tcp_qlen(struct usock *up,int rtx);
int so_tcp_kick(struct usock *up);
int so_tcp_shut(struct usock *up,int how);
int so_tcp_close(struct usock *up);
char *tcpstate(struct usock *up);
int so_tcp_stat(struct usock *up);

/* In udpsocket.c: */
int so_udp(struct usock *up,int protocol);
int so_udp_bind(struct usock *up);
int so_udp_conn(struct usock *up);
int so_udp_recv(struct usock *up,struct mbuf **bpp,struct ksockaddr *from,
	int *fromlen);
int so_udp_send(struct usock *up,struct mbuf **bp,struct ksockaddr *to);
int so_udp_qlen(struct usock *up,int rtx);
int so_udp_shut(struct usock *up,int how);
int so_udp_close(struct usock *up);
int so_udp_stat(struct usock *up);

#endif /* _KA9Q_USOCK_H */
