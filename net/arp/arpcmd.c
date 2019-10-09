/* ARP commands
 * Copyright 1991, Phil Karn, KA9Q
 */
#include "../../top.h"

#include <ctype.h>
#include "../../global.h"

#include "../../stdio.h"
#include "../../mbuf.h"
#include "../../timer.h"
#include "../../cmdparse.h"
#include "../../commands.h"

#include "../enet/enet.h"
#include "../arp/arp.h"

#include "../../lib/inet/netuser.h"

static int doarpadd(int argc,char *argv[],void *p);
static int doarpdrop(int argc,char *argv[],void *p);
static int doarpflush(int argc,char *argv[],void *p);
static void dumparp(void);

static struct cmds Arpcmds[] = {
	{ "add", doarpadd, 0, 4, "arp add <hostid> ether|ax25|netrom|arcnet <ether addr|callsign>" },
	{ "drop", doarpdrop, 0, 3, "arp drop <hostid> ether|ax25|netrom|arcnet" },
	{ "flush", doarpflush, 0, 0, NULL },
	{ "publish", doarpadd, 0, 4, "arp publish <hostid> ether|ax25|netrom|arcnet <ether addr|callsign>" },
	{ NULL },
};
char *Arptypes[] = {
	"NET/ROM",
	"10 Mb Ethernet",
	"3 Mb Ethernet",
	"AX.25",
	"Pronet",
	"Chaos",
	"",
	"Arcnet",
	"Appletalk"
};

int
doarp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2){
		dumparp();
		return 0;
	}
	return subcmd(Arpcmds,argc,argv,p);
}

static int
doarpadd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	enum arp_hwtype hardware;
	int32 addr;
	uint8 *hwaddr;
	struct arp_tab *ap;
	struct arp_type *at;
	int pub = 0;

	if(argv[0][0] == 'p')	/* Is this entry published? */
		pub = 1;
	if((addr = resolve(argv[1])) == 0){
		kprintf(Badhost,argv[1]);
		return 1;
	}
	/* This is a kludge. It really ought to be table driven */
	switch(tolower(argv[2][0])){
	case 'n':	/* Net/Rom pseudo-type */
		hardware = ARP_NETROM;
		break;
	case 'e':	/* "ether" */
		hardware = ARP_ETHER;
		break;		
	case 'a':	/* "ax25" */
		switch(tolower(argv[2][1])) {
		case 'x':
			hardware = ARP_AX25;
			break;
		case 'r':
			hardware = ARP_ARCNET;
			break;
		default:
			kprintf("unknown hardware type \"%s\"\n",argv[2]);
			return -1;
		}
		break;
	case 'm':	/* "mac appletalk" */
		hardware = ARP_APPLETALK;
		break;
	default:
		kprintf("unknown hardware type \"%s\"\n",argv[2]);
		return -1;
	}
	/* If an entry already exists, clear it */
	if((ap = arp_lookup(hardware,addr)) != NULL)
		arp_drop(ap);

	at = &Arp_type[hardware];
	if(at->scan == NULL){
		kprintf("Attach device first\n");
		return 1;
	}
	/* Allocate buffer for hardware address and fill with remaining args */
	hwaddr = mallocw(at->hwalen);
	/* Destination address */
	(*at->scan)(hwaddr,argv[3]);
	ap = arp_add(addr,hardware,hwaddr,pub);	/* Put in table */
	free(hwaddr);				/* Clean up */
	stop_timer(&ap->timer);			/* Make entry permanent */
	set_timer(&ap->timer,0L);
	return 0;
}

/* Remove an ARP entry */
static int
doarpdrop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	enum arp_hwtype hardware;
	int32 addr;
	struct arp_tab *ap;

	if((addr = resolve(argv[1])) == 0){
		kprintf(Badhost,argv[1]);
		return 1;
	}
	/* This is a kludge. It really ought to be table driven */
	switch(tolower(argv[2][0])){
	case 'n':
		hardware = ARP_NETROM;
		break;
	case 'e':	/* "ether" */
		hardware = ARP_ETHER;
		break;		
	case 'a':	/* "ax25" */
		switch(tolower(argv[2][1])) {
		case 'x':
			hardware = ARP_AX25;
			break;
		case 'r':
			hardware = ARP_ARCNET;
			break;
		default:
			hardware = 0;
			break;
		}
		break;
	case 'm':	/* "mac appletalk" */
		hardware = ARP_APPLETALK;
		break;
	default:
		hardware = 0;
		break;
	}
	if((ap = arp_lookup(hardware,addr)) == NULL)
		return -1;
	arp_drop(ap);
	return 0;	
}
/* Flush all automatic entries in the arp cache */
static int
doarpflush(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct arp_tab *ap;
	struct arp_tab *aptmp;
	int i;

	for(i=0;i<HASHMOD;i++){
		for(ap = Arp_tab[i];ap != NULL;ap = aptmp){
			aptmp = ap->next;
			if(dur_timer(&ap->timer) != 0)
				arp_drop(ap);
		}
	}
	return 0;
}

/* Dump ARP table */
static void
dumparp()
{
	int i;
	struct arp_tab *ap;
	char e[128];

	kprintf("received %u badtype %u bogus addr %u reqst in %u replies %u reqst out %u\n",
	 Arp_stat.recv,Arp_stat.badtype,Arp_stat.badaddr,Arp_stat.inreq,
	 Arp_stat.replies,Arp_stat.outreq);

	kprintf("IP addr         Type           Time Q Addr\n");
	for(i=0;i<HASHMOD;i++){
		for(ap = Arp_tab[i];ap != (struct arp_tab *)NULL;ap = ap->next){
			kprintf("%-16s",inet_ntoa(ap->ip_addr));
			kprintf("%-15s",smsg(Arptypes,NHWTYPES,ap->hardware));
			kprintf("%-5ld",read_timer(&ap->timer)/1000L);
			if(ap->state == ARP_PENDING)
				kprintf("%-2u",len_q(ap->pending));
			else
				kprintf("  ");
			if(ap->state == ARP_VALID){
				if(Arp_type[ap->hardware].format != NULL){
					(*Arp_type[ap->hardware].format)(e,ap->hw_addr);
				} else {
					e[0] = '\0';
				}
				kprintf("%s",e);
			} else {
				kprintf("[unknown]");
			}
			if(ap->pub)
				kprintf(" (published)");
			kprintf("\n");
		}
	}
}
