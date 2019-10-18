/* TCP timeout routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "lib/std/stdio.h"
#include "global.h"
#include "net/core/mbuf.h"
#include "core/timer.h"

#include "lib/inet/netuser.h"

#include "net/inet/internet.h"
#include "net/inet/tcp.h"

/* Timer timeout */
void
tcp_timeout(void *p)
{
	struct tcb *tcb;
	int32 ptrsave;

	tcb = p;
	if(tcb == NULL)
		return;

	/* Make sure the timer has stopped (we might have been kicked) */
	stop_timer(&tcb->timer);

	switch(tcb->state){
	case TCP_TIME_WAIT:	/* 2MSL timer has expired */
		close_self(tcb,NORMAL);
		break;
	default:		/* Retransmission timer has expired */
		tcb->timeouts++;
		tcb->flags.retran = 1;	/* Indicate > 1  transmission */
		tcb->backoff++;
		/* Reduce slowstart threshold to half current window */
		tcb->ssthresh = tcb->cwind / 2;
		tcb->ssthresh = max(tcb->ssthresh,tcb->mss);
		/* Shrink congestion window to 1 packet */
		tcb->cwind = tcb->mss;
		/* Retransmit just the oldest unacked packet */
		ptrsave = tcb->snd.ptr;
		tcb->snd.ptr = tcb->snd.una;
		tcp_output(tcb);
		tcb->snd.ptr = ptrsave;
	}
}
/* Backoff function - the subject of much research */
int32
backoff(int n)
{
	if(n > 31)
		n = 31;	/* Prevent truncation to zero */

	return 1L << n;	/* Binary exponential back off */
}

