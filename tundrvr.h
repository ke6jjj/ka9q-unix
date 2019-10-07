#ifndef	_KA9Q_TUNDRVR_H
#define	_KA9Q_TUNDRVR_H

#ifdef UNIX
#define TUN_MAX	4

/* In tundrvr.c: */
int tun_attach(int argc, char *argv[], void *p);

#endif	/* UNIX */

#endif	/* _KA9Q_TUNDRVR_H */
