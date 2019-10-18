#ifndef	_KA9Q_PC_H
#define	_KA9Q_PC_H

#include "global.h"

#define	NSW	10	/* Number of stopwatch "memories" */

#define	KBIRQ	1	/* IRQ for PC keyboard */

struct stopwatch {
	long calls;
	uint maxval;
	uint minval;
	int32 totval;
};
extern struct stopwatch Sw[];
extern uint Intstk[];	/* Interrupt stack defined in pcgen.asm */
extern uint Stktop[];	/* Top of interrupt stack */
extern void (*Shutdown[])();	/* List of functions to call at shutdown */
extern int Mtasker;	/* Type of multitasker, if any */

/* In n8250.c: */
void asytimer(void);


/* In random.c: */
void rtype(uint c);

/* In scc.c: */
void scctimer(void);
void sccstop(void);

/* In pc.c: */
long bioscnt(void);
uint clockbits(void);
void clrbit(uint port,uint8 bits);
void ctick(int);
int freevect(uint irq);
int getmask(unsigned irq);
int intcontext(void);
void ioinit(int);
void iostop(void);
void kbint(int);
void kbsave(int c);
int kbread(void);
int maskoff(unsigned irq);
int maskon(unsigned irq);
void pctick(void);
void setbit(uint port,uint8 bits);
int setvect(uint irq, int chain, void (*func)(int),int arg);
void sysreset(void);
void systick(void);
void writebit(uint port,uint8 mask,int val);

/* In stopwatch.asm: */
void swstart(void);
uint stopval(void);

/* In sw.c: */
void swstop(int n);

#endif	/* _KA9Q_PC_H */
