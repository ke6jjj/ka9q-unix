/* NOS User Session control
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "lib/std/stdio.h"
#include "lib/std/errno.h"
#include "global.h"
#include "net/core/mbuf.h"
#include "core/proc.h"
#include "telnet.h"
#include "core/tty.h"
#include "core/session.h"
#include "hardware.h"
#include "core/socket.h"
#include "lib/util/cmdparse.h"
#include "commands.h"
#include "main.h"

#include "cmd/ftpcli/ftpcli.h"

struct session **Sessions;
struct session *Command;
struct session *Current;
struct session *Lastcurr;
char Notval[] = "Not a valid control block\n";
static char Badsess[] = "Invalid session\n";
char *Sestypes[] = {
	"",
	"Telnet",
	"FTP",
	"AX25",
	"Finger",
	"Ping",
	"NET/ROM",
	"Command",
	"More",
	"Hopcheck",
	"Tip",
	"PPP PAP",
	"Dial",
	"Query",
	"Cache",
	"Trace",
	"Repeat",
};

/* Convert a character string containing a decimal session index number
 * into a pointer. If the arg is NULL, use the current default session.
 * If the index is out of range or unused, return NULL.
 */
struct session *
sessptr(cp)
char *cp;
{
	unsigned int i;

	if(cp == NULL)
		return Lastcurr;

	i = (unsigned)atoi(cp);
	if(i >= Nsessions)
		return NULL;
	else
		return Sessions[i];
}

/* Select and display sessions */
int
dosession(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;
	struct ksockaddr fsocket;
	int i,k,s,ses;
	int t;
	char *cp;

	sp = (struct session *)p;

	if(argc > 1){
		if((sp = sessptr(argv[1])) != NULL){
			go(0,NULL,sp);
		} else
			kprintf("Session %s not active\n",argv[1]);
		return 0;
	}
	kprintf(" #  S#  Snd-Q State     Remote socket         Command\n");
	for(ses=0;ses<Nsessions;ses++){
		sp = Sessions[ses];
		if(sp == NULL || sp->type == COMMAND)
			continue;

		t = 0;
		cp = NULL;
		if(sp->network != NULL && (s = kfileno(sp->network)) != -1){
			i = SOCKSIZE;
			k = kgetpeername(s,&fsocket,&i);
			t += socklen(s,1);
			cp = sockstate(s);
		} else {
			k = s = -1;
			t = 0;
			cp = NULL;
		}
		kprintf("%c", (Lastcurr == sp)? '*':' ');
		kprintf("%-3u",sp->index);
		kprintf("%-4d%5d %-10s",s,t,(cp != NULL) ? cp : "");
		kprintf("%-22s",(k == 0) ? psocket(&fsocket) : "");
		if(sp->name != NULL)
			kprintf("%s",sp->name);
		kprintf("\n");

		/* Display FTP data channel, if any */
		if(sp->type == FTP && (s = kfileno(sp->cb.ftp->data)) != -1){
			i = SOCKSIZE;
			k = kgetpeername(s,&fsocket,&i);
			t += socklen(s,1);
			cp = sockstate(s);
			kprintf("    %-4d%5d %-10s",s,t,(cp != NULL) ? cp : "");
			kprintf("%-22s\n",(k == 0) ? psocket(&fsocket) : "");
		}
		if(sp->record != NULL)
			kprintf("    Record: %s\n",kfpname(sp->record));
		if(sp->upload != NULL)
			kprintf("    Upload: %s\n",kfpname(sp->upload));
	}
	return 0;
}
/* Resume current session, and wait for it */
int
go(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;

	sp = (struct session *)p;
	if(sp == NULL || sp->type == COMMAND)
		return 0;
	if(Current != Command)
		Lastcurr = Current;
	Current = sp;
	alert(Display,1);
	return 0;
}
int
doclose(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;

	sp = (struct session *)p;
	if(argc > 1)
		sp = sessptr(argv[1]);

	if(sp == NULL){
		kprintf(Badsess);
		return -1;
	}
	kshutdown(kfileno(sp->network),1);
	return 0;
}
int
doreset(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;

	sp = (struct session *)p;
	if(argc > 1)
		sp = sessptr(argv[1]);

	if(sp == NULL){
		kprintf(Badsess);
		return -1;
	}
	/* Unwedge anyone waiting for a domain resolution, etc */
	alert(sp->proc,kEABORT);
	kshutdown(kfileno(sp->network),2);
	if(sp->type == FTP)
		kshutdown(kfileno(sp->cb.ftp->data),2);
	return 0;
}
int
dokick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;

	sp = (struct session *)p;
	if(argc > 1)
		sp = sessptr(argv[1]);

	if(sp == NULL){
		kprintf(Badsess);
		return -1;
	}
	sockkick(kfileno(sp->network));
	if(sp->type == FTP)
		sockkick(kfileno(sp->cb.ftp->data));
	return 0;
}

int
dosfsize(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setlong(&Sfsize,"Scroll file size",argc,argv);	
}

struct session *
newsession(name,type,makecur)
char *name;
int type;
int makecur;
{
	register struct session *sp;
	int i;

	/* Search for a free slot in the session table */
	for(i=0;i < Nsessions;i++)
		if(Sessions[i] == NULL)
			break;
	if(i == Nsessions)
		return NULL;	/* All full */

	sp = Sessions[i] = (struct session *)calloc(1,sizeof(struct session));
	sp->index = i;
	sp->type = type;
	if(name != NULL)
		sp->name = strdup(name);
	sp->proc = Curproc;
	/* Create standard input and output sockets. Output is
	 * in text mode by default
	 */
	kfclose(kstdin);
	kstdin =  sp->input = pipeopen();
	ksetvbuf(kstdin,NULL,_kIONBF,0);
	kfclose(kstdout);
	kstdout = sp->output = displayopen("wt",0,Sfsize);

	/* on by default */
	sp->ttystate.crnl = sp->ttystate.edit = sp->ttystate.echo = 1;
	sp->parent = Current;
	if(makecur){
		Current = sp;
		alert(Display,1);
	}
	return sp;
}
void
freesession(spp)
struct session **spp;
{
	int i;
	struct session *sp;

	if(spp == NULL || (sp=*spp) == NULL || sp != Sessions[sp->index])
		return;	/* Not on session list */
	*spp = NULL;
	kwait(NULL);	/* Wait for any pending output to go */

	for(i=0;i<Nsessions;i++){
		if(Sessions[i] != NULL && Sessions[i]->parent == sp)
			Sessions[i]->parent = sp->parent;
	}
	Sessions[sp->index] = NULL;
	if(sp->proc1 != NULL)
		killproc(&sp->proc1);
	if(sp->proc2 != NULL)
		killproc(&sp->proc2);

	free(sp->ttystate.line);
	if(sp->network != NULL)
		kfclose(sp->network);

	if(sp->record != NULL)
		kfclose(sp->record);

	if(sp->upload != NULL)
		kfclose(sp->upload);

	free(sp->name);

	if(Lastcurr == sp)
		Lastcurr = sp->parent;
	if(Current == sp){
		Current = sp->parent;
		alert(Display,1);
	}
	free(sp);

}
/* Control session recording */
int
dorecord(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;
	char *mode;

	sp = (struct session *)p;
	if(sp == NULL){
		kprintf("No current session\n");
		return 1;
	}
	if(argc > 1){
		if(sp->record != NULL){
			kfclose(sp->record);
			sp->record = NULL;
		}
		/* Open new record file, unless file name is "off", which means
		 * disable recording
		 */
		if(strcmp(argv[1],"off") != 0){
			if(kfmode(sp->output,-1) == STREAM_ASCII)
				mode = APPEND_TEXT;
			else
				mode = APPEND_BINARY;

			if((sp->record = kfopen(argv[1],mode)) == NULL)
				kprintf("Can't open %s: %s\n",argv[1],ksys_errlist[kerrno]);
		}
	}
	if(sp->record != NULL)
		kprintf("Recording into %s\n",kfpname(sp->record));
	else
		kprintf("Recording off\n");
	return 0;
}
/* Control file transmission */
int
doupload(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct session *sp;

	sp = (struct session *)p;
	if(sp == NULL){
		kprintf("No current session\n");
		return 1;
	}
	if(argc < 2){
		if(sp->upload != NULL)
			kprintf("Uploading %s\n",kfpname(sp->upload));
		else
			kprintf("Uploading off\n");
		return 0;
	}
	if(strcmp(argv[1],"stop") == 0 && sp->upload != NULL){
		/* Abort upload */
		kfclose(sp->upload);
		sp->upload = NULL;
		killproc(&sp->proc2);
		return 0;
	}
	/* Open upload file */
	if((sp->upload = kfopen(argv[1],READ_TEXT)) == NULL){
		kprintf("Can't read %s: %s\n",argv[1],ksys_errlist[kerrno]);
		return 1;
	}
	/* All set, invoke the upload process */
	sp->proc2 = newproc("upload",1024,upload,0,sp,NULL,0);
	return 0;
}
/* File uploading task */
void
upload(unused,sp1,p)
int unused;
void *sp1;
void *p;
{
	struct session *sp;
	char *buf;

	sp = (struct session *)sp1;

	buf = mallocw(kBUFSIZ);
	while(kfgets(buf,kBUFSIZ,sp->upload) != NULL)
		if(kfputs(buf,sp->network) == kEOF)
			break;

	free(buf);
	kfflush(sp->network);
	kfclose(sp->upload);
	sp->upload = NULL;
	sp->proc2 = NULL;
}

/* Print prompt and read one character */
int
keywait(prompt,flush)
char *prompt;	/* Optional prompt */
int flush;	/* Flush queued input? */
{
	int c;
	int i;

	if(prompt == NULL)
		prompt = "Hit enter to continue"; 
	kprintf(prompt);
	kfflush(kstdout);
	c = _kfgetc(kstdin);
	/* Get rid of the prompt */
	for(i=strlen(prompt);i != 0;i--)
		kputchar('\b');
	for(i=strlen(prompt);i != 0;i--)
		kputchar(' ');
	for(i=strlen(prompt);i != 0;i--)
		kputchar('\b');
	kfflush(kstdout);
	return (int)c;
}

/* Flush the current session's standard output. Called on every clock tick */
void
sesflush(void)
{
	if(Current != NULL)
		kfflush(Current->output);
}
