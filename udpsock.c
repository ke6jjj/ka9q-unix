#include "top.h"

#include "errno.h"
#include "global.h"
#include "udp.h"
#include "socket.h"
#include "usock.h"

static void s_urcall(struct iface *iface,struct udp_cb *udp,int cnt);

int
so_udp(up,protocol)
struct usock *up;
int protocol;
{
	return 0;
}
int
so_udp_bind(up)
struct usock *up;
{
	int s;
	struct ksockaddr_in *sp;
	struct ksocket lsock;

	s = up->index;
	sp = (struct ksockaddr_in *)up->name;
	lsock.address = sp->sin_addr.s_addr;
	lsock.port = sp->sin_port;
	if ((up->cb.udp = open_udp(&lsock,s_urcall)) == NULL) {
		switch (Net_error) {
		case NO_MEM:
			kerrno = kENOMEM;
			break;
		case CON_EXISTS:
			kerrno = kEADDRINUSE;
			break;
		default:
			kerrno = kEOPNOTSUPP;
			break;
		}
		return -1;
	}
	up->cb.udp->user = s;
	return 0;
}
int
so_udp_conn(up)
struct usock *up;
{
	if(up->name == NULL){
		return so_ip_autobind(up);
	}
	return 0;
}
int
so_udp_recv(up,bpp,from,fromlen)
struct usock *up;
struct mbuf **bpp;
struct ksockaddr *from;
int *fromlen;
{
	int cnt;
	struct udp_cb *udp;
	struct ksockaddr_in *remote;
	struct ksocket fsocket;

	while((udp = up->cb.udp) != NULL
	&& (cnt = recv_udp(udp,&fsocket,bpp)) == -1){
		if(up->noblock){
			kerrno = kEWOULDBLOCK;
			return -1;
		} else if((kerrno = kwait(up)) != 0){
			return -1;
		}
	}
	if(udp == NULL){
		/* Connection went away */
		kerrno = kENOTCONN;
		return -1;
	}
	if(from != NULL && fromlen != (int *)NULL && *fromlen >= SOCKSIZE){
		remote = (struct ksockaddr_in *)from;
		remote->sin_family = kAF_INET;
		remote->sin_addr.s_addr = fsocket.address;
		remote->sin_port = fsocket.port;
		*fromlen = SOCKSIZE;
	}
	return cnt;
}
int
so_udp_send(
struct usock *up,
struct mbuf **bpp,
struct ksockaddr *to
){
	struct ksockaddr_in *local,*remote;
	struct ksocket lsock,fsock;

	if(up->name == NULL) {
		if (so_ip_autobind(up) != 0) {
			free_p(bpp);
			return -1;
		}
	}
	local = (struct ksockaddr_in *)up->name;
	lsock.address = local->sin_addr.s_addr;
	lsock.port = local->sin_port;
	if(to != NULL) {
		remote = (struct ksockaddr_in *)to;
	} else if(up->peername != NULL){
		remote = (struct ksockaddr_in *)up->peername;
	} else {
		free_p(bpp);
		kerrno = kENOTCONN;
		return -1;
	}	
	fsock.address = remote->sin_addr.s_addr;
	fsock.port = remote->sin_port;
	send_udp(&lsock,&fsock,up->tos,0,bpp,0,0,0);
	return 0;
}
int
so_udp_qlen(up,rtx)
struct usock *up;
int rtx;
{
	int len;

	switch(rtx){
	case 0:
		len = up->cb.udp->rcvcnt;
		break;
	case 1:
		len = 0;
		break;
	}
	return len;
}
int
so_udp_close(struct usock *up)
{
	if(up->cb.udp != NULL)
		del_udp(&up->cb.udp);

	return 0;
}
int
so_udp_shut(up,how)
struct usock *up;
int how;
{
	int s;

	s = up->index;
	close_s(s);
	return 0;
}
static void
s_urcall(iface,udp,cnt)
struct iface *iface;
struct udp_cb *udp;
int cnt;
{
	ksignal(itop(udp->user),1);
	kwait(NULL);
}

int
so_udp_stat(up)
struct usock *up;
{
	st_udp(up->cb.udp,0);
	return 0;
}
