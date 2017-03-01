/* Internet TTY "link" (keyboard chat) server
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "stdio.h"
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "telnet.h"
#include "session.h"
#include "proc.h"
#include "tty.h"
#include "mailbox.h"
#include "commands.h"

static int Sttylink = -1;	/* Protoype ksocket for service */

int
ttylstart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ksockaddr_in lsocket;
	int s,type;
	kFILE *network;

	if(Sttylink != -1){
		return 0;
	}
	ksignal(Curproc,0);	/* Don't keep the parser waiting */
	chname(Curproc,"TTYlink listener");

	lsocket.sin_family = kAF_INET;
	lsocket.sin_addr.s_addr = kINADDR_ANY;
	if(argc < 2)
		lsocket.sin_port = IPPORT_TTYLINK;
	else
		lsocket.sin_port = atoi(argv[1]);

	Sttylink = ksocket(kAF_INET,kSOCK_STREAM,0);
	kbind(Sttylink,(struct ksockaddr *)&lsocket,sizeof(lsocket));
	klisten(Sttylink,1);
	for(;;){
		if((s = kaccept(Sttylink,NULL,(int *)NULL)) == -1)
			break;	/* Service is shutting down */
		
		network = kfdopen(s,"r+t");
		if(availmem() != 0){
			kfprintf(network,"System is overloaded; try again later\n");
			kfclose(network);
		} else {
			type = TELNET;
			newproc("chat",2048,ttylhandle,s,
			 (void *)&type,(void *)network,0);
		}
	}
	return 0;
}
/* This function handles all incoming "chat" sessions, be they TCP,
 * NET/ROM or AX.25
 */
void
ttylhandle(s,t,p)
int s;
void *t;
void *p;
{
	int type;
	struct session *sp;
	struct ksockaddr addr;
	int len = MAXSOCKSIZE;
	struct telnet tn;
	kFILE *network;
	char *tmp;

	type = * (int *)t;
	network = (kFILE *)p;
	sockowner(kfileno(network),Curproc);	/* We own it now */
	kgetpeername(kfileno(network),&addr,&len);
	logmsg(kfileno(network),"open %s",Sestypes[type]);
	tmp = malloc(kBUFSIZ);
	sprintf(tmp,"ttylink %s",psocket(&addr));

	/* Allocate a session descriptor */
	if((sp = newsession(tmp,type,1)) == NULL){
		kfprintf(network,"Too many sessions\n");
		kfclose(network);
		free(tmp);
		return;
	}
	free(tmp);
	/* Initialize a Telnet protocol descriptor */
	memset(&tn,0,sizeof(tn));
	tn.session = sp;	/* Upward pointer */
	sp->cb.telnet = &tn;	/* Downward pointer */
	sp->network = network;
	sp->proc = Curproc;
	ksetvbuf(sp->network,NULL,_kIOLBF,kBUFSIZ);

	kprintf("\007Incoming %s session %u from %s\007\n",
	 Sestypes[type],sp->index,psocket(&addr));

	tnrecv(&tn);
}

/* Shut down Ttylink server */
int
ttyl0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	close_s(Sttylink);
	Sttylink = -1;
	return 0;
}
