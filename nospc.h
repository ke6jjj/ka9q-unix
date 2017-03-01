#ifndef	_PC_H
#define	_PC_H
#define _HARDWARE_H

#ifndef	_GLOBAL_H
#include "global.h"
#endif

#define	NSW	10	/* Number of stopwatch "memories" */

#define	KBIRQ	1	/* IRQ for PC keyboard */

/* Extended keyboard codes for function keys */
#define	F1	59	/* Function key 1 */
#define	F2	60
#define	F3	61
#define	F4	62
#define	F5	63
#define	F6	64
#define	F7	65
#define	F8	66
#define	F9	67
#define	F10	68

#define	CURSHOM	71	/* Home key */
#define	CURSUP	72	/* Up arrow key */
#define	PAGEUP	73	/* Page up key */
#define	CURSLEFT 75	/* Cursor left key */
#define CURSRIGHT 77	/* Cursor right key */	
#define	CURSEND	79	/* END key */
#define	CURSDWN	80	/* Down arrow key */
#define	PAGEDWN	81	/* Page down key */

#define	AF1	104	/* ALT-F1 */
#define	AF2	105
#define	AF3	106
#define	AF4	107
#define	AF5	108
#define	AF6	109
#define	AF7	110
#define	AF8	111
#define	AF9	112
#define	AF10	113
#define	AF11	139
#define	AF12	140

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

#endif	/* _PC_H */

