#ifndef	_SOCKET_H
#define	_SOCKET_H

#include <stdarg.h>

#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "sockaddr.h"

/* Local IP wildcard address */
#define	kINADDR_ANY	0x0L

/* IP protocol numbers */
/* now in internet.h */

/* TCP port numbers */
#define	IPPORT_ECHO	7	/* Echo data port */
#define	IPPORT_DISCARD	9	/* Discard data port */
#define	IPPORT_FTPD	20	/* FTP Data port */
#define	IPPORT_FTP	21	/* FTP Control port */
#define IPPORT_TELNET	23	/* Telnet port */
#define IPPORT_SMTP	25	/* Mail port */
#define	IPPORT_MTP	57	/* Secondary telnet protocol */
#define	IPPORT_FINGER	79	/* Finger port */
#define	IPPORT_TTYLINK	87	/* Chat port */
#define IPPORT_POP	109	/* pop2 port */
#define	IPPORT_NNTP	119	/* Netnews port */
#define	IPPORT_LOGIN	513	/* BSD rlogin port */
#define	IPPORT_TERM	5000	/* Serial interface server port */

/* UDP port numbers */
#define	IPPORT_DOMAIN	53
#define	IPPORT_BOOTPS	67
#define	IPPORT_BOOTPC	68
#define	IPPORT_PHOTURIS	468	/* Photuris Key management */
#define	IPPORT_RIP	520
#define	IPPORT_REMOTE	1234	/* Pulled out of the air */
#define	IPPORT_BSR	5000	/* BSR X10 interface server port (UDP) */

#define	kAF_INET		0
#define	kAF_AX25		1
#define kAF_NETROM	2
#define	kAF_LOCAL	3
#define	NAF		4

#define	kSOCK_STREAM	0
#define	kSOCK_DGRAM	1
#define	kSOCK_RAW	2
#define kSOCK_SEQPACKET	3

extern char *Sock_errlist[];

/* In socket.c: */
extern int Axi_sock;	/* Socket listening to AX25 (there can be only one) */

int kaccept(int s,struct ksockaddr *peername,int *peernamelen);
int kbind(int s,struct ksockaddr *name,int namelen);
int close_s(int s);
int kconnect(int s,struct ksockaddr *peername,int peernamelen);
char *eolseq(int s);
void freesock(struct proc *pp);
int kgetpeername(int s,struct ksockaddr *peername,int *peernamelen);
int getsockname(int s,struct ksockaddr *name,int *namelen);
int klisten(int s,int backlog);
int recv_mbuf(int s,struct mbuf **bpp,int flags,struct ksockaddr *from,int *fromlen);
int send_mbuf(int s,struct mbuf **bp,int flags,struct ksockaddr *to,int tolen);
int settos(int s,int tos);
int kshutdown(int s,int how);
int ksocket(int af,int type,int protocol);
void sockinit(void);
int sockkick(int s);
int socklen(int s,int rtx);
struct proc *sockowner(int s,struct proc *newowner);
int usesock(int s);
int socketpair(int af,int type,int protocol,int sv[]);

/* In sockuser.c: */
void flushsocks(void);
int krecv(int s,void *buf,int len,int flags);
int krecvfrom(int s,void *buf,int len,int flags,struct ksockaddr *from,int *fromlen);
int ksend(int s,const void *buf,int len,int flags);
int ksendto(int s,void *buf,int len,int flags,struct ksockaddr *to,int tolen);

/* In file sockutil.c: */
char *psocket(void *p);
char *sockerr(int s);
char *sockstate(int s);

/* In file tcpsock.c: */
int start_tcp(uint port,char *name,void (*task)(),int stack);
int stop_tcp(uint port);

#endif	/* _SOCKET_H */
