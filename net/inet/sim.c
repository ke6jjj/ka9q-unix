/* Simulate a network path by introducing delay (propagation and queuing),
 * bandwidth (delay as a function of packet size), duplication and loss.
 * Intended for use with the loopback interface
 */
#include "../../top.h"

#include "../../global.h"
#include "../../mbuf.h"
#include "../../timer.h"
#include "../../iface.h"
#include "../../cmdparse.h"
#include "ip.h"

static void simfunc(void *p);

struct pkt {
	struct iface *iface;
	struct timer timer;
	struct mbuf *bp;
};

struct {
	int32 prop;	/* Fixed prop delay, ms */
	int32 base;	/* Xmit time, base per pkt, ms */
	int32 perbyte;	/* Xmit time, ms/byte */
} Simctl = {
	250,80,2};
static int dopropdelay(int,char **,void *);
static int dobasedelay(int,char **,void *);
static int doperbytedelay(int,char **,void *);

static struct cmds Simcmds[] = {
	{ "propdelay",	dopropdelay,	0, 0, NULL },
	{ "basedelay",	dobasedelay,	0, 0, NULL },
	{ "perbyte",	doperbytedelay,	0, 0, NULL },
	{ NULL },
};

int
dosim(int argc,char *argv[],void *p)
{
	return subcmd(Simcmds,argc,argv,p);
}
static int
dopropdelay(int argc,char *argv[],void *p)
{
	return setlong(&Simctl.prop,"Simulator propagation delay, ms",argc,argv);
}
static int
dobasedelay(int argc,char *argv[],void *p)
{
	return setlong(&Simctl.base,"Simulator base delay, ms",argc,argv);
}
static int
doperbytedelay(int argc,char *argv[],void *p)
{
	return setlong(&Simctl.perbyte,"Simulator per byte delay, ms",argc,argv);
}

/* Send packet after delay */
static void
simfunc(p)
void *p;
{
        struct pkt *pkt = (struct pkt *)p;
        struct mbuf *bp = pkt->bp;
        struct iface *iface = pkt->iface;
        struct qhdr qhdr;

        stop_timer(&pkt->timer);        /* shouldn't be necessary */
        pullup(&bp,&qhdr,sizeof(qhdr));

        (*iface->send)(&bp,iface,qhdr.gateway,qhdr.tos);
        free(pkt);
}
void
sim_tx(int dev,void *arg1,void *unused)
{
        struct mbuf *bp;        /* Buffer to send */
        struct iface *iface;    /* Pointer to interface control block */
        struct qhdr qhdr;
        struct pkt *pkt;

        iface = arg1;
        for(;;){
                while(iface->outq == NULL)
                        kwait(&iface->outq);

		iface->txbusy = 1;
                bp = dequeue(&iface->outq);
                /* Simulate transmission time */
		if(Simctl.base+Simctl.perbyte != 0)
	                ppause(Simctl.base+Simctl.perbyte*len_p(bp));
                
		if(Simctl.prop != 0){
			/* Now put the packet into the constant-delay pipe */
	                pkt = (struct pkt *)malloc(sizeof(struct pkt));
	                pkt->iface = iface;
	                pkt->bp = bp;
	                pkt->timer.func = simfunc;
	                pkt->timer.arg = pkt;
	                set_timer(&pkt->timer,Simctl.prop);
	                start_timer(&pkt->timer);
		} else {
		        pullup(&bp,&qhdr,sizeof(qhdr));
		        (*iface->send)(&bp,iface,qhdr.gateway,qhdr.tos);
		}
		iface->txbusy = 0;
                /* Let other tasks run, just in case we didn't block */
		kwait(NULL);
	}
}

