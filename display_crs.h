/* ANSI display emulation using the CURSES terminal library
 *
 * This code emulates the IBM ANSI terminal display internally and uses
 * the UNIX curses library to effect that emulation into the terminal
 * used by the user.
 *
 * Copyright 1992 Phil Karn, KA9Q
 * Copyright 2017 Jeremy Cooper, KE6JJJ
 *
 * To support the traditional NOS scrolling behavior each display in this
 * scheme is backed by a CURSES "pad". A pad is a virtual display buffer
 * which can be larger than the current terminal's screen size.
 *
 * This large buffer is divided into two regions: the "active" area and
 * the scrollback area. The active area is where all new input appears
 * as well as where all screen drawing commands take effect. That is to say,
 * any received escape sequence to move the cursor to row "0" will be
 * translated into movement to row "padtop". Likewise, screen clearing commands
 * only affect the rows in the "active" area.
 *
 *                   +================================+
 *                   |                                | <-- TOP of pad (row 0)
 *                   |           SCROLLBACK           |
 *                  ...                              ...
 *    viewtop row -> |                                |
 *          |        |             BUFFER             |
 * viewrows(+)       |                                |
 *          |        +--------------------------------+
 *          |        |                                | <-- padtop row
 *          \------> |             ACTIVE             |       |
 *                   |                                |      (+)-- viewrows
 *                   |              AREA              |       |
 *                   |                                | <-- rows (size)
 *                   +--------------------------------+
 *
 * The variables "viewtop" and "viewrows" keep track of the current viewing
 * region. During normal operation "viewtop" is set to "padtop" and the user
 * sees only the active area of the display. When scrolling back, however,
 * "viewtop" is moved up or down, allowing the user to see the scrollback
 * buffer. (Or a mixture of the scrollback buffer and the top lines of the
 * active area).
 *
 * Since each active NOS session allocates its own display and each display
 * allocates its own CURSES pad, multiple pads may be active at once. Only
 * one pad, the pad belonging to the "Current" display, will be actively
 * mapped to the terminal screen, however.
 *
 * KEYBOARD INPUT
 *
 * CURSES is intimately tied into the keyboard input and interpretation
 * process, so all NOS keyboard input is also handled in this translation
 * unit as well. At system startup, a thread is created which safely
 * queries (and blocks) on input until a keypress is received and translated
 * through CURSES. Once ready, the key is placed into a keyboard FIFO and
 * an "interrupt" is generated in NOS, allowing the NOS keyboard thread in
 * main.c to receive the keypress and pass it to its proper destination
 * process.
 */
#ifndef	_DISPLAY_CRS_H
#define	_DISPLAY_CRS_H

#include "top.h"

#ifndef UNIX
#error "This file should only be used for UNIX/POSIX builds"
#endif

#ifndef	_STDIO_H
#include "stdio.h"
#endif

#include <curses.h>
#include "nosunix.h"

#define	MAXARGS	5

struct display {
	uint32 cookie;	/* Magic cookie to detect bogus pointers */
#define	D_COOKIE	0xdeadbeef
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

#define	COLS	80
#define	ROWS	25

int curses_display_start();
int curses_display_stop();
int curses_keyboard_start();
int curses_keyboard_stop();

#endif /*_DISPLAY_CRS_H */
