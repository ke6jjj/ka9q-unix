#include <stdio.h>
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
	FILE *fp;
	char *name,*pass,*path;
	int pwdignore,perm;

	fp = fdopen(s,"r+t");
	setvbuf(fp,NULL,_IOLBF,BUFSIZ);
	sockowner(s,Curproc);		/* We own it now */
	fclose(stdin);
	stdin = fdup(fp);
	fclose(stdout);
	stdout = fdup(fp);

	name = malloc(BUFSIZ);
	pass = malloc(BUFSIZ);
	path = malloc(BUFSIZ);
	do {
		printf("login: ");
		fflush(stdout);
		if(fgets(name,BUFSIZ,stdin) == NULL)
			goto cleanup;
		rip(name);
		printf("Password: ");
		if(fgets(pass,BUFSIZ,stdin) == NULL)
			goto cleanup;
		rip(pass);
	} while((perm = userlogin(name,pass,&path,BUFSIZ,&pwdignore)) == -1);
	logmsg(s,"Telnet login: %s", name);
	if(!(perm & SYSOP_CMD)){
		printf("Access not authorized\n");
		goto cleanup;
	}
	printf("%s> ",Hostname); fflush(stdout);
	while(fgets(cmdbuf,sizeof(cmdbuf),fp) != NULL){
		cmdparse(Remcmds,cmdbuf,NULL);
		printf("%s> ",Hostname);fflush(stdout);
	}
cleanup:
	fclose(fp);
	logmsg(s,"Telnet logout: %s", name);

	free(name);
	free(pass);
	free(path);
}
