/* ICMP-related user commands
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "lib/std/stdio.h"
#include "global.h"
#include "net/core/mbuf.h"

#include "core/timer.h"
#include "core/socket.h"
#include "core/proc.h"
#include "lib/util/cmdparse.h"
#include "commands.h"

#include "lib/inet/netuser.h"
#include "net/inet/internet.h"
#include "net/inet/icmp.h"
#include "net/inet/ip.h"

static int doicmpec(int argc, char *argv[],void *p);
static int doicmpstat(int argc, char *argv[],void *p);
static int doicmptr(int argc, char *argv[],void *p);

static struct cmds Icmpcmds[] = {
	{ "echo",	doicmpec,	0, 0, NULL },
	{ "status",	doicmpstat,	0, 0, NULL },
	{ "trace",	doicmptr,	0, 0, NULL },
	{ NULL },
};

int Icmp_trace;
int Icmp_echo = 1;

int
doicmp(
int argc,
char *argv[],
void *p
){
	return subcmd(Icmpcmds,argc,argv,p);
}

static int
doicmpstat(
int argc,
char *argv[],
void *p
){
	int i;
	int lim;

	/* Note that the ICMP variables are shown in column order, because
	 * that lines up the In and Out variables on the same line
	 */
	lim = NUMICMPMIB/2;
	for(i=1;i<=lim;i++){
		kprintf("(%2u)%-20s%10lu",i,Icmp_mib[i].name,
		 Icmp_mib[i].value.integer);
		kprintf("     (%2u)%-20s%10lu\n",i+lim,Icmp_mib[i+lim].name,
		 Icmp_mib[i+lim].value.integer);
	}
	return 0;
}
static int
doicmptr(
int argc,
char *argv[],
void *p
){
	return setbool(&Icmp_trace,"ICMP tracing",argc,argv);
}
static int
doicmpec(
int argc,
char *argv[],
void *p
){
	return setbool(&Icmp_echo,"ICMP echo response accept",argc,argv);
}
