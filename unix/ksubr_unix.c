/* Machine or compiler-dependent portions of kernel
 * ANSI C version for Unix systems using pthreads.
 *
 * Copyright 1991 Phil Karn, KA9Q
 * Copyright 2017 Jeremy Cooper, KE6JJJ
 * 
 * Co-operative "process" sharing using pthreads for UNIX-hosted KA9Q NOS.
 *
 * The KA9Q NOS is a cooperative, single-task sharing system. Only one
 * process runs with control at any one time, but processes can transfer
 * control to one another through the well-defined switch points kwait()
 * and ksignal(). It also supports device interrupts and service handlers
 * for those interrupts. Interrupt servicing may be locked out with the
 * disable() call and re-enabled with the enable() call.
 *
 * To implement this procedure safely under UNIX without having to rely
 * on an interrupt system nor a stack pointer switching scheme (which is
 * hard to port), this implementation uses POSIX threads or "pthreads".
 *
 * Each process in NOS has a corresponding pthread that runs it. When a
 * process wishes to switch control to another process it simply signals
 * the condition semaphore associated with that thread and then releases
 * the global single-process mutex.
 *
 * "Interrupts" are generated by device thread loops which use UNIX
 * read() and write() calls to perform I/O and also use the ksignal() NOS
 * function to signal I/O completion to waiting NOS processes. The interrupt
 * lockout mechanism in enable()/disable() uses a single mutex known
 * as the "interrupt mutex". When an interrupt thread wishes to interact
 * with NOS processes it must first aqcuire the interrupt mutex, and when
 * it is done it releases the mutex.
 */
#include "top.h"

#ifndef UNIX
#error "This file should only be built on POSIX/UNIX systems."
#endif

#include <pthread.h>
#include <assert.h>
#include "lib/std//stdio.h"
#include "global.h"
#include "core/proc.h"
#include "commands.h"

static void pproc(struct proc *pp); /* Print a process entry line for PS */
static void *proc_entry(void *pptr);/* pthread entry point for new process */

/* The lock which is held by the running process and which keeps other
 * processes from running at the same time.
 */
static pthread_mutex_t g_curproc_mutex;

/* The lock which prevents "interrupt" threads from running */
static pthread_mutex_t g_interrupt_mutex;
/* And a condition variable for threads waiting for something to happen */
static int            g_proc_halted;
static pthread_cond_t g_interrupt_cond;

/* The state of this thread's holding of the interrupt mutex, used to simply
 * avoid recursively locking the interrupt mutex.
 *
 * This is a thread-specific value; it cannot be queried outside this thread,
 * nor would it make sense to. 
 */
static pthread_key_t g_interrupts_disabled;

/* Initialize the machine-dependent kernel */
void
kinit()
{
	/* Initialize signal queue */
	Ksig.wp = Ksig.rp = Ksig.entry;
	Ksig.nentries = 0;

	/* Initialize the single-process mutex */
	if (pthread_mutex_init(&g_curproc_mutex, NULL) != 0) {
		perror("global mutex init");
		exit(1);
	}

	/* Initialize the "interrupt" lockout mutex */
	if (pthread_mutex_init(&g_interrupt_mutex, NULL) != 0) {
		perror("interrupt mutex init");
		exit(1);
	}

	/* Initialize the "halting for interrupts" condition. */
	if (pthread_cond_init(&g_interrupt_cond, NULL) != 0) {
		perror("interrupt cond init");
		exit(1);
	}

	/* Create the per-thread interrupts-disabled key. By default
	 * each thread will receive the value NULL for this key when
	 * queried. It is only when a thread actively acquires the interrupt
	 * disabling mutex that it updates its value for this key to a
	 * non-NULL value.
	 */
	if (pthread_key_create(&g_interrupts_disabled, NULL) != 0) {
		perror("interrupts disabled key");
		exit(1);
	}
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

	kprintf("Uptime %s\n",tformat(secclock()));

	kprintf("ksigs %lu queued %lu hiwat %u woken %lu nops %lu dups %u\n",Ksig.ksigs,
	 Ksig.ksigsqueued,Ksig.maxentries,Ksig.ksigwakes,Ksig.ksignops,Ksig.duksigs);
	Ksig.maxentries = 0;
	kprintf("kwaits %lu nops %lu from int %lu\n",
	 Ksig.kwaits,Ksig.kwaitnops,Ksig.kwaitints);
	kprintf(__FWPTR" stksize   "__FWPTR" fl  in  out  name\n", "PID",
		"event");

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

	if(kfileno(pp->input) != -1)
		sprintf(insock,"%3d",kfileno(pp->input));
	else
		sprintf(insock,"   ");
	if(kfileno(pp->output) != -1)
		sprintf(outsock,"%3d",kfileno(pp->output));
	else
		sprintf(outsock,"   ");
	kprintf(__PRPTR" %-9u "__PRPTR" %c%c%c %s %s  %s\n",
	 pp,pp->stksize,
	 pp->event,
	 pp->flags.istate ? 'I' : ' ',
	 pp->flags.waiting ? 'W' : ' ',
	 pp->flags.suspend ? 'S' : ' ',
	 insock,outsock,pp->name);
}

/* Machine-dependent initialization of the first task (which starts
 * on its own and is inducted into the system.
 */
void
init_psetup(struct proc *pp)
{
	/* Initialize the process' run/wake semaphore */
	if (pthread_cond_init(&pp->cond, NULL) != 0) {
		perror("main process wake cond");
		exit(1);
	}
	/* Process starts out running in whatever mode it currently is in */
	pp->flags.run = 1;
	pp->flags.exit = 0;
	pp->flags.istate = istate();

	/* Process gets the "current process" mutex too. */
	pthread_mutex_lock(&g_curproc_mutex);
}

/* Machine-dependent initialization of a task */
void
psetup(pp,iarg,parg1,parg2,pc)
struct proc *pp;	/* Pointer to task structure */
int iarg;		/* Generic integer arg */
void *parg1;		/* Generic pointer arg #1 */
void *parg2;		/* Generic pointer arg #2 */
void (*pc)(int,void*,void*);	/* Initial execution address */
{
	pthread_attr_t attr;

	/* Initialize the process' run/wake semaphore */
	if (pthread_cond_init(&pp->cond, NULL) != 0) {
		perror("new process wake cond");
		exit(1);
	}

	pp->pc = pc;
	/* Task initially runs with interrupts on */
	pp->flags.istate = 1;
	/* But is to remain paused until officially sanctioned by NOS */
	pp->flags.run = 0;

	/* Establish the minimum stack size for the new thread. The
	 * value proposed by NOS is almost certainly comical and too
	 * small to matter since most operating systems that are modern
	 * enough to have a pthreads implementation are also running on
	 * hardware modern enough that the default stack size is likely
	 * measured in megabytes.
	 */
	if (pthread_attr_init(&attr) != 0) {
		perror("stack attr init");
		exit(1);
	}
	if (pthread_attr_setstacksize(&attr, pp->stksize) != 0) {
		perror("stack size set");
		exit(1);
	}

	/* Create a new thread to run the process. This thread will
	 * remain paused, however, until the processes' flags.run
	 * flag is enabled and the process' run semaphore (cond) is
	 * signaled.
	 */
	if (pthread_create(&pp->thread, &attr, proc_entry, pp) != 0) {
		perror("pthread create");
		exit(1);
	}

	pthread_attr_destroy(&attr);
}

void
pteardown(struct proc *pp)
{
	void *dummy;

	/* Ask the thread to exit */
	pp->flags.exit = 1;

	/* Wake it up */
	pthread_cond_signal(&pp->cond);

	/* Allow it to wake up */
	pthread_mutex_unlock(&g_curproc_mutex);

	/* Join with it */
	assert(pthread_join(pp->thread, &dummy) == 0);

	/* Take back the single process mutex */
	pthread_mutex_lock(&g_curproc_mutex);

	/* Destroy the thread's condition variable */
	pthread_cond_destroy(&pp->cond);
}

unsigned
phash(event)
void *event;
{
	/* If PHASH is a power of two, this will simply mask off the
	 * higher order bits
	 */
	return (uint)event % PHASH;
}

/* Return whether or not this thread has locked out interrupt threads
 * from interacting with NOS.
 *
 * The calling thread of this function must either:
 *
 * 1. already hold the interrupt mutex (g_interrupt_mutex), or
 * 2. already hold the single-process mutex (g_curproc_mutex).
 *
 * If neither of these conditions is true then the results
 * will be indeterminate.
 */
int
istate()
{
	/* If interrupts aren't disabled then they are enabled */
 	return pthread_getspecific(g_interrupts_disabled) == NULL;
}

void
restore(int state)
{
	state ? enable() : disable();
}

/* Prevent all interrupt threads from interacting with NOS */
int
disable()
{
	int was_enabled;

	was_enabled = istate();
	if (was_enabled) {
		pthread_mutex_lock(&g_interrupt_mutex);
		pthread_setspecific(g_interrupts_disabled, (const void *)1);
	}
	return was_enabled;
}

/* Allow interrupt threads to interact with NOS */
int
enable()
{
	int was_enabled;

	was_enabled = istate();
	if (!was_enabled) {
		pthread_setspecific(g_interrupts_disabled, NULL);
		pthread_mutex_unlock(&g_interrupt_mutex);
	}
	return was_enabled;
}

void
interrupt_enter()
{
	pthread_mutex_lock(&g_interrupt_mutex);
	pthread_setspecific(g_interrupts_disabled, (const void *)1);
}

/*
 * This is a POSIX-specific routine for NOS drivers which wish to use
 * the pthread system more efficiently to communicate with their top
 * halves using the interrupt lock and a pthread condition variable.
 *
 * Caller MUST be holding the interrupt lock (via interrupt_enter).
 */
void
interrupt_cond_wait(pthread_cond_t *cond)
{
	pthread_setspecific(g_interrupts_disabled, (const void *)0);
	pthread_cond_wait(cond, &g_interrupt_mutex);
	pthread_setspecific(g_interrupts_disabled, (const void *)1);
}

void
interrupt_leave()
{
	pthread_setspecific(g_interrupts_disabled, NULL);
	if (g_proc_halted && Ksig.nentries > 0) {
		g_proc_halted = 0;
		pthread_cond_signal(&g_interrupt_cond);
	}
	pthread_mutex_unlock(&g_interrupt_mutex);
}

/*
 * Pause the current thread until an interrupt causes a wakeup
 * Caller must not hold the interrupt mutex, undefined results if not.
 */
void
giveup()
{
	assert(istate());
	pthread_mutex_lock(&g_interrupt_mutex);
	while (Ksig.nentries == 0) {
		g_proc_halted = 1;
		pthread_cond_wait(&g_interrupt_cond, &g_interrupt_mutex);
	}
	pthread_mutex_unlock(&g_interrupt_mutex);
}

/* Pause the current thread and wait until signaled to run again.
 *
 * The calling thread must be holding the g_curproc_mutex. If it isn't
 * then there's a risk that a wakeup signal sent to this process
 * will be missed and the system will deadlock.
 */
void
proc_sleep(struct proc *self)
{
	assert(self->flags.run == 1);
	self->flags.run = 0;
	while (self->flags.run == 0 && self->flags.exit == 0)
		pthread_cond_wait(&self->cond, &g_curproc_mutex);
	if (self->flags.exit) {
		pthread_mutex_unlock(&g_curproc_mutex);
		pthread_exit(NULL);
	}
	/* We have been woken up to run at this point */
	assert(Curproc == self);
}

/* Wake up another thread and cause it to start running again.
 *
 * The calling thread must be holding the g_curproc_mutex. If it isn't then
 * there's a risk that the wakeup event will be missed and the system
 * will deadlock.
 */
void
proc_wakeup(struct proc *other)
{
	other->flags.run = 1;
	pthread_cond_signal(&other->cond);
}

/* pthread entry point for a new process */
static void *
proc_entry(void *pptr)
{
	struct proc *self = (struct proc *)pptr;

	/* We need to acquire the single thread lock and wait for a start
	 * signal.
	 */
	pthread_mutex_lock(&g_curproc_mutex);
	while (self->flags.run == 0 && self->flags.exit == 0)
		pthread_cond_wait(&self->cond, &g_curproc_mutex);
	if (self->flags.exit)
		goto ExitBeforeStart;

	/* We're now the running process. Call the process function */
	assert(Curproc == self);
	self->pc(self->iarg, self->parg1, self->parg2);

	/* Process function has returned. We're done running. */
	killself();

	/* Not reached */
	return NULL;

ExitBeforeStart:
	/* We were asked to exit before getting a chance to start. Oh well */
	pthread_mutex_unlock(&g_curproc_mutex);
	return NULL;
}
