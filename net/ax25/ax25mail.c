/* AX25 mailbox interface
 * Copyright 1991 Phil Karn, KA9Q
 *
 *	May '91	Bill Simpson
 *		move to separate file for compilation & linking
 */
#include "../../top.h"

#include "../../global.h"
#include "../../proc.h"
#include "../../socket.h"
#include "../../session.h"
#include "../../mailbox.h"

#include "ax25.h"
#include "ax25mail.h"

/* Axi_sock is kept in Socket.c, so that this module won't be called */

int
ax25start(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int s,type,c;
	kFILE *network;

	if (Axi_sock != -1)
		return 0;

	ksignal(Curproc,0);	/* Don't keep the parser waiting */
	chname(Curproc,"AX25 listener");
	Axi_sock = ksocket(kAF_AX25,kSOCK_STREAM,0);
	/* bind() is done automatically */
	if(klisten(Axi_sock,1) == -1){
		close_s(Axi_sock);
		Axi_sock = -1;
		return -1;
	}
	for(;;){
		if((s = kaccept(Axi_sock,NULL,NULL)) == -1)
			break;	/* Service is shutting down */

		type = AX25TNC;
		/* Eat the line that triggered the connection
		 * and then start the mailbox
		 */
		network = kfdopen(s,"r+t");		
		while((c = kgetc(network)) != '\n' && c != kEOF)
			;
		newproc("mbox",2048,mbx_incom,s,(void *)type,(void *)network,0);
	}
	close_s(Axi_sock);
	Axi_sock = -1;
	return 0;
}
int
ax250(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	close_s(Axi_sock);
	Axi_sock = -1;
	return 0;
}


int
dogateway(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	struct ksockaddr_ax fsocket;
	int ndigis,i,s;
	uint8 digis[MAXDIGIS][AXALEN];
	uint8 target[AXALEN];

	m = (struct mbx *)p;
	if(!(m->privs & AX25_CMD)){
		kprintf(Noperm);
		return 0;
	}
	/* If digipeaters are given, put them in the routing table */
	if(argc > 3){
		setcall(target,argv[2]);
		ndigis = argc - 3;
		if(ndigis > MAXDIGIS){
			kprintf("Too many digipeaters\n");
			return 1;
		}
		for(i=0;i<ndigis;i++){
			if(setcall(digis[i],argv[i+3]) == -1){
				kprintf("Bad digipeater %s\n",argv[i+3]);
				return 1;
			}
		}
		if(ax_add(target,kAX_LOCAL,digis,ndigis) == NULL){
			kprintf("Route add failed\n");
			return 1;
		}
	}
	if((s = ksocket(kAF_AX25,kSOCK_STREAM,0)) == -1){
		kprintf(Nosock);
		return 0;
	}
	fsocket.sax_family = kAF_AX25;
	setcall(fsocket.ax25_addr,argv[2]);
	strncpy(fsocket.iface,argv[1],ILEN);
	m->startmsg = mallocw(80);
	sprintf(m->startmsg,"*** LINKED to %s\n",m->name);
	return gw_connect(m,s,(struct ksockaddr *)&fsocket, sizeof(struct ksockaddr_ax));
}
