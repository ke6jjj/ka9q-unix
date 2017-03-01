#include "top.h"

#include "errno.h"
#include "global.h"
#include "mbuf.h"
#include "ip.h"
#include "usock.h"
#include "socket.h"

char Inet_eol[] = "\r\n";

static void rip_recv(struct raw_ip *rp);
static void autobind(struct usock *up);

int
so_ip_sock(up,protocol)
struct usock *up;
int protocol;
{
	int s;

	s = up->index;
	up->cb.rip = raw_ip(protocol,rip_recv);
	up->cb.rip->user = s;
	return 0;
}
int
so_ip_conn(up)
struct usock *up;
{
	if(up->name == NULL)
		autobind(up);
	return 0;
}
int
so_ip_recv(up,bpp,from,fromlen)
struct usock *up;
struct mbuf **bpp;
struct ksockaddr *from;
int *fromlen;
{
	struct raw_ip *rip;
	struct ksockaddr_in *remote;
	struct ip ip;
	int cnt;

	while((rip = up->cb.rip) != NULL && rip->rcvq == NULL){
		if(up->noblock){
			kerrno = kEWOULDBLOCK;
			return -1;
		} else if((kerrno = kwait(up)) != 0){
			return -1;
		}
	}
	if(rip == NULL){
		/* Connection went away */
		kerrno = kENOTCONN;
		return -1;
	}
	*bpp = dequeue(&rip->rcvq);
	ntohip(&ip,bpp);

	cnt = len_p(*bpp);
	if(from != NULL && fromlen != (int *)NULL && *fromlen >= SOCKSIZE){
		remote = (struct ksockaddr_in *)from;
		remote->sin_family = kAF_INET;
		remote->sin_addr.s_addr = ip.source;
		remote->sin_port = 0;
		*fromlen = SOCKSIZE;
	}
	return cnt;
}
int
so_ip_send(
struct usock *up,
struct mbuf **bpp,
struct ksockaddr *to
){
	struct ksockaddr_in *local,*remote;

	if(up->name == NULL)
		autobind(up);
	local = (struct ksockaddr_in *)up->name;
	if(to != NULL){
		remote = (struct ksockaddr_in *)to;
	} else if(up->peername != NULL) {
		remote = (struct ksockaddr_in *)up->peername;
	} else {
		free_p(bpp);
		kerrno = kENOTCONN;
		return -1;
	}	
	ip_send(local->sin_addr.s_addr,remote->sin_addr.s_addr,
		(char)up->cb.rip->protocol,0,0,bpp,0,0,0);
	return 0;
}
int
so_ip_qlen(up,rtx)
struct usock *up;
int rtx;
{
	int len;

	switch(rtx){	
	case 0:
		len = len_q(up->cb.rip->rcvq);
		break;
	case 1:
		len = 0;		
		break;
	}
	return len;
}
int
so_ip_close(up)
struct usock *up;
{
	del_ip(up->cb.rip);
	return 0;
}
int
checkipaddr(name,namelen)
struct ksockaddr *name;
int namelen;
{
	struct ksockaddr_in *sock;

	sock = (struct ksockaddr_in *)name;
	if(sock->sin_family != kAF_INET || namelen != sizeof(struct ksockaddr_in))
		return -1;
	return 0;
}

/* Raw IP receive upcall routine */
static void
rip_recv(rp)
struct raw_ip *rp;
{
	ksignal(itop(rp->user),1);
	kwait(NULL);
}
/* Issue an automatic bind of a local address */
static void
autobind(up)
struct usock *up;
{
	struct ksockaddr_in local;
	int s;

	s = up->index;
	local.sin_family = kAF_INET;
	local.sin_addr.s_addr = kINADDR_ANY;
	local.sin_port = Lport++;
	kbind(s,(struct ksockaddr *)&local,sizeof(struct ksockaddr_in));
}
char *
ippsocket(p)
struct ksockaddr *p;
{
	struct ksockaddr_in *sp;
	struct ksocket socket;
	static char buf[30];

	sp = (struct ksockaddr_in *)p;
	socket.address = sp->sin_addr.s_addr;
	socket.port = sp->sin_port;
	strcpy(buf,pinet(&socket));

	return buf;
}
