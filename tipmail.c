/* "Dumb terminal" mailbox interface
 * Copyright 1991 Phil Karn, KA9Q
 *
 *	May '91	Bill Simpson
 *		move to separate file for compilation & linking
 *	Sep '91 Bill Simpson
 *		minor changes for DTR & RLSD
 */
#include "top.h"

#include "lib/std/errno.h"
#include "global.h"
#include "net/core/mbuf.h"
#include "core/timer.h"
#include "core/proc.h"
#include "net/core/iface.h"
#ifdef UNIX
#include "unix/asy_unix.h"
#else
#include "msdos/n8250.h"
#endif
#include "core/asy.h"
#include "core/socket.h"
#include "core/usock.h"
#include "telnet.h"
#include "mailbox.h"
#include "tipmail.h"
#include "core/devparam.h"

static struct tipcb {
	struct tipcb *next;
	struct proc *proc;
	struct proc *in;
	struct iface *iface;
	int (*rawsave)(struct iface *,struct mbuf **);
	kFILE *network;
	int echo;
	struct timer timer;
} *Tiplist;
#define	NULLTIP	(struct tipcb *)0

static void tip_in(int dev,void *n1,void *n2);
static void tipidle(void *t);

unsigned Tiptimeout = 180;	/* Default tip inactivity timeout (seconds) */

/* Input process */
static void
tip_in(dev,n1,n2)
int dev;
void *n1,*n2;
{
	struct tipcb *tip;
	struct mbuf *bp;
	char *buf[2], line[MBXLINE];
	int c, ret, pos = 0;

	tip = (struct tipcb *) n1;
	while((c = get_asy(dev)) != -1){
		Asy[dev].iface->lastrecv = secclock();
		ret = 0;
		if(tip->echo == WONT){
			switch(c){
			case 18:	/* CTRL-R */
				bp = NULL;
				pushdown(&bp,line,pos);
				pushdown(&bp,"^R\r\n",4);
				ret = 1;
				break;
			case 0x7f:	/* DEL */
			case '\b':
				bp = NULL;
				if(pos){
					--pos;
					bp = qdata("\b \b",3);
				}
				ret = 1;
				break;
			case '\r':
				c = '\n';	/* CR => NL */
			case '\n':
				bp = qdata("\r\n",2);
				break;
			default:
				bp = NULL;
				pushdown(&bp,NULL,1);
				*bp->data = c;
				break;
			}
			asy_send(dev,&bp);
			tip->iface->lastsent = secclock();
			if(ret)
				continue;
		}
		line[pos++] = c;
		if(pos == MBXLINE - 1 || tip->echo == WILL
		  || c == '\n'){
			line[pos] = '\0';
			pos = 0;
			kfputs(line,tip->network);
			kfflush(tip->network);
		}
	}
	/* get_asy() failed, terminate */
	kfclose(tip->network);
	tip->in = tip->proc;
	tip->proc = Curproc;
	buf[1] = Asy[dev].iface->name;
	tip0(2,buf,NULL);
}
/* Start mailbox on serial line */
int
tipstart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp;
	register struct asy *ap;
	struct tipcb *tip;
	struct mbuf *bp;
	char *buf[2];
	int dev, c, cmd, s[2], type = TIP;

	if((ifp = if_lookup(argv[1])) == NULL){
		kprintf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	for(dev=0,ap = Asy;dev < ASY_MAX;dev++,ap++)
		if(ap->iface == ifp)
			break;
	if(dev == ASY_MAX){
		kprintf("Interface %s not asy port\n",argv[1]);
		return 1;
	}
	if(ifp->raw == bitbucket){
		kprintf("Tip session already active on %s\n",argv[1]);
		return 1;
	}
	ksignal(Curproc,0);	/* Don't keep the parser waiting */
	chname(Curproc,"Mbox tip");
	tip = (struct tipcb *) callocw(1,sizeof(struct tipcb));

	/* Save output handler and temporarily redirect output to null */
	tip->rawsave = ifp->raw;
	ifp->raw = bitbucket;
	tip->iface = ifp;
	tip->proc = Curproc;
	tip->timer.func = tipidle;
	tip->timer.arg = (void *) tip;
	tip->next = Tiplist;
	Tiplist = tip;
	buf[1] = ifp->name;

	/* Suspend packet input drivers */
	suspend(ifp->rxproc);

	for(;;) {
#ifndef UNIX
		/* Wait for DCD to be asserted */
		get_rlsd_asy(dev,1);
#endif

		if(socketpair(kAF_LOCAL,kSOCK_STREAM,0,s) == -1){
			kprintf("Could not create socket pair, errno %d\n",kerrno);
			tip0(2,buf,p);
			return 1;
		}
		tip->echo = WONT;
		tip->network = kfdopen(s[0],"r+t");
		newproc("mbx_incom",2048,mbx_incom,s[1],(void *)type,NULL,0);
		set_timer(&tip->timer,Tiptimeout*1000);
		start_timer(&tip->timer);

		/* Now fork into two paths, one rx, one tx */
		tip->in = newproc("Mbox tip in",
				256,tip_in,dev,(void *)tip,NULL,0);
		while((c = kgetc(tip->network)) != -1) {
			if(c == IAC){	/* ignore most telnet options */
				if((cmd = kgetc(tip->network)) == -1)
					break;
				if(cmd > 250 && cmd < 255) {
					if((c = kgetc(tip->network)) == -1)
						break;
					switch(cmd){
					case WILL:
						if(c == TN_ECHO) {
							tip->echo = cmd;
							cmd = DO;
						}
						else
							cmd = DONT;
						break;
					case WONT:
						if(c == TN_ECHO)
							tip->echo = cmd;
						cmd = DONT;
						break;
					case DO:
					case DONT:
						cmd = WONT;
						break;
					}
					kfprintf(tip->network,"%c%c%c",IAC,cmd,c);
					kfflush(tip->network);
				}
				continue;
			}
			if(c == '\n')
				bp = qdata("\r\n",2);
			else {
				bp = NULL;
				pushdown(&bp,NULL,1);
				*bp->data = c;
			}
			asy_send(dev,&bp);
			ifp->lastsent = secclock();
		}
		kfclose(tip->network);
		killproc(&tip->in);
		kwait(itop(s[1])); /* let mailbox terminate, if necessary */
		stop_timer(&tip->timer);

		/* Tell line to go down */
		ifp->ioctl(ifp,PARAM_DOWN,TRUE,0L);

#ifndef UNIX
		/* Wait for DCD to be dropped */
		get_rlsd_asy(dev,0);
#endif
	}
}
int
tip0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp;
	struct tipcb *tip, *prev = NULLTIP;
	struct proc *proc;

	if((ifp = if_lookup(argv[1])) == NULL){
		kprintf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	for(tip = Tiplist; tip != NULLTIP; prev = tip, tip = tip->next)
		if(tip->iface == ifp) {
			if(prev != NULLTIP)
				prev->next = tip->next;
			else
				Tiplist = tip->next;
			proc = tip->proc;
			kfclose(tip->network);
			ifp->raw = tip->rawsave;
			resume(ifp->rxproc);
			stop_timer(&tip->timer);
			killproc(&tip->in);
			free(tip);
			killproc(&proc);
			return 0;
		}
	return 0;
}
static void
tipidle(t)
void *t;
{
	struct tipcb *tip;
	static char *msg = "You have been idle too long. Please hang up.\r\n";
	struct mbuf *bp;
	tip = (struct tipcb *) t;
	if(secclock() - tip->iface->lastrecv < Tiptimeout){
		set_timer(&tip->timer,(Tiptimeout-secclock() *
		 tip->iface->lastrecv)*1000);
		start_timer(&tip->timer);
		return;
	}
	bp = qdata(msg,strlen(msg));
	asy_send(tip->iface->dev,&bp);
	tip->iface->lastsent = secclock();
	kfclose(tip->network);
}

static int Stelnet = -1;

/* Start up Telnet server */
int
telnet1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ksockaddr_in lsocket;
	int s;
	int type;

	if(Stelnet != -1){
		return 0;
	}
	ksignal(Curproc,0); 	/* Don't keep the parser waiting */
	chname(Curproc,"Telnet listener");

	lsocket.sin_family = kAF_INET;
	lsocket.sin_addr.s_addr = kINADDR_ANY;
	if(argc < 2)
		lsocket.sin_port = IPPORT_TELNET;
	else
		lsocket.sin_port = atoi(argv[1]);
	Stelnet = ksocket(kAF_INET,kSOCK_STREAM,0);
	kbind(Stelnet,(struct ksockaddr *)&lsocket,sizeof(lsocket));
	klisten(Stelnet,1);
	for(;;){
		if((s = kaccept(Stelnet,NULL,(int *)NULL)) == -1)
			break;	/* Service is shutting down */

		if(availmem() != 0){
			kshutdown(s,1);
		} else {
			/* Spawn a server */
			type = TELNET;
			newproc("mbox",2048,mbx_incom,s,(void *)type,NULL,0);
		}
	}
	return 0;
}
/* Stop telnet server */
int
telnet0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	close_s(Stelnet);
	Stelnet = -1;
	return 0;
}

