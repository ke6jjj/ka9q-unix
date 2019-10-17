/* ICMP-related user commands
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "lib/std/stdio.h"
#include "global.h"
#include "../../mbuf.h"
#include "../../socket.h"
#include "../../proc.h"
#include "../../session.h"
#include "../../commands.h"
#include "lib/std/errno.h"

#include "lib/inet/netuser.h"
#include "net/inet/icmp.h"
#include "net/inet/internet.h"

#include "net/inet/ping.h"

static void pingtx(int s,void *ping1,void *p);
static void pinghdr(struct session *sp,struct ping *ping);
static int pingproc(int c);
static int keychar(int c);

/* Send ICMP Echo Request packets */
int
doping(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ksockaddr_in from;
	struct icmp icmp;
	struct mbuf *bp;
	int32 timestamp,rtt,abserr;
	int s,fromlen;
	struct ping ping;
	struct session *sp;

	memset(&ping,0,sizeof(ping));
	/* Allocate a session descriptor */
	if((sp = ping.sp = newsession(Cmdline,PING,1)) == NULL){
		kprintf("Too many sessions\n");
		return 1;
	}
	if((ping.s = s = ksocket(kAF_INET,kSOCK_RAW,ICMP_PTCL)) == -1){
		kprintf("Can't create socket\n");
		keywait(NULL,1);
		freesession(&sp);
		return 1;
	}
	sp->inproc = keychar;	/* Intercept ^C */
	if(SETSIG(kEABORT)){
		keywait(NULL,1);
		freesession(&sp);
		return 1;
	}
	kprintf("Resolving %s...\n",argv[1]);
	if((ping.target = resolve(argv[1])) == 0){
		kprintf("unknown\n");
		keywait(NULL,1);
		freesession(&sp);
		return 1;
	}
	kprintf("Pinging %s\n",inet_ntoa(ping.target));
	sp->cb.p = &ping;
	if(argc > 2)
		ping.len = atoi(argv[2]);

	if(argc > 3)
		ping.interval = atol(argv[3]);

	/* Optionally ping a range of IP addresses */
	if(argc > 4)
		ping.incflag = 1;

	if(ping.interval != 0){
		sp->proc1 = newproc("pingtx",300,pingtx,s,&ping,NULL,0);
	} else {
		/* One shot ping; let echo_proc hook handle response.
		 * An ID of MAXINT16 will not be confused with a legal socket
		 * number, which is used to identify repeated pings
		 */
		pingem(s,ping.target,0,MAXINT16,ping.len);
		kclose(s);
		freesession(&sp);
		return 0;
	}
	sp->inproc = pingproc;
	/* Now collect the replies */
	pinghdr(sp,&ping);
	for(;;){
		fromlen = sizeof(from);
		if(recv_mbuf(s,&bp,0,(struct ksockaddr *)&from,&fromlen) == -1)
			break;
		ntohicmp(&icmp,&bp);
		if(icmp.type != ICMP_ECHO_REPLY || icmp.args.echo.id != s){
			/* Ignore other people's responses */
			free_p(&bp);
			continue;
		}
		/* Get stamp */
		if(pullup(&bp,&timestamp,sizeof(timestamp))
		 != sizeof(timestamp)){
			/* The timestamp is missing! */
			free_p(&bp);	/* Probably not necessary */
			continue;
		}
		free_p(&bp);

		ping.responses++;

		/* Compute round trip time, update smoothed estimates */
		rtt = msclock() - timestamp;
		abserr = (rtt > ping.srtt) ? (rtt-ping.srtt) : (ping.srtt-rtt);

		if(ping.responses == 1){
			/* First response, base entire SRTT on it */
			ping.srtt = rtt;
			ping.mdev = 0;
			ping.maxrtt = ping.minrtt = rtt;
		} else {
			ping.srtt = (7*ping.srtt + rtt + 4) >> 3;
			ping.mdev = (3*ping.mdev + abserr + 2) >> 2;
			if(rtt > ping.maxrtt)
				ping.maxrtt = rtt;
			if(rtt < ping.minrtt)
				ping.minrtt = rtt;
		}
		if((ping.responses % 20) == 0)
			pinghdr(sp,&ping);
		kprintf("%10lu%10lu%5lu%8lu%8lu%8lu%8lu%8lu\n",
		 ping.sent,ping.responses,
		 (ping.responses*100 + ping.sent/2)/ping.sent,
		 rtt,ping.srtt,ping.mdev,ping.maxrtt,ping.minrtt);
	}
	killproc(&sp->proc1);
	kclose(s);
	keywait(NULL,1);
	freesession(&sp);
	return 0;
}
static int
keychar(c)
int c;
{
	if(c != CTLC)
		return 1;	/* Ignore all but ^C */

	kfprintf(Current->output,"^C\n");
	alert(Current->proc,kEABORT);
	return 0;
}
static void
pinghdr(sp,ping)
struct session *sp;
struct ping *ping;
{
	kprintf("      sent      rcvd    %%     rtt     avg    mdev     max     min\n");
}

void
echo_proc(
int32 source,
int32 dest,
struct icmp *icmp,
struct mbuf **bpp
){
	int32 timestamp,rtt;

	if(Icmp_echo && icmp->args.echo.id == MAXINT16
	 && pullup(bpp,&timestamp,sizeof(timestamp))
	 == sizeof(timestamp)){
		/* Compute round trip time */
		rtt = msclock() - timestamp;
		kprintf("%s: rtt %lu\n",inet_ntoa(source),rtt);
	}
	free_p(bpp);
}
/* Ping transmit process. Runs until killed */
static void
pingtx(s,ping1,p)
int s;		/* Socket to use */
void *ping1;
void *p;
{
	struct ping *ping;

	ping = (struct ping *)ping1;
	if(ping->incflag){
		for(;;){
			pingem(s,ping->target++,0,MAXINT16,ping->len);
			ping->sent++;
			ppause(ping->interval);
		}
	} else {
		for(;;){
			pingem(s,ping->target,ping->sent++,s,ping->len);
			ppause(ping->interval);
		}
	}
}


/* Send ICMP Echo Request packet */
int
pingem(s,target,seq,id,len)
int s;		/* Raw socket on which to send ping */
int32 target;	/* Site to be pinged */
uint seq;	/* ICMP Echo Request sequence number */
uint id;	/* ICMP Echo Request ID */
uint len;	/* Length of optional data field */
{
	struct mbuf *data;
	struct icmp icmp;
	struct ksockaddr_in to;
	int32 clock;
	int i;
	uint8 *cp;

	clock = msclock();
	data = ambufw(NET_HDR_PAD+len+sizeof(clock));
	data->data += NET_HDR_PAD;
	data->cnt = len+sizeof(clock);
#define	counter	1
#ifdef	rnd
	/* Set data field to random pattern */
	cp = data->data+sizeof(clock);
	while(len-- != 0)
		*cp++ = rand();
#elif	alternate
	/* Set optional data field, if any, to all 55's */
	if(len != 0)
		memset(data->data+sizeof(clock),0x55,len);
#elif	counter
	cp = data->data+sizeof(clock);
	i = 0;
	while(len-- != 0)
		*cp++ = i++;
#endif
	/* Insert timestamp and build ICMP header */
	memcpy(data->data,&clock,sizeof(clock));
	icmpOutEchos++;
	icmpOutMsgs++;
	icmp.type = ICMP_ECHO;
	icmp.code = 0;
	icmp.args.echo.seq = seq;
	icmp.args.echo.id = id;
	htonicmp(&icmp,&data);
	to.sin_family = kAF_INET;
	to.sin_addr.s_addr = target;
	send_mbuf(s,&data,0,(struct ksockaddr *)&to,sizeof(to));
	return 0;
}
static int
pingproc(c)
int c;
{
	struct ping *p;
	struct session *sp;

	sp = Current;
	p = (struct ping *)sp->cb.p;
	
	if(p->s == -1)
		return 1;	/* Shutting down, let keywait have it */
	switch(c){
	case '\033':
	case 'Q':
	case 'q':
	case 3:	/* ctl-c - quit */
		alert(sp->proc,kEABORT);
		killproc(&sp->proc1);
		kshutdown(p->s,2);
		p->s = -1;
		break;
	case ' ':	/* Toggle pinger */
		if(sp->proc1 != NULL){
			killproc(&sp->proc1);
			kfprintf(sp->output,"Pinging suspended, %lu sent\n",p->sent);
		} else {
			p->sent = p->responses = 0;
			sp->proc1 = newproc("pingtx",300,pingtx,p->s,p,NULL,0);
			kfprintf(sp->output,"Pinging resumed\n");
		}
		break;
	}
	return 0;
}

