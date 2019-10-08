/* Low level socket routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "errno.h"
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "usock.h"

/* Convert a socket (address + port) to an ascii string of the form
 * aaa.aaa.aaa.aaa:ppppp
 */
char *
psocket(p)
void *p;
{
	struct ksockaddr *sp;	/* Pointer to structure to decode */

	sp = (struct ksockaddr *)p;
	if(sp->sa_family < kAF_INET || sp->sa_family >= NAF)
		return NULL;

	return (*Psock[sp->sa_family])(sp);
}

/* Return ASCII string giving reason for connection closing */
char *
sockerr(s)
int s;	/* Socket index */
{
	register struct usock *up;
	struct socklink *sp;

	if((up = itop(s)) == NULL){
		kerrno = kEBADF;
		return Badsocket;
	}
	sp = up->sp;
	if(sp->error != NULL){
		return sp->error[up->errcodes[0]];
	} else {
		kerrno = kEOPNOTSUPP;	/* not yet, anyway */
		return NULL;
	}
}
/* Get state of protocol. Valid only for connection-oriented sockets. */
char *
sockstate(s)
int s;		/* Socket index */
{
	register struct usock *up;
	struct socklink *sp;

	if((up = itop(s)) == NULL){
		kerrno = kEBADF;
		return NULL;
	}
	if(up->cb.p == NULL){
		kerrno = kENOTCONN;
		return NULL;
	}
	sp = up->sp;	
	if(sp->state != NULL)
		return (*sp->state)(up);
	
	/* Datagram sockets don't have state */
	kerrno = kEOPNOTSUPP;
	return NULL;
}
/* Convert a socket index to an internal user socket structure pointer */
struct usock *
itop(s)
register int s;	/* Socket index */
{
	if(s < 0 || _fd_type(s) != _FL_SOCK)
		return NULL;	/* Valid only for sockets */
	s = _fd_seq(s);
	if(s >= Nsock)
		return NULL;

	return Usock[s];
}

void
st_garbage(red)
int red;
{
	int i;
	struct usock *up;

	for(i=0;i<Nsock;i++){
		up = Usock[i];
		if(up != NULL && up->type == TYPE_LOCAL_STREAM)
			mbuf_crunch(&up->cb.local->q);
	}
}

