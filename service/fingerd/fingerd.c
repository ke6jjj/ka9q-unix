/* Internet Finger server
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "lib/std/stdio.h"
#include <string.h>
#include "global.h"
#include "files.h"
#include "net/core/mbuf.h"
#include "core/socket.h"
#include "core/session.h"
#include "core/proc.h"
#include "lib/std/dirutil.h"
#include "commands.h"
#include "mailbox.h"

#include "service/ftp/ftp.h"

static void fingerd(int s,void *unused,void *p);

/* Start up finger service */
int
finstart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint port;

	port = (argc < 2) ? IPPORT_FINGER : atoi(argv[1]);
	return start_tcp(port,"Finger Server",fingerd,512);
}
static void
fingerd(s,n,p)
int s;
void *n;
void *p;
{
	char user[80];
	kFILE *fp;
	char *file,*cp;
	kFILE *network;

	network = kfdopen(s,"r+t");

	sockowner(s,Curproc);
	logmsg(s,"open Finger");
	kfgets(user,sizeof(user),network);
	rip(user);
	if(strlen(user) == 0){
		fp = dir(Fdir,0);
		if(fp == NULL)
			kfprintf(network,"No finger information available\n");
		else
			kfprintf(network,"Known users on this system:\n");
	} else {
		file = pathname(Fdir,user);
		cp = pathname(Fdir,"");
		/* Check for attempted security violation (e.g., somebody
		 * might be trying to finger "../ftpusers"!)
		 */
		if(strncmp(file,cp,strlen(cp)) != 0){
			fp = NULL;
			kfprintf(network,"Invalid user name %s\n",user);
		} else if((fp = kfopen(file,READ_TEXT)) == NULL)
			kfprintf(network,"User %s not known\n",user);
		free(cp);
		free(file);
	}
	if(fp != NULL){
		ksendfile(fp,network,ASCII_TYPE,0);
		kfclose(fp);
	}
	if(strlen(user) == 0 && Listusers != NULL)
		(*Listusers)(network);
	kfclose(network);
	logmsg(s,"close Finger");
}
int
fin0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint port;

	port = (argc < 2) ? IPPORT_FINGER : atoi(argv[1]);
	return stop_tcp(port);
}
