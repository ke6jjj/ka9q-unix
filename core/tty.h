#ifndef	_KA9Q_TTY_H
#define	_KA9Q_TTY_H

#include "net/core/mbuf.h"
#include "core/session.h"

/* In ttydriv.c: */
int ttydriv(struct session *sp,uint8 c);

#endif /* _KA9Q_TTY_H */
