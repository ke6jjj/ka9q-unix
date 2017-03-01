/* Higher level user subroutines built on top of the ksocket primitives
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "global.h"
#include <stdarg.h>
#include "errno.h"
#include "mbuf.h"
#include "proc.h"
#include "socket.h"
#include "usock.h"
#include "session.h"
#include "nr4.h"


/* Higher-level receive routine, intended for connection-oriented sockets.
 * Can be used with datagram sockets, although the sender id is lost.
 */
int
krecv(s,buf,len,flags)
int s;		/* Socket index */
void *buf;	/* User buffer */
int len;	/* Max length to receive */
int flags;	/* Unused; will eventually select oob data, etc */
{
	struct mbuf *bp;
	int cnt;

	if(len == 0)
		return 0;	/* Otherwise would be interp as "all" */

	cnt = recv_mbuf(s,&bp,flags,NULL,(int *)NULL);
	if(cnt > 0){
		cnt = min(cnt,len);
		pullup(&bp,buf,cnt);
		free_p(&bp);
	}
	return cnt;
}
/* Higher level receive routine, intended for datagram sockets. Can also
 * be used for connection-oriented sockets, although from and fromlen are
 * ignored.
 */
int
krecvfrom(s,buf,len,flags,from,fromlen)
int s;			/* Socket index */
void *buf;		/* User buffer */
int len;		/* Maximum length */
int flags;		/* Unused; will eventually select oob data, etc */
struct ksockaddr *from;	/* Source address, only for datagrams */
int *fromlen;		/* Length of source address */
{
	struct mbuf *bp;
	register int cnt;

	cnt = recv_mbuf(s,&bp,flags,from,fromlen);
	if(cnt > 0){
		cnt = min(cnt,len);
		pullup(&bp,buf,cnt);
		free_p(&bp);
	}
	return cnt;
}
/* High level send routine */
int
ksend(s,buf,len,flags)
int s;		/* Socket index */
const void *buf;	/* User buffer */
int len;	/* Length of buffer */
int flags;	/* Unused; will eventually select oob data, etc */
{
	struct mbuf *bp;
	struct ksockaddr sock;
	int i = MAXSOCKSIZE;

	if(kgetpeername(s,&sock,&i) == -1)
		return -1;
	bp = qdata(buf,len);
	return send_mbuf(s,&bp,flags,&sock,i);
}
/* High level send routine, intended for datagram sockets. Can be used on
 * connection-oriented sockets, but "to" and "tolen" are ignored.
 */
int
ksendto(
int s,			/* Socket index */
void *buf,		/* User buffer */
int len,		/* Length of buffer */
int flags,		/* Unused; will eventually select oob data, etc */
struct ksockaddr *to,	/* Destination, only for datagrams */
int tolen		/* Length of destination */
){
	struct mbuf *bp;

	bp = qdata(buf,len);
	return send_mbuf(s,&bp,flags,to,tolen);
}
