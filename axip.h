#ifndef	_AXIP_H
#define	_AXIP_H

#ifdef AXIP
#define AXUDP_MAX	4

/* In axip.c: */
int axudp_attach(int argc, char *argv[], void *p);
int doaxudp(int argc, char *argv[], void *p);

#endif	/* AXIP */

#endif	/* _AXIP_H */
