#include "top.h"

#include "lib/std/errno.h"
#include "global.h"
#include "net/core/mbuf.h"
#include "core/socket.h"
#include "core/usock.h"

#include "net/ax25/lapb.h"
#include "net/ax25/ax25.h"

char Ax25_eol[] = "\r";

static void autobind(struct usock *up);

/* The following two variables are needed because there can be only one
 * socket listening on each of the AX.25 modes (I and UI)
 */
int Axi_sock = -1;	/* Socket number listening for AX25 connections */
static int Axui_sock = -1;	/* Socket number listening for AX25 UI frames */
static struct mbuf *Bcq;	/* Queue of incoming UI frames */

/* Function that handles incoming UI frames from lapb.c */
void
beac_input(
struct iface *iface,
uint8 *src,
struct mbuf **bpp
){
	struct mbuf *hdr;
	struct ksockaddr_ax *sax;

	if(Axui_sock == -1){
		/* Nobody there to read it */
		free_p(bpp);
	} else {
		pushdown(&hdr,NULL,sizeof(struct ksockaddr_ax));
		sax = (struct ksockaddr_ax *)hdr->data;
		sax->sax_family = kAF_AX25;
		memcpy(sax->ax25_addr,src,AXALEN);
		strncpy(sax->iface,iface->name,ILEN);
		hdr->next = (*bpp);
		*bpp = NULL;
		enqueue(&Bcq,&hdr);
	}
}
int
so_ax_sock(
struct usock *up,
int protocol
){
	return 0;
}
int
so_axui_sock(
struct usock *up,
int protocol
){
	return 0;
}

int
so_axui_bind(
struct usock *up
){
	if(Axui_sock != -1){
		kerrno = kEADDRINUSE;
		return -1;
	}
	Axui_sock = up->index;
	return 0;
}
int
so_ax_listen(
struct usock *up,
int backlog
){
	struct ksockaddr_ax *local;

	if(up->name == NULL)
		autobind(up);
	if(up != itop(Axi_sock)){
		kerrno = kEOPNOTSUPP;
		return -1;
	}
	local = (struct ksockaddr_ax *)up->name;
	up->cb.ax25 = open_ax25(NULL,local->ax25_addr,NULL,
	 backlog ? AX_SERVER:AX_PASSIVE,0,
	 s_arcall,s_atcall,s_ascall,Axi_sock);
	return 0;
}
int
so_ax_conn(
struct usock *up
){
	struct ksockaddr_ax *local,*remote,localtmp;
	struct ax25_cb *ax25;
	struct iface *iface;
	int s;

	s = up->index;
	remote = (struct ksockaddr_ax *)up->peername;
	if((iface = if_lookup(remote->iface)) == NULL){
		kerrno = kEINVAL;
		return -1;
	}
	local = (struct ksockaddr_ax *)up->name;
	if(local == NULL){
		/* The local address was unspecified; set it from
		 * the interface we'll use
		 */
		localtmp.sax_family = kAF_AX25;
		memcpy(localtmp.ax25_addr,iface->hwaddr,AXALEN);
		memcpy(localtmp.iface,remote->iface,ILEN);
		kbind(s,(struct ksockaddr *)&localtmp,sizeof(localtmp));
		local = (struct ksockaddr_ax *)up->name;
	}
	/* If we already have an AX25 link we can use it */
	if((up->cb.ax25 = find_ax25(remote->ax25_addr)) != NULL
	   && up->cb.ax25->state != LAPB_DISCONNECTED &&
	   up->cb.ax25->user == -1) {
		up->cb.ax25->user = s;
		up->cb.ax25->r_upcall = s_arcall;
		up->cb.ax25->t_upcall = s_atcall;
		up->cb.ax25->s_upcall = s_ascall;
		if(up->cb.ax25->state == LAPB_CONNECTED
		   || up->cb.ax25->state == LAPB_RECOVERY)
		    	return 0;
	} else {
		up->cb.ax25 = open_ax25(iface,local->ax25_addr,
		 remote->ax25_addr,AX_ACTIVE,
		 Axwindow,s_arcall,s_atcall,s_ascall,s);
	}
	/* Wait for the connection to complete */
	while((ax25 = up->cb.ax25) != NULL && ax25->state != LAPB_CONNECTED){
		if(up->noblock){
			kerrno = kEWOULDBLOCK;
			return -1;
		} else if((kerrno = kwait(up)) != 0){
			return -1;
		}
	}
	if(ax25 == NULL){
		/* Connection probably already exists */
		free(up->peername);
		up->peername = NULL;
		kerrno = kECONNREFUSED;
		return -1;
	}
	return 0;
}
int
so_axui_conn(
struct usock *up
){
	if(up->name == NULL)
		autobind(up);
	return 0;
}

int
so_ax_recv(
struct usock *up,
struct mbuf **bpp,
struct ksockaddr *from,
int *fromlen
){
	struct ax25_cb *ax25;
	int cnt;
	
	while((ax25 = up->cb.ax25) != NULL
	 && (*bpp = recv_ax25(ax25,0)) == NULL){
		if(up->noblock){
			kerrno = kEWOULDBLOCK;
			return -1;
		} else if((kerrno = kwait(up)) != 0){
			return -1;
		}
	}
	if(ax25 == NULL){
		/* Connection went away */
		kerrno = kENOTCONN;
		return -1;
	}
	cnt = (*bpp)->cnt;
	return cnt;
}
int
so_axui_recv(
struct usock *up,
struct mbuf **bpp,
struct ksockaddr *from,
int *fromlen
){
	int s;

	s = up->index;

	while(s == Axui_sock && Bcq == NULL){
		if(up->noblock){
			kerrno = kEWOULDBLOCK;
			return -1;
		} else if((kerrno = kwait(&Bcq)) != 0){
			return -1;
		}
	}
	if(s != Axui_sock){
		kerrno = kENOTCONN;
		return -1;
	}
	*bpp = dequeue(&Bcq);

	if(from != NULL && fromlen != NULL
	   && *fromlen >= sizeof(struct ksockaddr_ax)){
		pullup(bpp,from,sizeof(struct ksockaddr_ax));
		*fromlen = sizeof(struct ksockaddr_ax);
	} else {
		pullup(bpp,NULL,sizeof(struct ksockaddr_ax));
	}
	return len_p(*bpp);
}
int	
so_ax_send(
struct usock *up,
struct mbuf **bpp,
struct ksockaddr *to
){
	struct ax25_cb *ax25;

	if((ax25 = up->cb.ax25) == NULL){
		free_p(bpp);
		kerrno = kENOTCONN;
		return -1;
	}
	send_ax25(ax25,bpp,PID_NO_L3);

	while((ax25 = up->cb.ax25) != NULL &&
	 len_q(ax25->txq) * ax25->paclen > ax25->window){
		if(up->noblock){
			kerrno = kEWOULDBLOCK;
			return -1;
		} else if((kerrno = kwait(up)) != 0){
			return -1;
		}
	}
	if(ax25 == NULL){
		kerrno = kEBADF;
		return -1;
	}
	return 0;
}
int	
so_axui_send(
struct usock *up,
struct mbuf **bpp,
struct ksockaddr *to
){
	struct ksockaddr_ax *local,*remote;

	local = (struct ksockaddr_ax *)up->name;
	if(to != NULL)
		remote = (struct ksockaddr_ax *)to;
	else if(up->peername != NULL){
		remote = (struct ksockaddr_ax *)up->peername;
	} else {
		free_p(bpp);
		kerrno = kENOTCONN;
		return -1;
	}
	ax_output(if_lookup(remote->iface),remote->ax25_addr,
	  local->ax25_addr,PID_NO_L3,bpp);
	return 0;
}

int
so_ax_qlen(
struct usock *up,
int rtx
){
	int len;

	if(up->cb.ax25 == NULL){
		kerrno = kENOTCONN;
		return -1;
	}
	switch(rtx){
	case 0:
		len = len_p(up->cb.ax25->rxq);
		break;
	case 1:	/* Number of packets, not bytes */
		len = len_q(up->cb.ax25->txq);
	}
	return len;
}
int
so_axui_qlen(
struct usock *up,
int rtx
){
	int len;

	switch(rtx){	
	case 0:
		len = len_q(Bcq);
		break;
	case 1:
		len = 0;		
		break;
	}
	return len;
}
int
so_ax_kick(
struct usock *up
){
	if(up->cb.ax25 != NULL)
		kick_ax25(up->cb.ax25);
	return 0;
}
int
so_ax_shut(
struct usock *up,
int how
){
	if(up->cb.ax25 == NULL)
		return 0;
	switch(how){
	case 0:
	case 1:	/* Attempt regular disconnect */
		disc_ax25(up->cb.ax25);
		break;
	case 2: /* Blow it away */
		reset_ax25(up->cb.ax25);
		up->cb.ax25 = NULL;
		break;
	}
	return 0;	
}
int
so_ax_close(
struct usock *up
){
	if(up->cb.ax25 != NULL){
		/* Tell the CLOSED upcall there's no more socket */
		up->cb.ax25->user = -1;
		disc_ax25(up->cb.ax25);
	}
	return 0;
}
int
so_axui_close(
struct usock *up
){
	Axui_sock = -1;
	free_q(&Bcq);
	ksignal(&Bcq,0);	/* Unblock any reads */
	return 0;
}
/* AX.25 receive upcall */
void
s_arcall(
struct ax25_cb *axp,
int cnt
){
	int ns;
	struct usock *up,*nup,*oup;
	union sp sp;

	up = itop(axp->user);
	/* When AX.25 data arrives for the first time the AX.25 listener
	   is notified, if active. If the AX.25 listener is a server its
	   socket is duplicated in the same manner as in s_tscall().
	 */
	if (Axi_sock != -1 && axp->user == -1) {
		oup = up = itop(Axi_sock);
		/* From now on, use the same upcalls as the listener */
		axp->t_upcall = up->cb.ax25->t_upcall;
		axp->r_upcall = up->cb.ax25->r_upcall;
		axp->s_upcall = up->cb.ax25->s_upcall;
		if (up->cb.ax25->flags.clone) {
			/* Clone the socket */
			ns = ksocket(kAF_AX25,kSOCK_STREAM,0);
			nup = itop(ns);
			ASSIGN(*nup,*up);
			axp->user = ns;
			nup->cb.ax25 = axp;
			/* Allocate new memory for the name areas */
			nup->name = mallocw(sizeof(struct ksockaddr_ax));
			nup->peername = mallocw(sizeof(struct ksockaddr_ax));
			nup->index = ns;
			/* Store the new socket # in the old one */
			up->rdysock = ns;
			up = nup;
		} else {
			axp->user = Axi_sock;
			del_ax25(up->cb.ax25);
			up->cb.ax25 = axp;
			/* Allocate space for the peer's name */
			up->peername = mallocw(sizeof(struct ksockaddr_ax));
			/* Store the old socket # in the old socket */
			up->rdysock = Axi_sock;
		}
		/* Load the addresses. Memory for the name has already
		 * been allocated, either above or in the original bind.
		 */
		sp.ax = (struct ksockaddr_ax *)up->name;
		sp.ax->sax_family = kAF_AX25;
		memcpy(sp.ax->ax25_addr,axp->local,AXALEN);
		memcpy(sp.ax->iface,axp->iface->name,ILEN);
		up->namelen = sizeof(struct ksockaddr_ax);

		sp.ax = (struct ksockaddr_ax *)up->peername;
		sp.ax->sax_family = kAF_AX25;
		memcpy(sp.ax->ax25_addr,axp->remote,AXALEN);
		memcpy(sp.ax->iface,axp->iface->name,ILEN);
		up->peernamelen = sizeof(struct ksockaddr_ax);
		/* Wake up the guy accepting it, and let him run */
		ksignal(oup,1);
		kwait(NULL);
		return;
	}
	/* Wake up anyone waiting, and let them run */
	ksignal(up,1);
	kwait(NULL);
}
/* AX.25 transmit upcall */
void
s_atcall(
struct ax25_cb *axp,
int cnt
){
	/* Wake up anyone waiting, and let them run */
	ksignal(itop(axp->user),1);
	kwait(NULL);
}
/* AX25 state change upcall routine */
void
s_ascall(
struct ax25_cb *axp,
int old,
int new
){
	int s;
	struct usock *up;

	s = axp->user;
	up = itop(s);

	switch(new){
	case LAPB_DISCONNECTED:
		/* Clean up. If the user has already closed the socket,
		 * then up will be null (s was set to -1 by the close routine).
		 * If not, then this is an abnormal close (e.g., a reset)
		 * and clearing out the pointer in the socket structure will
		 * prevent any further operations on what will be a freed
		 * control block. Also wake up anybody waiting on events
		 * related to this block so they will notice it disappearing.
		 */
		if(up != NULL){
			up->errcodes[0] = axp->reason;
			up->cb.ax25 = NULL;
		}
		del_ax25(axp);
		break;
	default:	/* Other transitions are ignored */
		break;
	}
	ksignal(up,0);	/* In case anybody's waiting */
}

/* Issue an automatic bind of a local AX25 address */
static void
autobind(
struct usock *up
){
	struct ksockaddr_ax local;
	int s;

	s = up->index;
	local.sax_family = kAF_AX25;
	memcpy(local.ax25_addr,Mycall,AXALEN);
	kbind(s,(struct ksockaddr *)&local,sizeof(struct ksockaddr_ax));
}
int
checkaxaddr(
struct ksockaddr *name,
int namelen
){
	struct ksockaddr_ax *sock;

	sock = (struct ksockaddr_ax *)name;
	if(sock->sax_family != kAF_AX25 || namelen != sizeof(struct ksockaddr_ax))
		return -1;
	return 0;
}
char *
axpsocket(
struct ksockaddr *p
){
	struct ksockaddr_ax *axp;
	static char buf[30];
	char tmp[11];

	axp = (struct ksockaddr_ax *)p;
	pax25(tmp,axp->ax25_addr);
	if(strlen(axp->iface) != 0)
		sprintf(buf,"%s on %s",tmp,axp->iface);
	else
		strcpy(buf,tmp);
	return buf;
}
char *
axstate(
struct usock *up
){
	return Ax25states[up->cb.ax25->state];	
}
int
so_ax_stat(
struct usock *up
){
	st_ax25(up->cb.ax25);
	return 0;
}
