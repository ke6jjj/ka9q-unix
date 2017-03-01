/*
 *	HOP.C   -- trace route packets take to a remote host
 *
 *	02-90	-- Katie Stevens (dkstevens@ucdavis.edu)
 *		   UC Davis, Computing Services
 *		   Davis, CA
 *	04-90	-- Modified by Phil Karn to use raw IP sockets to kread replies
 *	08-90	-- Modified by Bill Simpson to display domain names
 */
#include "top.h"

#include "stdio.h"
#include <string.h>
#include "errno.h"
#include "global.h"
#include "mbuf.h"
#include "usock.h"
#include "socket.h"
#include "session.h"
#include "timer.h"
#include "proc.h"
#include "netuser.h"
#include "domain.h"
#include "commands.h"
#include "tty.h"
#include "cmdparse.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "hardware.h"

#define HOPMAXQUERY	5		/* Max# queries each TTL value */
static uint Hoprport = 32768+666;	/* funny port for udp probes */
#define HOP_HIGHBIT	32768		/* Mask to check ICMP msgs */


#define HOPTRACE	1		/* Enable HOP tracing */
#ifdef HOPTRACE
static int Hoptrace = 0;
static int hoptrace(int argc,char *argv[],void *p);
#endif


static unsigned  short Hopmaxttl  = 30;		/* max attempts */
static unsigned  short Hopmaxwait = 5;		/* secs timeout each attempt */
static unsigned  short Hopquery   = 3;		/* #probes each attempt */

static int hopcheck(int argc,char *argv[],void *p);
static int hopttl(int argc,char *argv[],void *p);
static int hokwait(int argc,char *argv[],void *p);
static int hopnum(int argc,char *argv[],void *p);
static int geticmp(int s,uint lport,uint fport,
	int32 *sender,char *type,char *code);
static int keychar(int c);

static struct cmds Hopcmds[] = {
	"check",	hopcheck,	2048,	2,	"check <host>",
	"maxttl",	hopttl,		0,	0,	NULL,
	"maxwait",	hokwait,	0,	0,	NULL,
	"queries",	hopnum,		0,	0,	NULL,
#ifdef HOPTRACE
	"trace",	hoptrace,	0,	0,	NULL,
#endif
	NULL,
};

/* attempt to trace route to a remote host */
int
dohop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Hopcmds,argc,argv,p);
}

/* Set/show # queries sent each TTL value */
static int
hopnum(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint r;
	int x = Hopquery;
	r = setint(&x,"# queries each attempt",argc,argv);
	if ((x <= 0)||(x > HOPMAXQUERY)) {
		kprintf("Must be  0 < x <= %d\n",HOPMAXQUERY);
		return 0;
	} else {
		Hopquery = x;
	}
    return (int)r;
}
#ifdef HOPTRACE
/* Set/show tracelevel */
static int
hoptrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Hoptrace,"HOPCHECK tracing",argc,argv);
}
#endif
/* Set/show maximum TTL value for a traceroute query */
static int
hopttl(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint r;
	int x = Hopmaxttl;
	r = setint(&x,"Max attempts to reach host",argc,argv);
	if ((x <= 0)||(x > 255)) {
		kprintf("Must be  0 < x <= 255\n");
		return 0;
	} else {
		Hopmaxttl = x;
	}
    return (int)r;
}
/* Set/show #secs until timeout for a traceroute query */
static int
hokwait(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint r;
	int x = Hopmaxwait;
	r = setint(&x,"# secs to wait for reply to query",argc,argv);
	if (x <= 0) {
		kprintf("Must be >= 0\n");
		return 0;
	} else {
		Hopmaxwait = x;
	}
    return (int)r;
}

/* send probes to trace route of a remote host */
static int
hopcheck(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;		/* Session for trace output */
	int s;				/* Socket for queries */
	int s1;				/* Raw ksocket for replies */
	struct ksocket lsocket;		/* Local ksocket sending queries */
	struct ksocket rsocket;		/* Final destination of queries */
	int32 cticks;			/* Timer for query replies */
	int32 icsource;			/* Sender of last ICMP reply */
	char ictype;			/* ICMP type last ICMP reply */
	char iccode;			/* ICMP code last ICMP reply */
	int32 lastaddr;			/* Sender of previous ICMP reply */
	struct ksockaddr_in sock;
	struct usock *usp;
	struct ksockaddr_in *sinp;
	unsigned char sndttl, q;
	int tracedone = 0;
	int ilookup = 1;		/* Control of inverse domain lookup */
	int c;
	extern int optind;
	char *hostname;
	int save_trace;
	int user_reset = 0;

	optind = 1;
	while((c = kgetopt(argc,argv,"n")) != kEOF){
		switch(c){
		case 'n':
			ilookup = 0;
			break;
		}
	}
	hostname = argv[koptind];
	/* Allocate a session descriptor */
	if((sp = newsession(Cmdline,HOP,1)) == NULL){
		kprintf("Too many sessions\n");
		keywait(NULL,1);
		return 1;
	}
	sp->inproc = keychar;
	s = -1;

	/* Setup UDP ksocket to remote host */
	sock.sin_family = kAF_INET;
	sock.sin_port = Hoprport;
	kprintf("Resolving %s... ",hostname);
	if((sock.sin_addr.s_addr = resolve(hostname)) == 0){
		kprintf("unknown\n",hostname);
		keywait(NULL,1);
		freesession(&sp);
		return 1;
	}

	/* Open ksocket to remote host */
	kprintf("%s ",psocket((struct ksockaddr *)&sock));
	if((s = ksocket(kAF_INET,kSOCK_DGRAM,0)) == -1){
		kprintf("Can't create udp ksocket\n");
		keywait(NULL,1);
		freesession(&sp);
		return 1;
	}
	if(kconnect(s,(struct ksockaddr *)&sock,sizeof(sock)) == -1){
		kprintf("Connect failed\n");
		keywait(NULL,1);
		freesession(&sp);
		return 1;
	}
	if((s1 = ksocket(kAF_INET,kSOCK_RAW,ICMP_PTCL)) == -1){
		kprintf("Can't create raw ksocket\n");
		keywait(NULL,1);
		kclose(s);
		freesession(&sp);
		return 1;
	}
	kprintf("\n");
	/* turn off icmp tracing while hop-checking */
	save_trace = Icmp_trace;
	Icmp_trace = 0;

	/* Setup structures to send queries */
	/* Retrieve ksocket details for user ksocket control block */
	usp = itop(s);
	sinp = (struct ksockaddr_in *)usp->name;
	lsocket.address = sinp->sin_addr.s_addr;
	lsocket.port = sinp->sin_port;
	sinp = (struct ksockaddr_in *)usp->peername;
	rsocket.address = sinp->sin_addr.s_addr;

	/* Send queries with increasing TTL; start with TTL=1 */
	if (Hoptrace)
		logmsg(s,"HOPCHECK start trace to %s\n",sp->name);
	for (sndttl=1; (sndttl < Hopmaxttl); ++sndttl, sinp->sin_port++) {
		/* Increment funny UDP port number each round */
		rsocket.port = sinp->sin_port;
		kprintf("%3d:",sndttl);
		lastaddr = (int32)0;
		/* Send a round of queries */
		for (q=0; (q < Hopquery); ++q) {
			struct mbuf *bp;
			bp = ambufw(0);
			send_udp(&lsocket,&rsocket,0,sndttl,&bp,0,0,0);
			cticks = msclock();
			kalarm( ((long)Hopmaxwait*1000L) );

			/* Wait for a reply to our query */
			if(geticmp(s1,lsocket.port,rsocket.port,
			 &icsource,&ictype,&iccode) == -1){
				if(kerrno != kEALARM){
					user_reset = 1;
					goto done;	/* User reset */
				}
				/* Alarm rang, give up waiting for replies */
				kprintf(" ***");
				continue;
			}
			/* Save #ticks taken for reply */
                        cticks = msclock() - cticks;
			/* Report ICMP reply */
			if (icsource != lastaddr) {
				struct rr *save_rrlp, *rrlp;

				if(lastaddr != (int32)0)
					kprintf("\n    ");
				kprintf(" %-15s",inet_ntoa(icsource));
				if(ilookup){
					for(rrlp = save_rrlp = inverse_a(icsource);
					    rrlp != NULL;
					    rrlp = rrlp->next){
						if(rrlp->rdlength > 0){
							switch(rrlp->type){
							case TYPE_PTR:
								kprintf(" %s", rrlp->rdata.name);
								goto got_name;
							case TYPE_A:
								kprintf(" %s", rrlp->name);
								goto got_name;
							}
#ifdef notdef
							if(rrlp->next != NULL)
								kprintf("\n%20s"," ");
#endif
						}
					}
					got_name: ;
					free_rr(&save_rrlp);

				}
				lastaddr = icsource;
			}
                        kprintf(" (%ld ms)",cticks);
#ifdef HOPTRACE
			if (Hoptrace)
				logmsg(s,
				    "(hopcheck) ICMP from %s (%ldms) %s %s",
				    inet_ntoa(icsource),
				    cticks,
				    Icmptypes[ictype],
				    ((ictype == ICMP_TIME_EXCEED)?Exceed[iccode]:Unreach[iccode]));
#endif

			/* Check type of reply */
			if (ictype == ICMP_TIME_EXCEED)
				continue;
			/* Reply was: destination unreachable */
			switch(iccode) {
			case ICMP_PORT_UNREACH:
				++tracedone;
				break;
			case ICMP_NET_UNREACH:
				++tracedone;
				kprintf(" !N");
				break;
			case ICMP_HOST_UNREACH:
				++tracedone;
				kprintf(" !H");
				break;
			case ICMP_PROT_UNREACH:
				++tracedone;
				kprintf(" !P");
				break;
			case ICMP_FRAG_NEEDED:
				++tracedone;
				kprintf(" !F");
				break;
			case ICMP_ROUTE_FAIL:
				++tracedone;
				kprintf(" !S");
				break;
                        case ICMP_ADMIN_PROHIB:
                                ++tracedone;
                                kprintf(" !A");
                                break;
                        default:
                                kprintf(" !?");
                                break;
			}
		}
		/* Done with this round of queries */
		kalarm((long)0);
		kprintf("\n");
		/* Check if we reached remote host this round */
		if (tracedone != 0)
			break;
	}

	/* Done with traceroute */
done:	kclose(s);
	s = -1;
	kclose(s1);
	if(user_reset)
		kprintf("\n");	/* May have been in middle of line */
	kprintf("traceroute done: ");
	Icmp_trace = save_trace;
	if(user_reset){
		kprintf("user abort\n");
	} else if (sndttl >= Hopmaxttl) {
		kprintf("!! maximum TTL exceeded\n");
	} else if ((icsource == rsocket.address)
		    &&(iccode == ICMP_PORT_UNREACH)) {
		kprintf("normal (%s %s)\n",
			Icmptypes[ictype],Unreach[iccode]);
	} else {
		kprintf("!! %s %s\n",
			Icmptypes[ictype],Unreach[iccode]);
	}
#ifdef HOPTRACE
	if (Hoptrace)
		logmsg(s,"HOPCHECK to %s done",sp->name);
#endif
	keywait(NULL,1);
	freesession(&sp);
	return 0;
}

/* Hop check session keyboard upcall routine -- handles ^C */
static int
keychar(c)
int c;
{
	switch(c){
	case CTLC:
		alert(Current->proc,kEABORT);
		return 0;
	}
	return 1;
}

/* Read raw network ksocket looking for ICMP messages in response to our
 * UDP probes
 */
static int
geticmp(s,lport,fport,sender,type,code)
int s;
uint lport;
uint fport;
int32 *sender;
char *type,*code;
{
	int size;
	struct icmp icmphdr;
	struct ip iphdr;
	struct udp udphdr;
	struct mbuf *bp;
	struct ksockaddr_in sock;

	for(;;){
		size = sizeof(sock);
		if(recv_mbuf(s,&bp,0,(struct ksockaddr *)&sock,&size) == -1)
			return -1;
		/* It's an ICMP message, let's see if it's interesting */
		ntohicmp(&icmphdr,&bp);
		if((icmphdr.type != ICMP_TIME_EXCEED ||
		 icmphdr.code != ICMP_TTL_EXCEED)
		 && icmphdr.type != ICMP_DEST_UNREACH){
			/* We're not interested in these */
			free_p(&bp);
			continue;
		}
		ntohip(&iphdr,&bp);
		if(iphdr.protocol != UDP_PTCL){
			/* Not UDP, so can't be interesting */
			free_p(&bp);
			continue;
		}
		ntohudp(&udphdr,&bp);
		if(udphdr.dest != fport || udphdr.source != lport){
			/* Not from our hopcheck session */
			free_p(&bp);
			continue;
		}
		/* Passed all of our checks, so return it */
		*sender = sock.sin_addr.s_addr;
		*type = icmphdr.type;
		*code = icmphdr.code;
		free_p(&bp);
		return 0;
	}
}
