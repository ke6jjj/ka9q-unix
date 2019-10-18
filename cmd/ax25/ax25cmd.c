/* AX25 control commands
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "lib/std/stdio.h"
#include "global.h"
#include "net/core/mbuf.h"
#include "core/timer.h"
#include "core/proc.h"
#include "net/core/iface.h"
#include "lib/util/cmdparse.h"
#include "core/socket.h"
#include "mailbox.h"
#include "core/session.h"
#include "core/tty.h"
#include "commands.h"
#include "lib/std/errno.h"

#include "net/ax25/ax25.h"
#include "net/ax25/lapb.h"

static int axdest(struct iface *ifp);
static int axheard(struct iface *ifp);
static void axflush(struct iface *ifp);
static int doaxflush(int argc,char *argv[],void *p);
static int doaxirtt(int argc,char *argv[],void *p);
static int doaxkick(int argc,char *argv[],void *p);
static int doaxreset(int argc,char *argv[],void *p);
static int doaxroute(int argc,char *argv[],void *p);
static int doaxstat(int argc,char *argv[],void *p);
static int doaxwindow(int argc,char *argv[],void *p);
static int doblimit(int argc,char *argv[],void *p);
static int dodigipeat(int argc,char *argv[],void *p);
static int domaxframe(int argc,char *argv[],void *p);
static int domycall(int argc,char *argv[],void *p);
static int don2(int argc,char *argv[],void *p);
static int dopaclen(int argc,char *argv[],void *p);
static int dopthresh(int argc,char *argv[],void *p);
static int dot1max(int argc,char *argv[],void *p);
static int dot2(int argc,char *argv[],void *p);
static int dot3(int argc,char *argv[],void *p);
static int doversion(int argc,char *argv[],void *p);

char *Ax25states[] = {
	"",
	"Disconn",
	"Listening",
	"Conn pend",
	"Disc pend",
	"Connected",
	"Recovery",
};

/* Ascii explanations for the disconnect reasons listed in lapb.h under
 * "reason" in ax25_cb
 */
char *Axreasons[] = {
	"Normal",
	"DM received",
	"Timeout"
};

static struct cmds Axcmds[] = {
	{ "blimit",	doblimit,	0, 0, NULL },
	{ "destlist",	doaxdest,	0, 0, NULL },
	{ "digipeat",	dodigipeat,	0, 0, NULL },
	{ "flush",	doaxflush,	0, 0, NULL },
	{ "heard",	doaxheard,	0, 0, NULL },
	{ "irtt",	doaxirtt,	0, 0, NULL },
	{ "kick",	doaxkick,	0, 2, "ax25 kick <axcb>" },
	{ "maxframe",	domaxframe,	0, 0, NULL },
	{ "mycall",	domycall,	0, 0, NULL },
	{ "paclen",	dopaclen,	0, 0, NULL },
	{ "pthresh",	dopthresh,	0, 0, NULL },
	{ "reset",	doaxreset,	0, 2, "ax25 reset <axcb>" },
	{ "retry",	don2,		0, 0, NULL },
	{ "route",	doaxroute,	0, 0, NULL },
	{ "status",	doaxstat,	0, 0, NULL },
	{ "t1max",	dot1max,	0, 0, "ax25 t1 <maximum retransmit timer value in ms>" },
	{ "t2",		dot2,		0, 0, "ax25 t2 <transmit time in ms>" },
	{ "t3",		dot3,		0, 0, NULL },
	{ "version",	doversion,	0, 0, NULL },
	{ "window",	doaxwindow,	0, 0, NULL },
	{ NULL },
};
static int keychar(int c);


/* Multiplexer for top-level ax25 command */
int
doax25(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Axcmds,argc,argv,p);
}

int
doaxheard(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp;

	if(argc > 1){
		if((ifp = if_lookup(argv[1])) == NULL){
			kprintf("Interface %s unknown\n",argv[1]);
			return 1;
		}
		if(ifp->output != ax_output){
			kprintf("Interface %s not AX.25\n",argv[1]);
			return 1;
		}
		axheard(ifp);
		return 0;
	}
	for(ifp = Ifaces;ifp != NULL;ifp = ifp->next){
		if(ifp->output != ax_output)
			continue;	/* Not an ax.25 interface */
		if(axheard(ifp) == kEOF)
			break;
	}
	return 0;
}
static int
axheard(ifp)
struct iface *ifp;
{
	struct lq *lp;
	char tmp[AXBUF];

	if(ifp->hwaddr == NULL)
		return 0;
	kprintf("%s:\n",ifp->name);
	kprintf("Station   Last heard           Pkts\n");
	for(lp = Lq;lp != NULL;lp = lp->next){
		if(lp->iface != ifp)
			continue;
		kprintf("%-10s%-17s%8lu\n",pax25(tmp,lp->addr),
		 tformat(secclock() - lp->time),lp->currxcnt);
	}
	return 0;
}
int
doaxdest(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp;

	if(argc > 1){
		if((ifp = if_lookup(argv[1])) == NULL){
			kprintf("Interface %s unknown\n",argv[1]);
			return 1;
		}
		if(ifp->output != ax_output){
			kprintf("Interface %s not AX.25\n",argv[1]);
			return 1;
		}
		axdest(ifp);
		return 0;
	}
	for(ifp = Ifaces;ifp != NULL;ifp = ifp->next){
		if(ifp->output != ax_output)
			continue;	/* Not an ax.25 interface */
		if(axdest(ifp) == kEOF)
			break;
	}
	return 0;
}
static int
axdest(ifp)
struct iface *ifp;
{
	struct ld *lp;
	struct lq *lq;
	char tmp[AXBUF];

	if(ifp->hwaddr == NULL)
		return 0;
	kprintf("%s:\n",ifp->name);
	kprintf("Station   Last ref         Last heard           Pkts\n");
	for(lp = Ld;lp != NULL;lp = lp->next){
		if(lp->iface != ifp)
			continue;

		kprintf("%-10s%-17s",
		 pax25(tmp,lp->addr),tformat(secclock() - lp->time));

		if(addreq(lp->addr,ifp->hwaddr)){
			/* Special case; it's our address */
			kprintf("%-17s",tformat(secclock() - ifp->lastsent));
		} else if((lq = al_lookup(ifp,lp->addr,0)) == NULL){
			kprintf("%-17s","");
		} else {
			kprintf("%-17s",tformat(secclock() - lq->time));
		}
		kprintf("%8lu\n",lp->currxcnt);
	}
	return 0;
}
static int
doaxflush(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp;

	for(ifp = Ifaces;ifp != NULL;ifp = ifp->next){
		if(ifp->output != ax_output)
			continue;	/* Not an ax.25 interface */
		axflush(ifp);
	}
	return 0;
}
static void
axflush(ifp)
struct iface *ifp;
{
	struct lq *lp,*lp1;
	struct ld *ld,*ld1;

	ifp->rawsndcnt = 0;
	for(lp = Lq;lp != NULL;lp = lp1){
		lp1 = lp->next;
		free(lp);
	}
	Lq = NULL;
	for(ld = Ld;ld != NULL;ld = ld1){
		ld1 = ld->next;
		free(ld);
	}
	Ld = NULL;
}

static int
doaxreset(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ax25_cb *axp;

	axp = (struct ax25_cb *)htop(argv[1]);
	if(!ax25val(axp)){
		kprintf(Notval);
		return 1;
	}
	reset_ax25(axp);
	return 0;
}

/* Display AX.25 link level control blocks */
static int
doaxstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ax25_cb *axp;
	char tmp[AXBUF];

	if(argc < 2){
		kprintf(__FWPTR" Snd-Q   Rcv-Q   Remote    State\n", "&AXB");
		for(axp = Ax25_cb;axp != NULL; axp = axp->next){
			kprintf(__PRPTR" %-8d%-8d%-10s%s\n",
			 axp,
			 len_q(axp->txq),len_p(axp->rxq),
			 pax25(tmp,axp->remote),
			 Ax25states[axp->state]);
		}
		return 0;
	}
	axp = (struct ax25_cb *)htop(argv[1]);
	if(!ax25val(axp)){
		kprintf(Notval);
		return 1;
	}
	st_ax25(axp);
	return 0;
}
/* Dump one control block */
void
st_ax25(axp)
struct ax25_cb *axp;
{
	char tmp[AXBUF];

	if(axp == NULL)
		return;
	kprintf(__FWPTR" Remote   RB V(S) V(R) Unack P Retry State\n", "&AXB");

	kprintf(__PRPTR" %-9s%c%c",axp,pax25(tmp,axp->remote),
	 axp->flags.rejsent ? 'R' : ' ',
	 axp->flags.remotebusy ? 'B' : ' ');
	kprintf(" %4d %4d",axp->vs,axp->vr);
	kprintf(" %02u/%02u %u",axp->unack,axp->maxframe,axp->proto);
	kprintf(" %02u/%02u",axp->retries,axp->n2);
	kprintf(" %s\n",Ax25states[axp->state]);

	kprintf("srtt = %lu mdev = %lu ",axp->srt,axp->mdev);
	kprintf("T1: ");
	if(run_timer(&axp->t1))
		kprintf("%lu",read_timer(&axp->t1));
	else
		kprintf("stop");
	kprintf("/%lu ms; ",dur_timer(&axp->t1));

	kprintf("T2: ");
	if(run_timer(&axp->t2))
		kprintf("%lu",read_timer(&axp->t2));
	else
		kprintf("stop");
	kprintf("/%lu ms; ",dur_timer(&axp->t2));

	kprintf("T3: ");
	if(run_timer(&axp->t3))
		kprintf("%lu",read_timer(&axp->t3));
	else
		kprintf("stop");
	kprintf("/%lu ms\n",dur_timer(&axp->t3));
}

/* Display or change our AX.25 address */
static int
domycall(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char tmp[AXBUF];

	if(argc < 2){
		kprintf("%s\n",pax25(tmp,Mycall));
		return 0;
	}
	if(setcall(Mycall,argv[1]) == -1)
		return -1;
	return 0;
}

/* Control AX.25 digipeating */
static int
dodigipeat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Digipeat,"Digipeat",argc,argv);
}
/* Set limit on retransmission backoff */
static int
doblimit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setuns(&Blimit,"blimit",argc,argv);
}
static int
doversion(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setuns(&Axversion,"AX25 version",argc,argv);
}

static int
doaxirtt(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setuns(&Axirtt,"Initial RTT (ms)",argc,argv);
}

/* Set maximum t1 timer value */
static int
dot1max(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setuns(&T1maxinit,"Maximum T1 timer value (ms)",argc,argv);
}


/* Set transmit timer */
static int
dot2(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setuns(&T2init,"Transmit delay (ms)",argc,argv);
}

/* Set idle timer */
static int
dot3(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setuns(&T3init,"Idle poll timer (ms)",argc,argv);
}

/* Set retry limit count */
static int
don2(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setuns(&N2,"Retry limit",argc,argv);
}
/* Force a retransmission */
static int
doaxkick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ax25_cb *axp;

	axp = (struct ax25_cb *)htop(argv[1]);
	if(!ax25val(axp)){
		kprintf(Notval);
		return 1;
	}
	kick_ax25(axp);
	return 0;
}
/* Set maximum number of frames that will be allowed in flight */
static int
domaxframe(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setuns(&Maxframe,"Window size (frames)",argc,argv);
}

/* Set maximum length of I-frame data field */
static int
dopaclen(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setuns(&Paclen,"Max frame length (bytes)",argc,argv);
}
/* Set size of I-frame above which polls will be sent after a timeout */
static int
dopthresh(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setuns(&Pthresh,"Poll threshold (bytes)",argc,argv);
}

/* Set high water mark on receive queue that triggers RNR */
static int
doaxwindow(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setuns(&Axwindow,"AX25 receive window (bytes)",argc,argv);
}
/* End of ax25 subcommands */

/* Initiate interactive AX.25 connect to remote station */
int
doconnect(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ksockaddr_ax fsocket;
	struct session *sp;
	int ndigis,i,s;
	uint8 digis[MAXDIGIS][AXALEN];
	uint8 target[AXALEN];

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
	/* Allocate a session descriptor */
	if((sp = newsession(Cmdline,AX25TNC,1)) == NULL){
		kprintf("Too many sessions\n");
		return 1;
	}
	sp->inproc = keychar;	/* Intercept ^C */
	if((s = ksocket(kAF_AX25,kSOCK_STREAM,0)) == -1){
		kprintf("Can't create socket\n");
		freesession(&sp);
		keywait(NULL,1);
		return 1;
	}
	fsocket.sax_family = kAF_AX25;
	setcall(fsocket.ax25_addr,argv[2]);
	strncpy(fsocket.iface,argv[1],ILEN);
	sp->network = kfdopen(s,"r+t");
	ksetvbuf(sp->network,NULL,_kIOLBF,kBUFSIZ);
	if(SETSIG(kEABORT)){
		keywait(NULL,1);
		freesession(&sp);
		return 1;
	}
	return tel_connect(sp, (struct ksockaddr *)&fsocket, sizeof(struct ksockaddr_ax));
}

/* Display and modify AX.25 routing table */
static int
doaxroute(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char tmp[AXBUF];
	int i,ndigis;
	struct ax_route *axr;
	uint8 target[AXALEN],digis[MAXDIGIS][AXALEN];

	if(argc < 2){
		kprintf("Target    Type   Digipeaters\n");
		for(axr = Ax_routes;axr != NULL;axr = axr->next){
			kprintf("%-10s%-6s",pax25(tmp,axr->target),
			 axr->type == kAX_LOCAL ? "Local":"Auto");
			for(i=0;i<axr->ndigis;i++){
				kprintf(" %s",pax25(tmp,axr->digis[i]));
			}
			kprintf("\n");
		}
		return 0;
	}
	if(argc < 3){
		kprintf("Usage: ax25 route add <target> [digis...]\n");
		kprintf("       ax25 route drop <target>\n");
		return 1;
	}
	if(setcall(target,argv[2]) == -1){
		kprintf("Bad target %s\n",argv[2]);
		return 1;
	}
	switch(argv[1][0]){
	case 'a':	/* Add route */
		if(argc < 3){
			kprintf("Usage: ax25 route add <target> [digis...]\n");
			return 1;
		}
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
			kprintf("Failed\n");
			return 1;
		}
		break;
	case 'd':	/* Drop route */
		if(ax_drop(target) == -1){
			kprintf("Not in table\n");
			return 1;
		}
		break;
	default:
		kprintf("Unknown command %s\n",argv[1]);
		return 1;
	}
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

/* Initiate unproto AX.25 session to the given digipeater path/call */
int
do_unproto_connect(int argc,char *argv[], void *p)
{
	struct ksockaddr_ax fsocket;
	struct session *sp;
	int ndigis,i,s;
	uint8 digis[MAXDIGIS][AXALEN];
	uint8 target[AXALEN];

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
	/* Allocate a session descriptor */
	if((sp = newsession(Cmdline,AX25TNC,1)) == NULL){
		kprintf("Too many sessions\n");
		return 1;
	}
	sp->inproc = keychar;	/* Intercept ^C */
	if((s = ksocket(kAF_AX25,kSOCK_DGRAM,0)) == -1){
		kprintf("Can't create socket\n");
		freesession(&sp);
		keywait(NULL,1);
		return 1;
	}
	fsocket.sax_family = kAF_AX25;
	setcall(fsocket.ax25_addr,argv[2]);
	strncpy(fsocket.iface,argv[1],ILEN);
	sp->network = kfdopen(s,"r+t");
	ksetvbuf(sp->network,NULL,_kIOLBF,kBUFSIZ);
	if(SETSIG(kEABORT)){
		keywait(NULL,1);
		freesession(&sp);
		return 1;
	}
	return tel_connect(sp, (struct ksockaddr *)&fsocket, sizeof(struct ksockaddr_ax));
}
