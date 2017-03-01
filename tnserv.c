#include "top.h"

#include "stdio.h"
#include "global.h"
#include "socket.h"
#include "telnet.h"
#include "proc.h"
#include "files.h"
#include "commands.h"
#include "cmdparse.h"

static void tnserv(int s,void *p1,void *p2);

int
tnstart(
int argc,
char *argv[],
void *p
){
	uint port;

	port = (argc < 2) ? IPPORT_TELNET : atoi(argv[1]);
	return start_tcp(port,"Telnet Server",tnserv,4096);
}
void
tnserv(int s,void *p1,void *p2)
{
	char cmdbuf[256];
	kFILE *fp;
	char *name,*pass,*path;
	int pwdignore,perm;

	fp = kfdopen(s,"r+t");
	ksetvbuf(fp,NULL,_kIOLBF,kBUFSIZ);
	sockowner(s,Curproc);		/* We own it now */
	kfclose(kstdin);
	kstdin = kfdup(fp);
	kfclose(kstdout);
	kstdout = kfdup(fp);

	name = malloc(kBUFSIZ);
	pass = malloc(kBUFSIZ);
	path = malloc(kBUFSIZ);
	do {
		kprintf("login: ");
		kfflush(kstdout);
		if(kfgets(name,kBUFSIZ,kstdin) == NULL)
			goto cleanup;
		rip(name);
		kprintf("Password: ");
		if(kfgets(pass,kBUFSIZ,kstdin) == NULL)
			goto cleanup;
		rip(pass);
	} while((perm = userlogin(name,pass,&path,kBUFSIZ,&pwdignore)) == -1);
	logmsg(s,"Telnet login: %s", name);
	if(!(perm & SYSOP_CMD)){
		kprintf("Access not authorized\n");
		goto cleanup;
	}
	kprintf("%s> ",Hostname); kfflush(kstdout);
	while(kfgets(cmdbuf,sizeof(cmdbuf),fp) != NULL){
		cmdparse(Remcmds,cmdbuf,NULL);
		kprintf("%s> ",Hostname);kfflush(kstdout);
	}
cleanup:
	kfclose(fp);
	logmsg(s,"Telnet logout: %s", name);

	free(name);
	free(pass);
	free(path);
}
