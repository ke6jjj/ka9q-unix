/* ANSI display emulation using the CURSES terminal library
 *
 * This file emulates the IBM ANSI terminal display internally and uses
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
#include "top.h"

#include <sys/ioctl.h>
#include <pthread.h>
#include <curses.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "errno.h"
#include "global.h"
#include "display.h"
#include "display_crs.h"
#include "nosunix.h"	/* For keyboard character definitions */
#include "proc.h"

#define	DCOL	67
#define	DSIZ	(81-DCOL)
#define KEYFIFO_SIZE	64

static void desc(struct display *dp,uint8 c);
static void darg(struct display *dp,uint8 c);
static void dchar(struct display *dp,uint8 c);
static void dattrib(struct display *dp,int val);
static void scroll_adjust(struct display *dp,int lines);
static void scroll_set(struct display *dp,int topline);
static void cursor_adjust(struct display *dp, int rowadj, int coladj);
static void cursor_set(struct display *dp, int row, int col);
static short pc2ncurses(int color);
static void *kbread_proc(void *dummy);
static void kb_add(int c);

extern struct proc *Display;

/* Current terminal supports (or doesn't) colors */
static int crs_has_colors;

/* The current terminal window size (queried at startup) */
struct winsize g_ws;

/* CURSES isn't specifically multi-thread safe */
static pthread_mutex_t g_curses_mutex;

/* The keyboard reading thread */
static pthread_t g_keyboard_thread;

/* The keyboard FIFO */
static struct {
	const int *rp;
	int       *wp;
	int       *buf;
	size_t    sz;
	size_t    cnt;
} key_fifo;

/*
 * These are thread-safe calls into CURSES
 */
#define LCKC	pthread_mutex_lock(&g_curses_mutex)
#define ULCKC	pthread_mutex_unlock(&g_curses_mutex)

#define WMOVE(win, y, x) { \
	LCKC; wmove((win), (y), (x)); ULCKC; \
	}

#define DELWIN(win) { \
	LCKC; delwin(dp->window); ULCKC; \
	}

#define GETYX(win,y,x) { \
	LCKC; getyx((win),(y),(x)); ULCKC; \
	}

#define WINSCH(win, ch) { \
	LCKC; winsch((win), (ch)); ULCKC; \
	}

#define WCLRTOBOT(win) { \
	LCKC; wclrtobot((win)); ULCKC; \
	}

#define WCLRTOEOL(win) { \
	LCKC; wclrtoeol((win)); ULCKC; \
	}

#define WINSERTLN(win) { \
	LCKC; winsertln((win)); ULCKC; \
	}

#define WDELETELN(win) { \
	LCKC; wdeleteln((win)); ULCKC; \
	}

#define WDELCH(win) { \
	LCKC; wdelch((win)); ULCKC; \
	}

#define WCOLOR_SET(win, pair, opts) { \
	LCKC; wcolor_set((win), (pair), (opts)); ULCKC; \
	}

#define WATTRSET(win, attr) { \
	LCKC; wattrset((win), (attr)); ULCKC; \
	}

#define WATTRON(win, attr) { \
	LCKC; wattron((win), (attr)); ULCKC; \
	}

#define WADDCH(win, ch) { \
	LCKC; waddch((win), (ch)); ULCKC; \
	}

/* Given a PC foreground and background color, yield the CURSES color index */
#define pcPAIR(fgnd, bgnd) ((fgnd) + ((bgnd) * 8) + 1)

int
curses_display_start()
{
	if (pthread_mutex_init(&g_curses_mutex, NULL) != 0)
		goto FailedInitMutex;
	if (initscr() == NULL)
		goto FailedInitScr;
	if (start_color() == ERR)
		goto FailedStartColor;
	if (savetty() == ERR)
		goto FailedSaveTty;
	if (raw() == ERR)
		goto FailedCbreak;
	if (nodelay(stdscr, TRUE) == ERR)
		goto FailedNoDelay;
	if (noecho() == ERR)
		goto FailedNoEcho;
	if (nonl() == ERR)
		goto FailedNoNl;
	if (intrflush(stdscr, FALSE) == ERR)
		goto FailedIntrFlush;
	if (keypad(stdscr, TRUE) == ERR)
		goto FailedKeypad;
	if (curs_set(1) == ERR)
		goto FailedCursorSet;

	/* Create a foreground and background color palette that matches
	 * the IBM-PC CGA scheme.
	 */
	if (has_colors() && COLORS >= 8 && COLOR_PAIRS >= 256) {
		/* Terminal can support the full PC scheme */
		int i;
		for (i = 0; i < 256; i++) {
			short fcolor = pc2ncurses(i % 8);
			short bcolor = pc2ncurses(i / 8);
			init_pair(i + 1, fcolor, bcolor);
		}
		crs_has_colors = 1;
	} else {
		crs_has_colors = 0;
	}

	/* Query the current terminal window size. In the future
	 * we might also set a signal handler to receive updates to the
	 * size and adjust the current (and future) display refreshes
	 * accordingly.
	 */
	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &g_ws) == -1)
		goto FailedWindowSize;

	return 0;

FailedWindowSize:
FailedCursorSet:
FailedKeypad:
FailedIntrFlush:
FailedNoNl:
FailedNoEcho:
FailedNoDelay:
FailedCbreak:
	resetty();
FailedSaveTty:
FailedStartColor:
	endwin();
FailedInitScr:
	pthread_mutex_destroy(&g_curses_mutex);
FailedInitMutex:
	return -1;
}

/* Start up the CURSES keyboard reading thread and FIFO */
int
curses_keyboard_start()
{
	if ((key_fifo.buf = calloc(sizeof(int), KEYFIFO_SIZE)) == NULL)
		goto FailedFifoAlloc;

	key_fifo.rp = key_fifo.wp = key_fifo.buf;
	key_fifo.sz = KEYFIFO_SIZE;
	key_fifo.cnt = 0;

	if (pthread_create(&g_keyboard_thread, NULL, kbread_proc, NULL) != 0)
		goto FailedThreadStart;

	return 0;

FailedThreadStart:
	free(key_fifo.buf);
FailedFifoAlloc:
	return -1;
}

/* Shutdown CURSES */
int
curses_display_stop()
{
	keypad(stdscr, FALSE);
	intrflush(stdscr, TRUE);
	curs_set(1);
	resetty();
	endwin();
	pthread_mutex_destroy(&g_curses_mutex);

	return 0;
}

/* Stop the CURSES keyboard thread, free the FIFO */
int
curses_keyboard_stop()
{
	void *dummy;

	pthread_cancel(g_keyboard_thread);
	pthread_join(g_keyboard_thread, &dummy);
	free(key_fifo.buf);

	return 0;
}

/* Create a new virtual display.
 * The "noscrol" flag, if set, causes lines to "wrap around" from the bottom
 * to the top of the screen instead of scrolling the entire screen upwards
 * with each new line. This can be handy for packet trace screens.
 */
struct display *
newdisplay(
int rows,
int cols,	/* Size of new screen. 0,0 defaults to whole screen */
int noscrol,	/* (Not supported) 1: old IBM-style wrapping instead of */
            	/* scrolling */
int size	/* Size of virtual screen, lines */
){
	struct display *dp;
	int i;

	/* Set viewport defaults for display */
	if (rows == 0 && cols == 0) {
		rows = g_ws.ws_row;
		cols = g_ws.ws_col;
	}

	dp = (struct display *)calloc(1,sizeof(struct display));
	dp->cookie = D_COOKIE;
	dp->tabstops = (uint8 *)calloc(1,cols);
	dp->cols = cols;
	dp->rows = size;

	/* Current viewport starts justified with the bottom */
	/* This viewport position can change with scroll view commands */
	dp->viewrows = rows; /* Viewport size */
	dp->viewtop = dp->rows - dp->viewrows;

	/* All VT-100 activity is directed to the bottom view lines,
	 * regardless of the scrolling viewport.
	 */
	dp->padtop = dp->viewtop;

	/* Set the last "saved cursor position" at top of normal view */
	dp->savrow = dp->padtop;
	dp->savcol = 0;

	/* Set default tabs every 8 columns */
	for(i=0;i<cols;i+= 8)
		dp->tabstops[i] = 1;

	/* Start with green color on black background */
	dp->fcolor = 2;
	dp->bcolor = 0;

	/* Lock out other CURSES threads temporarily */
	pthread_mutex_lock(&g_curses_mutex);

	dp->window = newpad(dp->rows, dp->cols);
	scrollok(dp->window, TRUE);
	if (crs_has_colors)
		wcolor_set(dp->window, pcPAIR(dp->fcolor, dp->bcolor), NULL);
	wclear(dp->window);	/* Start with a clean slate */
	wmove(dp->window, dp->padtop, 0); /* Position cursor at top of act. */

	/* Turn on intensity */
	wattron(dp->window, A_BOLD);

	pthread_mutex_unlock(&g_curses_mutex);

	return dp;
}

/* Close a display - simply get rid of the memory */
void
closedisplay(struct display *dp)
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;
	DELWIN(dp->window);
	free(dp);
}

#if 0 /* PORT ME */
/* Write buffer to status line. Works independently of the ANSI
 * machinery so as to not upset a possible escape sequence in
 * progress. Maximum of one line allowed, no control sequences
 */
void
statwrite(
int col,		/* Starting column of write */
void *buf,		/* Data to be written */
int cnt,		/* Count */
int attrib		/* Screen attribute to be used */
){
	uint8 *buf1 = buf;
	uint8 *sp = Statbuf;

	/* Clip debug area if activated */
	if(Kdebug && cnt > DCOL - col - 1)
		cnt = DCOL - col - 1;
	else if(cnt > COLS-col)
		cnt = COLS - col;	/* Limit write to line length */

	while(cnt-- != 0){
		if(sp[0] != *buf1 || sp[1] != attrib){
			if(col < Stdirt.lcol)
				Stdirt.lcol = col; 
			if(col > Stdirt.rcol)
				Stdirt.rcol = col;
			sp[0] = *buf1;
			sp[1] = attrib;
		}
		buf1++;
		sp += 2;
		col++;
	}
	/* Display unscrolled status region */
	if(Stdirt.lcol <= Stdirt.rcol){
		puttext(Stdirt.lcol+1,ROWS,Stdirt.rcol+1,ROWS,
		 Statbuf + 2*Stdirt.lcol);
		Stdirt.lcol = COLS-1;
		Stdirt.rcol = 0;
	}
}
#endif

/* Write data to the virtual display. Does NOT affect the real screen -
 * dupdate(dp) must be called to copy the virtual screen to the real
 * screen.
 */
void
displaywrite(
struct display *dp,	/* Virtual screen pointer */
const void *buf,	/* Data to be written */
int cnt			/* Count */
){
	uint8 c;
	const char *bufp = buf;

	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	while(cnt-- != 0){
		c = *bufp++;
		switch(dp->state){
		case DISP_ESCAPE:
			desc(dp,c);
			break;
		case DISP_ARG:
			darg(dp,c);
			break;
		case DISP_NORMAL:
			dchar(dp,c);
			break;
		}
	}
	ksignal(dp,1);
}

/* Make the real screen look like the virtual one. Quite simple in CURSES.
 */
void
dupdate(struct display *dp)
{
	/* This curses call draws a portion of the full virtual screen
	 * onto the real screen. The portion selected is based on the
	 * current viewport/scrolling region selected.
	 */
	pthread_mutex_lock(&g_curses_mutex);
	/* If we're in "scroll mode", hide the cursor */
	if (dp->flags.scrollbk)
		curs_set(0);
	else
		curs_set(1);
	prefresh(dp->window,
		dp->viewtop, 0, /* Internal start */
		0, 0, /* External start (at real screen positon 0, 0) */
		dp->viewrows, dp->cols /* External stop */
	);
	pthread_mutex_unlock(&g_curses_mutex);
}

/* Enter (or exit) scroll mode. For us that just signals whether the
 * cursor should be displayed.
 */
void
dscrollmode(
struct display *dp,
int flag
){
	flag = flag ? 1 : 0;

	if (flag != dp->flags.scrollbk) {
		dp->flags.scrollbk = flag;
		ksignal(dp, 1);
	}
}

/* Position scroll viewport at top of buffer? */
void
dhome(struct display *dp)
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	scroll_set(dp, 0);
	ksignal(dp, 1);
}

/* Position scroll viewport at bottom of buffer? */
void
dend(struct display *dp)
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	scroll_set(dp, dp->padtop);
	ksignal(dp, 1);
}
void
dpgup(struct display *dp)
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	scroll_adjust(dp, -dp->viewrows);
	ksignal(dp, 1);
}
void
dpgdown(struct display *dp)
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	scroll_adjust(dp, dp->rows);
	ksignal(dp, 1);
}
void
dcursup(struct display *dp)
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	scroll_adjust(dp, 1);
	ksignal(dp, 1);
}
void
dcursdown(struct display *dp)
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	scroll_adjust(dp, -1);
	ksignal(dp, 1);
}

/* Process incoming character while in ESCAPE state */
static void
desc(struct display *dp,uint8 c)
{
	int i, col, row;

	switch(c){
	case 'O':
	case '[':	/* Always second char of ANSI escape sequence */
		/* Get ready for argument list */
		dp->state = DISP_ARG;
		dp->argi = 0;
		for(i=0;i<MAXARGS;i++)
			dp->arg[i] = 0;

		break;
	case '7':	/* Save cursor location (VT-100) */
		GETYX(dp->window, dp->savrow, dp->savcol);
		dp->state = DISP_NORMAL;
		break;
	case '8':	/* Restore cursor location (VT-100) */
		WMOVE(dp->window, dp->savrow, dp->savcol);
		dp->state = DISP_NORMAL;
		break;
	case ESC:
		break;	/* Remain in ESCAPE state */
	case 'H':	/* Set tab stop at current position (VT-100) */
		GETYX(dp->window, row, col);
		dp->tabstops[col] = 1;
		break;
	default:
		dp->state = DISP_NORMAL;
		dchar(dp,c);
	}
}

/* Process characters after a ESC[ sequence */
static void
darg(struct display *dp,uint8 c)
{
	int i, row, col;

	switch(c){
	case ESC:
		dp->state = DISP_ESCAPE;
		return;
	case '?':	/* Ignored */
	case '=':
		return;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		/* Collect decimal number */
		dp->arg[dp->argi] = 10*dp->arg[dp->argi] + (c - '0');
		return;
	case ';':	/* Next argument is beginning */
		if(dp->argi <= MAXARGS - 1)
			dp->argi++;
		dp->arg[dp->argi] = 0;
		return;
	case '@':	/* Open up space for character */
		WINSCH(dp->window, ' ');
		break;
	case 'A':	/* Cursor up */
		if(dp->arg[0] == 0)
			dp->arg[0] = 1;	/* Default is one line */
		cursor_adjust(dp, -dp->arg[0], 0);
		break;
	case 'B':	/* Cursor down */
		if(dp->arg[0] == 0)
			dp->arg[0] = 1;	/* Default is one line */
		cursor_adjust(dp, dp->arg[0], 0);
		break;
	case 'C':	/* Cursor right */
		if(dp->arg[0] == 0)
			dp->arg[0] = 1;	/* Default is one column */
		cursor_adjust(dp, 0, dp->arg[0]);
		break;
	case 'D':	/* Cursor left */
		if(dp->arg[0] == 0)
			dp->arg[0] = 1;	/* Default is one column */
		cursor_adjust(dp, 0, -dp->arg[0]);
		break;
	case 'f':
	case 'H':	/* Cursor motion - limit to scrolled region */
		cursor_set(dp,
			(dp->arg[0] == 0) ? 0 : dp->arg[0] - 1, /* row */
			(dp->arg[1] == 0) ? 0 : dp->arg[1] - 1  /* col */
		);
		break;
#if 0
	case 'h':	/* Set mode */
		switch(dp->arg[0]){
		case 7:	/* Turn on wrap mode */
			/* Can't emulate easily in CURSES */
			break;
		}
		break;
#endif
	case 'J':	/* Clear screen */
		switch(dp->arg[0]){
		case 2:
			/* Note: we don't clear the whole buffer here, just
			 * the bottom visible portion that functions as the
			 * working area.
			 */
			WMOVE(dp->window, dp->padtop, 0); /* Home cursor */
			WCLRTOBOT(dp->window);	/* Clear to bottom */
			break;
		case 0:
			WCLRTOBOT(dp->window);	/* Clear to end of screen (VT-100) */
			break;
		}
		break;
	case 'K':	/* Erase to end of current line */
		WCLRTOEOL(dp->window);
		break;
	case 'L':	/* Add blank line */
		WINSERTLN(dp->window);
		break;		
#if 0
	case 'l':	/* Clear mode */
		switch(dp->arg[0]){
		case 7:	/* Turn off wrap mode */
			/* Can't emulate easily in CURSES */
			dp->flags.no_line_wrap = 1;
			break;
		}
		break;
#endif
	case 'M':	/* Delete line */
		WDELETELN(dp->window);
		break;
	case 'm':	/* Set screen attributes */
		for(i=0;i<=dp->argi;i++){
			dattrib(dp,dp->arg[i]);
		}
		break;
	case 'P':	/* Delete character */
		WDELCH(dp->window);
		break;
	case 's':	/* Save cursor position */
		GETYX(dp->window, dp->savrow, dp->savcol);
		break;
	case 'u':	/* Restore cursor position */
		WMOVE(dp->window, dp->savrow, dp->savcol);
		break;
	case 'g':
		switch(dp->arg[0]){
		case 0:
			GETYX(dp->window, row, col);
			dp->tabstops[col] = 0;
			break;
		case 3:
			memset(dp->tabstops,0,dp->cols);
			break;
		}
		break;
	}
	dp->state = DISP_NORMAL;
}

/* Process an argument to an attribute set command */
static void
dattrib(
struct display *dp,
int val
){
	switch(val){
	case 0:	/* Normal white on black */
		dp->fcolor = 7;
		dp->bcolor = 0;
		WCOLOR_SET(dp->window, pcPAIR(dp->fcolor, dp->bcolor), NULL);
		WATTRSET(dp->window, A_NORMAL);
		break;
	case 1:	/* High intensity */
		WATTRON(dp->window, A_STANDOUT);
		break;
	case 5:	/* Blink on */
		WATTRON(dp->window, A_BLINK);
		break;
	case 7:	/* Reverse video */
		WATTRSET(dp->window, A_REVERSE);
		break;
	default:
		if(val >= 30 && val < 38){
			/* Set foreground color */
			dp->fcolor = val - 30;
		} else if(val >= 40 && val < 48){
			/* Set background color */
			dp->bcolor = val - 40;
		}
		if (crs_has_colors)
			WCOLOR_SET(dp->window, pcPAIR(dp->fcolor, dp->bcolor),
				NULL);
		break;
	}
 }

/* Display character */
static void
dchar(
struct display *dp,
uint8 c
){
	uint8 *cp;
	int row, col;

	switch(c){
	case ESC:
		dp->state = DISP_ESCAPE;
		return;
	case '\r':
	case CTLQ:	/*****/
	case '\0':	/* Ignore nulls and bells */
	case BELL:
		break;
	case FF:	/* Page feed */
		/* Note: we don't clear the whole buffer here, just
		 * the bottom visible portion that functions as the
		 * working area.
		 */
		WMOVE(dp->window, dp->padtop, 0); /* Home cursor */
		WCLRTOBOT(dp->window);	/* Clear to bottom */
		break;
	case '\t':	/* Tab */
		GETYX(dp->window, row, col);
		while(col < dp->cols-1){
			if(dp->tabstops[++col])
				break;
		}
		WMOVE(dp->window, row, col);
		break;
	default:	/* Display character on screen */
		WADDCH(dp->window, c);
	}
}

/* Immediately display short debug string on lower right corner of display */
void
debug(char *s)
{
	/* Implement me */
	return;
}

static void
scroll_adjust(struct display *dp, int lines)
{
	int newtop;

	newtop = dp->viewtop + lines;
	scroll_set(dp, newtop);
}

static void
scroll_set(struct display *dp, int top)
{
	if (top < 0)
		top = 0;
	else if (top > dp->padtop)
		top = dp->padtop;
	dp->viewtop = top;
}

/* Move cursor a relative distance, but keep bounded in terminal */
static void
cursor_adjust(struct display *dp, int rowadj, int coladj)
{
	int row, col;

	/* This will fetch the true cursor position within the entire
	 * virtual buffer. This must be adjusted to active-display
	 * coordinates by subtracting the starting row number of the
	 * active display area within the full virtual buffer.
	 */
	GETYX(dp->window, row, col);
	row -= dp->padtop; /* Adjust to be relative to active start */

	row += rowadj;
	col += coladj;

	cursor_set(dp, row, col);
}

/* Move cursor to a specific position, but keep bounded in terminal */
/* The row and column position passed in is to be interpreted as belonging
 * to the active display region of the entire virtual window. This is
 * the area residing at the bottom of the buffer.
 */
static void
cursor_set(struct display *dp, int row, int col)
{
	/* Convert active-relative row to absolute buffer row */
	row += dp->padtop;

	if (row < dp->padtop)
		row = dp->padtop;
	else if (row >= dp->rows)
		row = dp->rows - 1;
	if (col < 0)
		col = 0;
	else if (col >= dp->cols)
		col = dp->cols - 1;
	WMOVE(dp->window, row, col);
}

/* CURSES keyboard reading thread. Runs continuously, blocking on
 * keyboard input. When a character is read, it is passed on to NOS as
 * a keyboard interrupt
 */
static void *
kbread_proc(void *dummy)
{
	int c, res, dum;
	fd_set readers, errors;

	for (;;) {
		/* Ask CURSES for all available characters */
		for (;;) {
			/* Don't kill this thread while holding lock */
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &dum);
			/* Lockout other CURSES users */
			pthread_mutex_lock(&g_curses_mutex);
			/* Fetch next keypress */
			c = getch();
			/* Unlock CURSES */
			pthread_mutex_unlock(&g_curses_mutex);
			/* Cancels are ok now */
			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &dum);

			if (c == ERR) {
				/* No more input at this time */
				break;
			}
			/* Translate key into NOS scheme */
			kb_add(c);
		}

		/* Wait for input to appear */
		FD_ZERO(&readers);
		FD_ZERO(&errors);
		FD_SET(STDIN_FILENO, &readers);
		FD_SET(STDIN_FILENO, &errors);
		res = select(STDIN_FILENO+1, &readers, NULL, &errors, NULL);
		if (res == -1) {
			if (errno == EINTR)
				continue;
			/* Bad. */
			break;
		}
		if (FD_ISSET(STDIN_FILENO, &errors)) {
			/* Bad */
			break;
		}
	}

	/* Shutdown NOS? */
	return NULL;
}

/* Convert code to "extended ASCII" */
#define pcSPEC(code) ((code) + 256)

/* Receive a character straight from the CURSES input thread */
static void
kb_add(int c)
{
	/* Keys from CURSES need translation before being given
	 * to the rest of NOS.
	 */
	switch (c) {
	case KEY_BACKSPACE: c = '\b'; break;
	case KEY_DOWN:  c = pcSPEC(CURSDWN); break;
	case KEY_UP:    c = pcSPEC(CURSUP); break;
	case KEY_LEFT:  c = pcSPEC(CURSLEFT); break;
	case KEY_RIGHT: c = pcSPEC(CURSRIGHT); break;
	case KEY_HOME:  c = pcSPEC(CURSHOM); break;
	case KEY_F(1):  c = pcSPEC(F1); break;
	case KEY_F(2):  c = pcSPEC(F2); break;
	case KEY_F(3):  c = pcSPEC(F3); break;
	case KEY_F(4):  c = pcSPEC(F4); break;
	case KEY_F(5):  c = pcSPEC(F5); break;
	case KEY_F(6):  c = pcSPEC(F6); break;
	case KEY_F(7):  c = pcSPEC(F7); break;
	case KEY_F(8):  c = pcSPEC(F8); break;
	case KEY_F(9):  c = pcSPEC(F9); break;
	case KEY_F(10): c = pcSPEC(F10); break;
	case KEY_NPAGE: c = pcSPEC(PAGEDWN); break;
	case KEY_PPAGE: c = pcSPEC(PAGEUP); break;
	default:
		if (c >= 256)
			/* ignore */
			return;
	}

	interrupt_enter();
#if 0
	if (key_fifo.cnt == 0)
#endif
		/* If FIFO is empty then it isn't anymore */
		ksignal(&key_fifo, 1);
	if (key_fifo.cnt < key_fifo.sz) {
		*(key_fifo.wp++) = c;
		if (key_fifo.wp >= &key_fifo.buf[key_fifo.sz])
			key_fifo.wp = key_fifo.buf;
		key_fifo.cnt++;
	}
	interrupt_leave();
}

/* Read from the keyboard input FIFO, blocking until something is
 * available.
 */
int
kbread(void)
{
	int state, c;

	state = disable();
	while (key_fifo.cnt == 0) {
		restore(state);
		if ((kerrno = kwait(&key_fifo)) != 0)
			return -1;
		disable();
	}

	c = *(key_fifo.rp++);
	if (key_fifo.rp >= &key_fifo.buf[key_fifo.sz])
		key_fifo.rp = key_fifo.buf;
	key_fifo.cnt--;
	restore(state);

	return c;
}

/* Convert a color index from the IBM PC CGA scheme:
 *  Black = 0, Blue, Green, Cyan, Red, Magenta, Yellow, White
 * to the ncurses scheme. (These may in fact be identically indexed,
 * but it is more portable not to assume so.
 */
static short
pc2ncurses(int color)
{
	switch (color) {
	default:
	case 0: return COLOR_BLACK;
	case 1: return COLOR_BLUE;
	case 2: return COLOR_GREEN;
	case 3: return COLOR_CYAN;
	case 4: return COLOR_RED;
	case 5: return COLOR_MAGENTA;
	case 6: return COLOR_YELLOW; /* or brown */
	case 7: return COLOR_WHITE;
	}
}
