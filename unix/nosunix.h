#ifndef	_KA9Q_UNIX_HARDWARE_H
#define	_KA9Q_UNIX_HARDWARE_H

#include "../global.h"
#include "../proc.h"

extern void (*Shutdown[])();	/* List of functions to call at shutdown */

/* In unix.c: */
void ioinit(int);
void iostop(void);
void sysreset(void);

/* In display_crs.c: */
int kbread(void);

/* In ksubr_unix.c */
void init_psetup(struct proc *);
void proc_sleep(struct proc *);
void proc_wakeup(struct proc *);
void interrupt_enter(void);
void interrupt_cond_wait(pthread_cond_t *cond);
void interrupt_leave(void);

#endif	/* _KA9Q_UNIX_HARDWARE_H */
