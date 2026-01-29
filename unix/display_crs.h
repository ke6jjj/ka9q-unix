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

#include "../top.h"

#include "unix/display.h"

#ifndef UNIX
#error "This file should only be used for UNIX/POSIX builds"
#endif

int curses_display_start();
int curses_display_stop();
int curses_keyboard_start();
int curses_keyboard_stop();

struct display * newdisplay_curses(int rows, int cols, int noscrol, int size,
	int *do_buffered, const char **desired_eol);
void closedisplay_curses(struct display *dp);
void displaywrite_curses(struct display *dp, const void *buf, int cnt);
void dupdate_curses(struct display *dp);
void dscrollmode_curses(struct display *dp, int flag);
void dhome_curses(struct display *dp);
void dend_curses(struct display *dp);
void dpgup_curses(struct display *dp);
void dpgdown_curses(struct display *dp);
void dcursup_curses(struct display *dp);
void dcursdown_curses(struct display *dp);
int kbread_curses(void);

#endif /*_DISPLAY_CRS_H */
