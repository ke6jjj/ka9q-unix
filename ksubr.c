/* Machine or compiler-dependent portions of kernel
 * Turbo-C version for PC
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <dos.h>
#include "global.h"
#include "proc.h"
#include "nospc.h"
#include "commands.h"

static char *Taskers[] = {
	"",
	"DoubleDos",
	"DesqView",
	"Windows",
	"OS/2",
};


static oldNull;


static void pproc(struct proc *pp);

void
kinit()
{
	/* Initialize signal queue */
	Ksig.wp = Ksig.rp = Ksig.entry;
}
/* Print process table info
 * Since things can change while ps is running, the ready proceses are
 * displayed last. This is because an interrupt can make a process ready,
 * but a ready process won't spontaneously become unready. Therefore a
 * process that changes during ps may show up twice, but this is better
 * than not having it showing up at all.
 */
int
ps(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct proc *pp;
	int i;

	printf("Uptime %s\n",tformat(secclock()));

	printf("ksigs %lu queued %lu hiwat %u woken %lu nops %lu dups %u\n",Ksig.ksigs,
	 Ksig.ksigsqueued,Ksig.maxentries,Ksig.ksigwakes,Ksig.ksignops,Ksig.duksigs);
	Ksig.maxentries = 0;
	printf("kwaits %lu nops %lu from int %lu\n",
	 Ksig.kwaits,Ksig.kwaitnops,Ksig.kwaitints);
	printf("PID       SP        stksize   event     fl  in  out  name\n");

	for(pp = Susptab;pp != NULL;pp = pp->next)
		pproc(pp);

	for(i=0;i<PHASH;i++)
		for(pp = Waittab[i];pp != NULL;pp = pp->next)
			pproc(pp);

	for(pp = Rdytab;pp != NULL;pp = pp->next)
		pproc(pp);

	if(Curproc != NULL)
		pproc(Curproc);

	return 0;
}
static void
pproc(pp)
struct proc *pp;
{
	char insock[5],outsock[5];

	if(fileno(pp->input) != -1)
		sprintf(insock,"%3d",fileno(pp->input));
	else
		sprintf(insock,"   ");
	if(fileno(pp->output) != -1)
		sprintf(outsock,"%3d",fileno(pp->output));
	else
		sprintf(outsock,"   ");
	printf("%-10p%-10lx%-10u%-10p%c%c%c %s %s  %s\n",
	 pp,pp->env[0].__esp,pp->stksize,
	 pp->event,
	 pp->flags.istate ? 'I' : ' ',
	 pp->flags.waiting ? 'W' : ' ',
	 pp->flags.suspend ? 'S' : ' ',
	 insock,outsock,pp->name);
}

/* Machine-dependent initialization of a task */
void
psetup(pp,iarg,parg1,parg2,pc)
struct proc *pp;	/* Pointer to task structure */
int iarg;		/* Generic integer arg */
void *parg1;		/* Generic pointer arg #1 */
void *parg2;		/* Generic pointer arg #2 */
void (*pc)();		/* Initial execution address */
{
	int32 *stktop;

	/* Set up stack to make it appear as if the user's function was called
	 * by killself() with the specified arguments. When the user returns,
	 * killself() automatically cleans up.
	 *
	 * First, push args on stack in reverse order, simulating what C
	 * does just before it calls a function.
	 */
	stktop = (int32 *)pp->stack + pp->stksize;
	*--stktop = (int32)parg2;
	*--stktop = (int32)parg1;
	*--stktop = (int32)iarg;
		
	/* Now push the entry address of killself(), simulating the call to
	 * the user function.
	 */
	*--stktop = (int32)killself;

	/* Set up task environment. */
	setjmp(pp->env);
	pp->env[0].__esp = (int32)stktop;
	pp->env[0].__eip = (int32)pc;
	pp->env[0].__ebp = 0;		/* Anchor stack traces */
	/* Task initially runs with interrupts on */
	pp->flags.istate = 1;
}
unsigned
phash(event)
void *event;
{
	/* If PHASH is a power of two, this will simply mask off the
	 * higher order bits
	 */
	return (int)event % PHASH;
}
