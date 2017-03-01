/* Miscellaneous Internet servers: discard, echo
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "proc.h"
#include "tcp.h"
#include "commands.h"
#include "hardware.h"
#include "mailbox.h"
#include "asy.h"
#include "n8250.h"
#include "devparam.h"
#include "telnet.h"

static int chkrpass(struct mbuf *bp);
static void discserv(int s,void *unused,void *p);
static void echoserv(int s,void *unused,void *p);
static void termserv(int s,void *unused,void *p);
static void termrx(int s,void *p1,void *p2);
static void tunregister(struct iface *,int);
static void tregister(struct iface *);

struct tserv {
	struct tserv *next;
	struct proc *proc;
	struct iface *ifp;
};
struct tserv *Tserv;

/* Start up TCP discard server */
int
dis1(
int argc,
char *argv[],
void *p
){
	uint port;

	port = (argc < 2) ? IPPORT_DISCARD: atoi(argv[1]);
	return start_tcp(port,"Discard Server",discserv,576);
}
static void
discserv(
int s,
void *unused,
void *p
){
	struct mbuf *bp;

	sockowner(s,Curproc);
	logmsg(s,"open discard");
	if(availmem() == 0){
		while(recv_mbuf(s,&bp,0,NULL,NULL) > 0)
			free_p(&bp);
	}
	logmsg(s,"close discard");
	close_s(s);
}
/* Stop discard server */
int
dis0(
int argc,
char *argv[],
void *p
){
	uint port;

	port = (argc < 2) ? IPPORT_DISCARD : atoi(argv[1]);
	return stop_tcp(port);
}
/* Start up TCP echo server */
int
echo1(
int argc,
char *argv[],
void *p
){
	uint port;

	port = (argc < 2) ? IPPORT_ECHO : atoi(argv[1]);
	return start_tcp(port,"Echo Server",echoserv,512);
}
static void
echoserv(
int s,
void *unused,
void *p
){
	struct mbuf *bp;

	sockowner(s,Curproc);
	logmsg(s,"open echo");
	if(availmem() == 0){
		while(recv_mbuf(s,&bp,0,NULL,NULL) > 0)
			send_mbuf(s,&bp,0,NULL,0);
	}
	logmsg(s,"close echo");
	close_s(s);
}
/* stop echo server */
int
echo0(
int argc,
char *argv[],
void *p
){
	uint port;

	port = (argc < 2) ? IPPORT_ECHO : atoi(argv[1]);
	return stop_tcp(port);
}

/* Fix this to be command from telnet server */
static void
termserv(
int s,
void *unused,
void *p
){
	FILE *network = NULL;
	FILE *asy;
	char *buf = NULL;
	struct iface *ifp;
	struct route *rp;
	struct sockaddr_in fsocket;
	struct proc *rxproc = NULL;
	int i;
	
	sockowner(s,Curproc);
	logmsg(s,"open term");
	network = fdopen(s,"r+");

	if(network == NULL || (buf = malloc(BUFSIZ)) == NULL)
		goto quit;

	if(SETSIG(EABORT)){
		fprintf(network,"Abort\r\n");
		goto quit;
	}
	/* Prompt for desired interface. Verify that it exists, that
	 * we're not using it for our TCP connection, that it's an
	 * asynch port, and that there isn't already another tip, term
	 * or dialer session active on it.
	 */
	for(;;){
		fprintf(network,"Interface: ");
		fgets(buf,BUFSIZ,network);
		rip(buf);
		if((ifp = if_lookup(buf)) == NULL){
			fprintf(network,"Interface %s does not exist\n",buf);
			continue;
		}
		if(getpeername(s,(struct sockaddr *)&fsocket,&i) != -1
		 && !ismyaddr(fsocket.sin_addr.s_addr)
		 && (rp = rt_lookup(fsocket.sin_addr.s_addr)) != NULL
		 && rp->iface == ifp){
			fprintf(network,"You're using interface %s!\n",ifp->name);
			continue;
		}
		if((asy = asyopen(buf,"r+b")) != NULL)
			break;
		fprintf(network,"Can't open interface %s\n",buf);
		fprintf(network,"Try to bounce current user? ");
		fgets(buf,BUFSIZ,network);
		if(buf[0] == 'y' || buf[0] == 'Y'){
			tunregister(ifp,1);
			kwait(NULL);
		}
	}
	setvbuf(asy,NULL,_IONBF,0);
	tregister(ifp);
	fprintf(network,"Wink DTR? ");
	fgets(buf,BUFSIZ,network);
	if(buf[0] == 'y' || buf[0] == 'Y'){
		asy_ioctl(ifp,PARAM_DTR,1,0);	/* drop DTR */
		ppause(1000L);
		asy_ioctl(ifp,PARAM_DTR,1,1);	/* raise DTR */
	}
	fmode(network,STREAM_BINARY);	/* Switch to raw mode */
	setvbuf(network,NULL,_IONBF,0);
	fprintf(network,"Turn off local echo? ");
	fgets(buf,BUFSIZ,network);
	if(buf[0] == 'y' || buf[0] == 'Y'){
		fprintf(network,"%c%c%c",IAC,WILL,TN_ECHO);
		/* Eat the response */
		for(i=0;i<3;i++)
			(void)fgetc(network);
	}
#ifdef	notdef
	FREE(buf);
#endif
	/* Now fork into receive and transmit processes */
	rxproc = newproc("term rx",1500,termrx,s,network,asy,0);

	/* We continue to handle the TCP->asy direction */
	fblock(network,PART_READ);
	while((i = fread(buf,1,BUFSIZ,network)) > 0)
		fwrite(buf,1,i,asy);
quit:	fclose(network);
	fclose(asy);
	killproc(&rxproc);
	logmsg(s,"close term");
	free(buf);
	close_s(s);
	tunregister(ifp,0);
}
void
termrx(int s,void *p1,void *p2)
{
	int i;
	FILE *network = (FILE *)p1;
	FILE *asy = (FILE *)p2;
	char buf[BUFSIZ];
	
	fblock(asy,PART_READ);
	while((i = fread(buf,1,BUFSIZ,asy)) > 0){
		fwrite(buf,1,i,network);
		kwait(NULL);
	}
}
void
tregister(struct iface *ifp)
{
	struct tserv *tserv;

	tserv = (struct tserv *)calloc(1,sizeof(struct tserv));
	tserv->ifp = ifp;
	tserv->proc = Curproc;
	tserv->next = Tserv;
	Tserv = tserv;
}
void
tunregister(struct iface *ifp,int kill)
{
	struct tserv *tserv;
	struct tserv *prev = NULL;

	for(tserv = Tserv;tserv != NULL;prev = tserv,tserv = tserv->next){
		if(tserv->ifp == ifp)
			break;
	}
	if(tserv == NULL)
		return;
	if(kill)
		alert(tserv->proc,EABORT);

	if(prev == NULL)
		Tserv = tserv->next;
	else
		prev->next = tserv->next;
	free(tserv);
}


/* Stop term server */
int
term0(
int argc,
char *argv[],
void *p
){
	uint port;

	port = (argc < 2) ? IPPORT_TERM : atoi(argv[1]);
	return stop_tcp(port);
}
