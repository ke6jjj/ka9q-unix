/* TTY input line editing
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "top.h"

#include "lib/std/stdio.h"
#ifdef MSDOS
#include <conio.h>
#endif
#include "global.h"
#include "net/core/mbuf.h"
#include "core/session.h"
#include "core/tty.h"
#include "core/socket.h"

#define	OFF	0
#define	ON	1

#define	LINESIZE	256

/* Accept characters from the incoming tty buffer and process them
 * (if in cooked mode) or just pass them directly (if in raw mode).
 *
 * Echoing (if enabled) is direct to the raw terminal. This requires
 * recording (if enabled) of locally typed info to be done by the session
 * itself so that edited output instead of raw input is recorded.
 *
 * Returns the number of cooked characters ready to be read from the buffer.
 */
int
ttydriv(
  struct session *sp,
  uint8 c
)
{
	int rval;
	register struct ttystate *ttyp = &sp->ttystate;

	if(ttyp->line == NULL){
		/* First-time initialization */
		ttyp->lp = ttyp->line = calloc(1,LINESIZE);
	}
	switch(ttyp->edit){
	case OFF:
		/* Editing is off; add character to buffer
		 * and return the number of characters in it (probably 1)
		 */
		*ttyp->lp++ = c;
		if(ttyp->echo)
			kfputc(c,Current->output);
		rval = ttyp->lp - ttyp->line;
		ttyp->lp = ttyp->line;
		return rval;
	case ON:
		/* Perform cooked-mode line editing */
		switch(c){
		case '\r':	/* CR and LF both terminate the line */
		case '\n':
			if(ttyp->crnl)
				*ttyp->lp++ = '\n';
			else
				*ttyp->lp++ = c;
			if(ttyp->echo)
				kputc('\n',Current->output);
			rval = ttyp->lp - ttyp->line;
			ttyp->lp = ttyp->line;
			return rval;
		case DEL:
		case '\b':	/* Character delete */
			if(ttyp->lp != ttyp->line){
				ttyp->lp--;
				if(ttyp->echo)
					kfputs("\b \b",Current->output);
			}
			break;
		case CTLR:	/* print line buffer */
			if(ttyp->echo){
				kfprintf(Current->output,"^R\n");
				kfwrite(ttyp->line,1,ttyp->lp-ttyp->line,
				 Current->output);
			}
			break;
		case CTLU:	/* Line kill */
			while(ttyp->echo && ttyp->lp-- != ttyp->line){
				kfputs("\b \b",Current->output);
			}
			ttyp->lp = ttyp->line;
			break;
		default:	/* Ordinary character */
			*ttyp->lp++ = c;

			/* ^Z apparently hangs the terminal emulators under
			 * DoubleDos and Desqview. I REALLY HATE having to patch
			 * around other people's bugs like this!!!
			 */
			if(ttyp->echo &&
#ifndef	AMIGA
			 c != CTLZ &&
#endif
			 ttyp->lp - ttyp->line < LINESIZE-1){
				kputc(c,Current->output);

			} else if(ttyp->lp - ttyp->line >= LINESIZE-1){
				kputc('\007',Current->output);	/* Beep */
				ttyp->lp--;
			}
			break;
		}
		break;
	}
	return 0;
}
