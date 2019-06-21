/* OS- and machine-dependent stuff for IBM-PC running MS-DOS and Turbo-C
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "stdio.h"
#include <conio.h>
#include <dir.h>
#include <dos.h>
#include <sys/stat.h>
#include <string.h>
#include <process.h>
#include <fcntl.h>
/*#include <alloc.h> */
#include <stdarg.h>
#include <bios.h>
#include <time.h>
#include <dpmi.h>
#include <signal.h>
#include <crt0.h>
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#include "internet.h"
#include "session.h"
#include "socket.h"
#include "usock.h"
#include "cmdparse.h"
#include "nospc.h"
#include "display.h"
#include "display_pc.h"

static void statline(struct display *dp,struct session *sp);

int _go32_dpmi_unchain_protected_mode_interrupt_vector(uint irq,_go32_dpmi_seginfo *info);
void eoi(void);

extern int Curdisp;
extern struct proc *Display;
volatile int Tick;
volatile int32 Clock;

static int saved_break;

struct int_tab {
	__dpmi_paddr old;	/* Previous handler at this vector */
	_go32_dpmi_seginfo new;	/* Current handler, with wrapper info */
	void (*func)(int);	/* Function to call on interrupt */
	int arg;		/* Arg to pass to interrupt function */
	int chain;		/* Is interrupt chained to old handler? */
} Int_tab[16];


/* Called at startup time to set up misc I/O related functions */
void
ioinit(int hinit)
{
	union REGS inregs;
	extern int _fmode;

	_fmode = O_BINARY;
	/* Get some memory on the heap so interrupt calls to malloc
	 * won't fail unnecessarily
	 */
	free(malloc(hinit));

	/* Increase the size of the file table.
	 * Note: this causes MS-DOS
	 * to allocate a block of memory to hold the larger file table.
	 * By default, this happens right after our program, which means
	 * any further sbrk() calls from morecore (called from malloc)
	 * will fail. Hence there is now code in alloc.c that can call
	 * the MS-DOS allocmem() function to grab additional MS-DOS
	 * memory blocks that are not contiguous with the program and
	 * put them on the heap.
	 */
	inregs.h.ah = 0x67;
	inregs.x.bx = Nfiles;	/* Up to the base of the socket numbers */
	intdos(&inregs,&inregs);	

	saved_break = getcbrk();
	setcbrk(0);
	signal(SIGINT,SIG_IGN);

	/* Chain protected mode keyboard interrupt */
	setvect(1,1,kbint,0);

	/* Chain timer interrupt */
	setvect(0,1,ctick,0);
}
/* Called just before exiting to restore console state */
void
iostop(void)
{
	struct iface *ifp,*iftmp;
	void (**fp)(void);

	setcbrk(saved_break);

	for(ifp = Ifaces;ifp != NULL;ifp = iftmp){
		iftmp = ifp->next;
		if_detach(ifp);
	}
	/* Call list of shutdown functions */
	for(fp = Shutdown;*fp != NULL;fp++){
		(**fp)();
	}
	kfcloseall();
#ifndef	notdef	/* Can't unchain interrupts with current DJGPP lib */
	/* Restore previous timer and keyboard interrupts */
	freevect(0);
	freevect(1);
#endif
}

/* Read characters from the keyboard, translating them to "real" ASCII.
 * If none are ready, block. The special keys are translated to values
 * above 256, e.g., F-10 is 256 + 68 = 324.
 */
int
kbread(void)
{
	uint c;

	while((c = kbraw()) == 0)
		kwait(&kbint);

	rtype(c);	/* Randomize random number state */

	/* Convert "extended ascii" to something more standard */
	if((c & 0xff) != 0)
		return c & 0xff;
	c >>= 8;
	switch(c){
	case 3:		/* NULL (bizzare!) */
		c = 0;
		break;
	case 83:	/* DEL key */
		c = DEL;
		break;
	default:	/* Special key */
		c += 256;
	}
	return c;
}
/* Disable hardware interrupt */
int
maskoff(uint irq)
{
	if(irq < 8){
		setbit(0x21,(char)(1<<irq));
	} else if(irq < 16){
		irq -= 8;
		setbit(0xa1,(char)(1<<irq));
	} else {
		return -1;
	}
	return 0;
}
/* Enable hardware interrupt */
int
maskon(uint irq)
 {
	if(irq < 8){
		clrbit(0x21,1<<irq);
	} else if(irq < 16){
		irq -= 8;
		clrbit(0xa1,1<<irq);
	} else {
		return -1;
	}
	return 0;
}
/* Return 1 if specified interrupt is enabled, 0 if not, -1 if invalid */
int
getmask(unsigned irq)
{
	if(irq < 8)
		return (inportb(0x21) & (1 << irq)) ? 0 : 1;
	else if(irq < 16){
		irq -= 8;
		return (inportb(0xa1) & (1 << irq)) ? 0 : 1;
	} else
		return -1;
}
/* Called from assembler stub linked to BIOS interrupt 1C, called on each
 * hardware clock tick. Signal a clock tick to the timer process.
 */
void
ctick(int unused)
{
	Tick++;
	Clock++;
	ksignal((void *)&Tick,1);
}
/* Read the Clock global variable, with interrupts off to avoid possible
 * inconsistency on 16-bit machines
 */
int32
rdclock(void)
{
	int i_state;
	int32 rval;

	i_state = disable();
	rval = Clock;
	restore(i_state);
	return rval;
}

/* Called from the timer process on every tick. NOTE! This function
 * can NOT be called at interrupt time because it calls the BIOS
 */
void
pctick(void)
{
	long t;
	static long oldt;	/* Value of bioscnt() on last call */

	/* Check for day change */
	t = bioscnt();
	if(t < oldt){
		/* Call the regular DOS time func to handle the midnight flag */
		(void)time(NULL);
	}
}

/* Set bit(s) in I/O port */
void
setbit(uint port,uint8 bits)
{
	outportb(port,inportb(port)|bits);
}
/* Clear bit(s) in I/O port */
void
clrbit(uint port,uint8 bits)
{
	outportb(port,inportb(port) & ~bits);
}
/* Set or clear selected bits(s) in I/O port */
void
writebit(
uint port,
uint8 mask,
int val
){
	uint8 x;

	x = inportb(port);
	if(val)
		x |= mask;
	else
		x &= ~mask;
	outportb(port,x);
}

void
display(int i,void *v1,void *v2)
{
	struct session *sp;
	struct display *dp;
	static struct display *lastdp;
	static long lastkdebug;

	for(;;){
		sp = Current;
		if(sp == NULL || sp->output == NULL
		 || (dp = (struct display *)sp->output->ptr) == NULL){
			/* Something weird happened */
			ppause(500L);
			continue;
		}
		if(dp != lastdp || Kdebug != lastkdebug)
			dp->flags.dirty_screen = 1;
		statline(dp,sp);
		dupdate(dp);
		lastdp = dp;
		lastkdebug = Kdebug;
		kalarm(100L);	/* Poll status every 100 ms */
		kwait(dp);
		kalarm(0L);
	}
}

/* Compose status line for bottom of screen */
static void
statline(struct display *dp,struct session *sp)
{
	int attr;
	char buf[81];
	struct text_info text_info;
	int unack;
	int s1;
	int s = -1;

	/* Determine attribute to use */
	gettextinfo(&text_info);
	if(text_info.currmode == MONO)
		attr = 0x07;	/* Regular white on black */
	else
		attr = 0x02;	/* Green on black */

	if(sp->network != NULL && (s = kfileno(sp->network)) != -1){
		unack = socklen(s,1);
		if(sp->type == FTP && (s1 = kfileno(sp->cb.ftp->data)) != -1)
			unack += socklen(s1,1);
	}
	memset(buf,' ',80);
	buf[80] = '\0';
	sprintf(&buf[0],"%2d: %s",sp->index,sp->name);

	if(dp->flags.scrollbk){
		int off;
		off = dp->virttop - dp->realtop;
		if(off < 0)
			off += dp->size;
		sprintf(&buf[54],"Scroll:%-5u",off);
	} else if(s != -1 && unack != 0){
		sprintf(&buf[54],"Unack: %-5u",unack);
	} else
		sprintf(&buf[54],"            ");
	sprintf(&buf[66],"F8:nxt F10:cmd");
	statwrite(0,buf,sizeof(buf)-1,attr);
}

/* Return time since startup in milliseconds. Resolution is improved
 * below 55 ms (the clock tick interval) by reading back the instantaneous
 * 8254 counter value and combining it with the global clock tick counter.
 *
 * Reading the 8254 is a bit tricky since a tick could occur asynchronously
 * between the two reads. The tick counter is examined before and after the
 * hardware counter is read. If the tick counter changes, try again.
 * Note: the hardware counter counts down from 65536.
 */
int32
msclock(void)
{
	int32 hi;
	uint lo;
	uint64 x;

	do {
		hi = rdclock();
		lo = clockbits();
	} while(hi != rdclock());

	x = ((uint64)hi << 16) - lo;
	return (x * 11) / 13125;
}
/* Return clock in seconds */
int32
secclock(void)
{
	int32 hi;
	uint lo;
	uint64 x;

	do {
		hi = rdclock();
		lo = clockbits();
	} while(hi != rdclock());

	x = ((uint64)hi << 16) - lo;
	return (x * 11) / 13125000;
}
/* Return time in raw clock counts, approx 838 ns */
int32
usclock(void)
{
	int32 hi;
	uint lo;

	do {
		hi = rdclock();
		lo = clockbits();
	} while(hi != rdclock());

	return (hi << 16) - (int32)lo;
}

/* Directly read BIOS count of time ticks. This is used instead of
 * calling biostime(0,0L). The latter calls BIOS INT 1A, AH=0,
 * which resets the midnight overflow flag, losing days on the clock.
 */
long
bioscnt(void)
{
	long rval;
	int i_state;

	i_state = disable();
	dosmemget(0x46c,sizeof(rval),&rval);
	restore(i_state);
	return rval;
}

/* Atomic read-and-decrement operation.
 * Read the variable pointed to by p. If it is
 * non-zero, decrement it. Return the original value.
 */
int
arddec(p)
volatile int *p;
{
	int tmp;
	int i_state;

	i_state = disable();
	tmp = *p;
	if(tmp != 0)
		(*p)--;
	restore(i_state);
	return tmp;
}
void
restore(int state)
{
	state ? enable() : disable();
}
int
istate(void)
{
  long flags;
  asm ("pushfl\n\t"		/* We save the old ccr, which has interrupt mask bit. */
       "popl %0\n\t" : "=r" (flags));
  return (flags >> 9) & 1;
}

/* This function is called by exit() in the GCC libc. We define
 * it here to supersede the one defined in libc's stdio
 */
void
_cleanup(void)
{
	kfcloseall();
}

/* clockbits - Read low order bits of timer 0 (the TOD clock)
 * This works only for the 8254 chips used in ATs and 386s.
 *
 * The timer runs in mode 3 (square wave mode), counting down
 * by twos, twice for each cycle. So it is necessary to read back the
 * OUTPUT pin to see which half of the cycle we're in. I.e., the OUTPUT
 * pin forms the most significant bit of the count. Unfortunately,
 * the 8253 in the PC/XT lacks a command to read the OUTPUT pin...
 *
 * The PC's clock design is soooo brain damaged...
 */
uint
clockbits(void)
{
	int i_state;
	unsigned int stat,count;

	do {
		i_state = disable();
		outportb(0x43,0xc2);	/* latch timer 0 count and status for reading */
		stat = inportb(0x40);	/* get status of timer 0 */
		count = inportb(0x40);	/* lsb of count */
		count |= inportb(0x40) << 8;	/* msb of count */
		restore(i_state);	/* no more chip references */
	} while(stat & 0x40);		/* reread if NULL COUNT bit set */
	stat = (stat & 0x80) << 8;	/* Shift OUTPUT to msb of 16-bit word */
	count >>= 1;			/* count /= 2 */
	if(count == 0)
		return stat ^ 0x8000;	/* return complement of OUTPUT bit */
	else
		return count | stat;	/* Combine OUTPUT with counter */
}

void
kbint(int unused)
{
	ksignal(&kbint,1);
}

void
giveup(void)
{
}
void
sysreset(void)
{
}

uint
lcsum(uint16 *buf,uint cnt)
{
	uint32 sum = 0;

	while(cnt-- != 0)
		sum += *buf++;
	while(sum > 65535)
		sum = (sum & 0xffff) + (sum >> 16);
	return ((sum >> 8) | (sum << 8)) & 0xffff;
}

/* What a crock. All this inelegance should be replaced with something
 * that figures out what interrupt is being serviced by reading the 8259.
 */
static void irq0(void)
{
	eoi();
	(*Int_tab[0].func)(Int_tab[0].arg);
}
static void irq1(void)
{
	eoi();
	(*Int_tab[1].func)(Int_tab[1].arg);
}
static void irq2(void)
{
	eoi();
	(*Int_tab[2].func)(Int_tab[2].arg);
}
static void irq3(void)
{
	eoi();
	(*Int_tab[3].func)(Int_tab[3].arg);
}
int Irq4s;
static void irq4(void)
{
	disable();
	Irq4s++;
	eoi();
	(*Int_tab[4].func)(Int_tab[4].arg);
	enable();
}
static void irq5(void)
{
	eoi();
	(*Int_tab[5].func)(Int_tab[5].arg);
}
static void irq6(void)
{
	eoi();
	(*Int_tab[6].func)(Int_tab[6].arg);
}
static void irq7(void)
{
	eoi();
	(*Int_tab[7].func)(Int_tab[7].arg);
}
static void irq8(void)
{
	eoi();
	(*Int_tab[8].func)(Int_tab[8].arg);
}
static void irq9(void)
{
	eoi();
	(*Int_tab[9].func)(Int_tab[9].arg);
}
static void irq10(void)
{
	eoi();
	(*Int_tab[10].func)(Int_tab[10].arg);
}
static void irq11(void)
{
	eoi();
	(*Int_tab[11].func)(Int_tab[11].arg);
}
static void irq12(void)
{
	eoi();
	(*Int_tab[12].func)(Int_tab[12].arg);
}
static void irq13(void)
{
	eoi();
	(*Int_tab[13].func)(Int_tab[13].arg);
}
static void irq14(void)
{
	eoi();
	(*Int_tab[14].func)(Int_tab[14].arg);
}
static void irq15(void)
{
	eoi();
	(*Int_tab[15].func)(Int_tab[15].arg);
}

static void (*Vectab[16])(void) = {
	irq0,irq1,irq2,irq3,irq4,irq5,irq6,irq7,irq8,irq9,irq10,irq11,
	irq12,irq13,irq14,irq15
};

int
setvect(uint irq, int chain, void (*func)(int),int arg)
{
	struct int_tab *ip;
	uint intno;
	int i;

	if(irq > 15)
		return -1;	/* IRQ out of legal range */

	ip = &Int_tab[irq];
	if(ip->func != NULL)
		return -1;	/* Already in use */
	/* Convert irq to actual CPU interrupt vector */
	intno = (irq < 8) ? irq + 8 : 0x70 + irq - 8;

	__dpmi_get_protected_mode_interrupt_vector(intno,&ip->old);
	ip->func = func;
	ip->arg = arg;
	ip->new.pm_offset = (int)Vectab[irq];
	ip->new.pm_selector = _go32_my_cs();
	ip->chain = chain;

	if(chain)
		return _go32_dpmi_chain_protected_mode_interrupt_vector(intno,&ip->new);

	if(i =_go32_dpmi_allocate_iret_wrapper(&ip->new))
		return i;
	return _go32_dpmi_set_protected_mode_interrupt_vector(intno,&ip->new);
}

int
freevect(uint irq)
{
	struct int_tab *ip;
	int i;

	if(irq > 15)
		return -1;	/* IRQ out of legal range */

	ip = &Int_tab[irq];
	ip->func = NULL;
	/* Convert irq to actual CPU interrupt vector */
	irq = (irq < 8) ? irq + 8 : 0x70 + irq - 8;
	if(ip->chain)
		return _go32_dpmi_unchain_protected_mode_interrupt_vector(irq,&ip->new);
	if(i = __dpmi_set_protected_mode_interrupt_vector(irq,&ip->old))
		return i;
	return _go32_dpmi_free_iret_wrapper(&ip->new);
}
/* Written to extend gopint.c in djgpp library */
int
_go32_dpmi_unchain_protected_mode_interrupt_vector(uint irq,_go32_dpmi_seginfo *info)
{
  __dpmi_paddr v;
  char *stack;
  char *wrapper;

  __dpmi_get_protected_mode_interrupt_vector(irq,&v);
  /* Sanity check: does the vector point into our program? A bug in gdb
   * keeps us from hooking the keyboard interrupt when we run under its
   * control. This test catches it.
   */
  if(v.selector != _go32_my_cs())
	return -1;
  wrapper = (char *)v.offset32;
  /* Extract previous vector from the wrapper chainback area */
  v.offset32 = *(long *)(wrapper + 0x5b);
  v.selector = *(short *)(wrapper + 0x5f);
  /* Extract stack base from address of _call_count variable in wrapper */
  stack = (char *)(*(long *)(wrapper+0x0F) - 8);
#define	STACK_WAS_MALLOCED	(1 << 0)

  if (*(long *) stack & STACK_WAS_MALLOCED)
      free(stack);
  free(wrapper);
  __dpmi_set_protected_mode_interrupt_vector(irq,&v);
  return 0;
}
/* Re-arm 8259 interrupt controller(s)
 * Should be called just after taking an interrupt, instead of just
 * before returning. This is because the 8259 inputs are edge triggered, and
 * new interrupts arriving during an interrupt service routine might be missed.
 */
void
eoi(void)
{
	/* read in-service register from secondary 8259 */
	outportb(0xa0,0x0b);
	if(inportb(0xa0))
		outportb(0xa0,0x20);	/* Send EOI to secondary 8259 */
	outportb(0x20,0x20);	/* Send EOI to primary 8259 */
}

/* Very dangerous function */
void *
htop(const char *s)
{
	void *r;
	if (sscanf(s, "%p", &r) != 1)
		/* Pointer didn't parse. Don't let it point randomly */
		r = "BAD POINTER";

	return r;
}
