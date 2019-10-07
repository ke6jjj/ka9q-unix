#ifndef	_KA9Q_ASY_H
#define	_KA9Q_ASY_H

#include "global.h"
#include "mbuf.h"
#include "iface.h"

/* If you increase this, you must add additional interrupt vector
 * hooks in asyvec.asm (if using PC COM ports)
 */
#define	ASY_MAX	6
#define	FPORT_MAX	1

struct asymode {
	char *name;
	int trigchar;
	int (*init)(struct iface *);
	int (*free)(struct iface *);
};
extern struct asymode Asymode[];

#ifdef UNIX
int asy_init(int dev,struct iface *ifp,const char *path,
	uint bufsize,int trigchar,long speed,int cts);
#else
/* In n8250.c: */
int asy_init(int dev,struct iface *ifp,int base,int irq,
	uint bufsize,int trigchar,long speed,int cts,int rlsd,int chain);
#endif
int32 asy_ioctl(struct iface *ifp,int cmd,int set,int32 val);
int asy_read(int dev,void *buf,unsigned short cnt);
int asy_open(char *name);
int asy_close(int dev);
int asy_speed(int dev,long bps);
int asy_send(int dev,struct mbuf **bpp);
int asy_stop(struct iface *ifp);
int asy_write(int dev,const void *buf,unsigned short cnt);
#ifndef UNIX
int get_rlsd_asy(int dev, int new_rlsd);
#endif
int get_asy(int dev);
void fp_stop(void);

#ifndef UNIX
/* In asyvec.asm: */
INTERRUPT asy0vec(void);
INTERRUPT asy1vec(void);
INTERRUPT asy2vec(void);
INTERRUPT asy3vec(void);
INTERRUPT asy4vec(void);
INTERRUPT asy5vec(void);

/* In fourport.asm: */
INTERRUPT fp0vec(void);
#endif

#endif	/* _KA9Q_ASY_H */
