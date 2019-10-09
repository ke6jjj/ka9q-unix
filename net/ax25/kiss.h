#ifndef	_KA9Q_KISS_H
#define	_KA9Q_KISS_H

#include "../../mbuf.h"
#include "../../iface.h"

/* In kiss.c: */
int kiss_free(struct iface *ifp);
int kiss_raw(struct iface *iface,struct mbuf **data);
void kiss_recv(struct iface *iface,struct mbuf **bp);
int kiss_init(struct iface *ifp);
int32 kiss_ioctl(struct iface *iface,int cmd,int set,int32 val);
void kiss_recv(struct iface *iface,struct mbuf **bp);

#endif	/* _KISS_H */
