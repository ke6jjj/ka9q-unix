/* Internet finger client
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "stdio.h"
#include <string.h>
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "session.h"
#include "proc.h"
#include "netuser.h"
#include "commands.h"
#include "tty.h"
#include "errno.h"

static int keychar(int c);

int
dofinger(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ksockaddr_in sock;
	char *cp;
	int s,i;
	int c;
	struct session *sp;
	kFILE *network;

	/* Allocate a session descriptor */
	if((sp = newsession(Cmdline,FINGER,1)) == NULL){
		kprintf("Too many sessions\n");
		keywait(NULL,1);
		return 1;
	}
	sp->inproc = keychar;	/* Intercept ^C */
	sp->ttystate.echo = sp->ttystate.edit = 0;
	sock.sin_family = kAF_INET;
	sock.sin_port = IPPORT_FINGER;
	for(i=1;i<argc;i++){
		cp = strchr(argv[i],'@');
		if(cp == NULL){
			kprintf("%s: local names not supported\n",argv[i]);
			continue;
		}
		*cp++ = '\0';
		kprintf("%s@%s:\n",argv[i],cp);
		kprintf("Resolving %s...\n",cp);
		if((sock.sin_addr.s_addr = resolve(cp)) == 0){
			kprintf("unknown\n");
			continue;
		}
		kprintf("Trying %s...\n",psocket((struct ksockaddr *)&sock));
		if((s = ksocket(kAF_INET,kSOCK_STREAM,0)) == -1){
			kprintf("Can't create ksocket\n");
			break;
		}
		if(kconnect(s,(struct ksockaddr *)&sock,sizeof(sock)) == -1){
			cp = sockerr(s);
			kprintf("Connect failed: %s\n",cp != NULL ? cp : "");
			close_s(s);
			continue;
		}
		kprintf("Connected\n");
		
		sp->network = network = kfdopen(s,"r+t");
		kfprintf(network,"%s\n",argv[i]);
		kfflush(kstdout);
		while((c = kgetc(network)) != kEOF)
			kputchar(c);

		kfclose(network);
		sp->network = NULL;
	}
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

