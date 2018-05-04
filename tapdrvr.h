#ifndef	_TAPDRVR_H
#define	_TAPDRVR_H

#include "pktdrvr.h"

#ifdef UNIX
#define TAP_MAX	4

/* In tapdrvr.c: */
int tap_attach(int argc, char *argv[], void *p);

#endif	/* UNIX */

#endif	/* _TAPDRVR_H */
