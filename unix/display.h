#ifndef	_DISPLAY_H
#define	_DISPLAY_H

#include "../top.h"

#ifndef UNIX
#error "This file should only be used for UNIX/POSIX builds"
#endif

#include "../global.h"
#include <curses.h>

#define	MAXARGS	5

struct display {
	uint32 cookie;	/* Magic cookie to detect bogus pointers */
#define	D_COOKIE	0xdeadbeef
#define	D_COOKIE_HEADLESS	0xdeadb00f
	int cols;	/* Screen width (both virtual and real) */
	int savcol;	/* Saved cursor column */

	int rows;	/* Virtual screen height (with scrollback buffer) */
	int viewrows;	/* Viewport screen height */
	int savrow;	/* Saved cursor row */

	int viewtop;	/* Offset to start of virtual screen in buffer */
	int padtop;	/* Offset to start of real screen in buffer */

	int argi;	/* index to current entry in arg[] */
	int arg[MAXARGS];	/* numerical args to ANSI sequence */

	uint8 fcolor;	/* Current foreground color for added text */
	uint8 bcolor;	/* Current background color for added text */
	enum {
		DISP_NORMAL,	/* No ANSI sequence in progress */
		DISP_ESCAPE,	/* ESC char seen */
		DISP_ARG	/* ESC[ seen */
	} state;	/* State of ANSI escape sequence FSM */
	struct {
		unsigned int scrollbk:1;	/* Scrollback is active */ 
	} flags;        /* Status flags */

	uint8 *tabstops;	/* Tab stops */

	WINDOW *window;	/* CURSES window hosting this display */
};

void unix_display_init(int headless);
void unix_display_keyboard_stop();
void unix_display_stop();

#endif /*_DISPLAY_H */
