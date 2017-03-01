/* ANSI display emulation
 *
 * This file emulates the IBM ANSI terminal display. It maintains a
 * display buffer and descriptor for each virtual display, of which there
 * can be many. All writes occur first into this display buffer, and then
 * any particular display buffer can be copied onto the real screen.
 * This allows many background tasks to run without blocking even though
 * only one task's display is actually being shown.
 *
 * This display driver is substantially faster than even the NANSI.SYS
 * loadable screen driver, particularly when large blocks are written.
 *
 * Extensions to handle displaying multiple small virtual windows should
 * be pretty straightforward.
 *
 * Copyright 1992 Phil Karn, KA9Q
 * 
 */
#include "top.h"

#include <conio.h>
#include <string.h>
#include <sys/stat.h>
#include "global.h"
#include "display.h"
#include "proc.h"

#define	DCOL	67
#define	DSIZ	(81-DCOL)

uint8 Statbuf[2*COLS];
struct dirty Stdirt;

uint8 fgattr[] = { 0, 4, 2, 14, 1, 5, 3, 7 };	/* Foreground attribs */
uint8 bgattr[] = { 0, 4, 2, 6, 1, 5, 3, 7 };	/* Background attribs */

static void dclrscr(struct display *dp);
static void desc(struct display *dp,uint8 c);
static void darg(struct display *dp,uint8 c);
static void dchar(struct display *dp,uint8 c);
static void dclreol(struct display *dp,int row,int col);
static void dattrib(struct display *dp,int val);
static uint8 *rbufloc(struct display *dp,int row,int col);
static uint8 *vbufloc(struct display *dp,int row,int col);
static void dinsline(struct display *dp);
static void ddelline(struct display *dp);
static void ddelchar(struct display *dp);
static void dinsert(struct display *dp);
static void dclreod(struct display *dp,int row,int col);
static int sadjust(struct display *dp,int lines);


extern struct proc *Display;

/* Create a new virtual display.
 * The "noscrol" flag, if set, causes lines to "wrap around" from the bottom
 * to the top of the screen instead of scrolling the entire screen upwards
 * with each new line. This can be handy for packet trace screens.
 */
struct display *
newdisplay(
int rows,
int cols,	/* Size of new screen. 0,0 defaults to whole screen */
int noscrol,	/* 1: old IBM-style wrapping instead of scrolling */
int size	/* Size of virtual screen, lines */
){
	struct display *dp;
	struct text_info text_info;
	int i;

	gettextinfo(&text_info);
	if(rows == 0)
		rows = text_info.screenheight - 1;/* room for stat line */
	if(cols == 0)
		cols = text_info.screenwidth;

	dp = (struct display *)calloc(1,sizeof(struct display) +
	 (rows+1)*sizeof(struct dirty) + cols);
	dp->cookie = D_COOKIE;
	dp->buf = calloc(2,cols*size);
	dp->dirty = (struct dirty *)(dp + 1);
	dp->tabstops = (uint8 *)(dp->dirty + rows);
	dp->rows = rows;
	dp->cols = cols;
	dp->virttop = dp->realtop = 0;
	dp->size = size;

	/* Set default tabs every 8 columns */
	for(i=0;i<cols;i+= 8)
		dp->tabstops[i] = 1;

	dp->attrib = 0x7;	/* White on black, no blink or intensity */
	dclrscr(dp);		/* Start with a clean slate */
	dp->flags.dirty_cursor = 1;
	dp->flags.no_scroll = noscrol;

	return dp;
}

/* Close a display - simply get rid of the memory */
void
closedisplay(struct display *dp)
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;
	free(dp->buf);
	free(dp);
}

/* Write buffer to status line. Works independently of the ANSI
 * machinery so as to not upset a possible escape sequence in
 * progress. Maximum of one line allowed, no control sequences
 */
void
statwrite(
int col,		/* Starting column of kwrite */
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
		cnt = COLS - col;	/* Limit kwrite to line length */

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
/* Make the real screen look like the virtual one. It attempts to do as
 * little work as possible unless the "dirty screen" flag is set -- then
 * the entire screen is updated. (This is useful when switching between
 * virtual display screens.)
 *
 * Note the different row and column numbering conventions -- I start
 * at zero, the puttext() and gotoxy() library functions start at 1.
 */
void
dupdate(struct display *dp)
{
	int row;
	long sp;
	struct dirty *dirtp;

	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	for(row=0,dirtp=dp->dirty;row<dp->rows;row++,dirtp++){
		if(dp->flags.dirty_screen){
			/* Force display of all columns */
			dirtp->lcol = 0;
			dirtp->rcol = dp->cols-1;
		}
		if(dirtp->lcol <= dirtp->rcol){
			puttext(dirtp->lcol+1,row+1,dirtp->rcol+1,row+1,
			 dp->flags.scrollbk ? rbufloc(dp,row,dirtp->lcol)
				: vbufloc(dp,row,dirtp->lcol));
			dirtp->lcol = dp->cols-1;
			dirtp->rcol = 0;
		}
	}
	if(dp->flags.scrollbk){
		/* Turn off cursor entirely */
		_setcursortype(_NOCURSOR);
	} else 	if(dp->flags.dirty_screen || dp->flags.dirty_cursor){
		/* Update cursor */
		gotoxy(dp->col+1,dp->row+1);
		_setcursortype(_NORMALCURSOR);
	}
	dp->flags.dirty_cursor = 0;
	dp->flags.dirty_screen = 0;
}
void
dscrollmode(
struct display *dp,
int flag
){
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	if(flag != dp->flags.scrollbk){
		dp->flags.scrollbk = flag;
		dp->flags.dirty_screen = 1;
		alert(Display,1);
	}
}

void
dhome(struct display *dp)
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	sadjust(dp,(dp->size - dp->rows));
}
void
dend(struct display *dp)
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	if(dp->realtop != dp->virttop){
		dp->realtop = dp->virttop;
		dp->flags.dirty_screen = 1;
		alert(Display,1);
	}
}
void
dpgup(struct display *dp)
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	sadjust(dp,dp->rows);
}
void
dpgdown(struct display *dp)
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	sadjust(dp,-dp->rows);
}
void
dcursup(struct display *dp)
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	sadjust(dp,+1);
}
void
dcursdown(struct display *dp)
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	sadjust(dp,-1);
}

/* Process incoming character while in ESCAPE state */
static void
desc(struct display *dp,uint8 c)
{
	int i;

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
		dp->savcol = dp->col;
		dp->savrow = dp->row;
		dp->state = DISP_NORMAL;
		break;
	case '8':	/* Restore cursor location (VT-100) */
		dp->col = dp->savcol;
		dp->row = dp->savrow;
		dp->flags.dirty_cursor = 1;
		dp->state = DISP_NORMAL;
		break;
	case ESC:
		break;	/* Remain in ESCAPE state */
	case 'H':	/* Set tab stop at current position (VT-100) */
		dp->tabstops[dp->col] = 1;
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
	int i;

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
		dinsert(dp);
		break;
	case 'A':	/* Cursor up */
		if(dp->arg[0] == 0)
			dp->arg[0] = 1;	/* Default is one line */
		if(dp->arg[0] <= dp->row)
			dp->row -= dp->arg[0];
		else
			dp->row = 0;
		dp->flags.dirty_cursor = 1;
		break;
	case 'B':	/* Cursor down */
		if(dp->arg[0] == 0)
			dp->arg[0] = 1;	/* Default is one line */
		dp->row += dp->arg[0];
		if(dp->row >= dp->rows)
			dp->row = dp->rows - 1;
		dp->flags.dirty_cursor = 1; 
		break;
	case 'C':	/* Cursor right */
		if(dp->arg[0] == 0)
			dp->arg[0] = 1;	/* Default is one column */
		dp->col += dp->arg[0];
		if(dp->col >= dp->cols)
			dp->col = dp->cols - 1;
		dp->flags.dirty_cursor = 1;
		break;
	case 'D':	/* Cursor left */
		if(dp->arg[0] == 0)
			dp->arg[0] = 1;	/* Default is one column */
		if(dp->arg[0] <= dp->col)
			dp->col -= dp->arg[0];
		else
			dp->col = 0;
		dp->flags.dirty_cursor = 1;
		break;
	case 'f':
	case 'H':	/* Cursor motion - limit to scrolled region */
		i = (dp->arg[0] == 0) ? 0 : dp->arg[0] - 1;
		if(i >= dp->rows)
			i = dp->rows-1;
		dp->row = i;

		i = (dp->arg[1] == 0) ? 0 : dp->arg[1] - 1;
		if(i >= dp->cols)
			i = dp->cols - 1;
		dp->col = i;
		dp->state = DISP_NORMAL;
		dp->flags.dirty_cursor = 1;
		break;
	case 'h':	/* Set mode */
		switch(dp->arg[0]){
		case 7:	/* Turn on wrap mode */
			dp->flags.no_line_wrap = 0;
			break;
		}
		break;
	case 'J':	/* Clear screen */
		switch(dp->arg[0]){
		case 2:
			dclrscr(dp);	/* Clear entire screen, home cursor */
			break;
		case 0:
			dclreod(dp,dp->row,dp->col);	/* Clear to end of screen (VT-100) */
			break;
		}
		break;
	case 'K':	/* Erase to end of current line */
		dclreol(dp,dp->row,dp->col);
		break;
	case 'L':	/* Add blank line */
		dinsline(dp);
		break;		
	case 'l':	/* Clear mode */
		switch(dp->arg[0]){
		case 7:	/* Turn off wrap mode */
			dp->flags.no_line_wrap = 1;
			break;
		}
		break;
	case 'M':	/* Delete line */
		ddelline(dp);
		break;
	case 'm':	/* Set screen attributes */
		for(i=0;i<=dp->argi;i++){
			dattrib(dp,dp->arg[i]);
		}
		break;
	case 'P':	/* Delete character */
		ddelchar(dp);
		break;
	case 's':	/* Save cursor position */
		dp->savcol = dp->col;
		dp->savrow = dp->row;
		break;
	case 'u':	/* Restore cursor position */
		dp->col = dp->savcol;
		dp->row = dp->savrow;
		dp->flags.dirty_cursor = 1;
		break;
	case 'g':
		switch(dp->arg[0]){
		case 0:
			dp->tabstops[dp->col] = 0;
			break;
		case 3:
			memset(dp->tabstops,0,dp->cols);
			break;
		}
		break;
	}
	dp->state = DISP_NORMAL;
}
/* Clear from specified location to end of screen, leaving cursor as is */
static void
dclreod(
struct display *dp,
int row,
int col
){
	dclreol(dp,row,col);	/* Clear current line */
	for(row = row + 1;row < dp->rows;row++)
		dclreol(dp,row,0);	/* Clear all lines below */
}
/* Insert space at cursor, moving all chars on right to right one position */
static void
dinsert(struct display *dp)
{
	int i = 2*(dp->cols - dp->col - 1);
	uint8 *cp = vbufloc(dp,dp->row,dp->col);
	struct dirty *dirtp = &dp->dirty[dp->row];

	if(i != 0)
		memmove(cp+2,cp,i);	/* handles overlapping blocks */
	*cp++ = ' ';
	*cp = dp->attrib;
	/* Dirty everything from the cursor to the right edge */
	if(dp->col < dirtp->lcol)
		dirtp->lcol = dp->col;
	dirtp->rcol = dp->cols-1;
}
/* Delete character at cursor, moving chars to right left one position */
static void
ddelchar(struct display *dp)
{
	uint8 *cp = vbufloc(dp,dp->row,dp->col);
	int i = 2*(dp->cols-dp->col-1);
	struct dirty *dirtp = &dp->dirty[dp->row];

	/* Copy characters to right one space left */
	if(i != 0)
		memmove(cp,cp+2,i);	/* memmove handles overlapping blocks */
	/* Clear right most character on line */
	cp[i] = ' ';
	cp[i+1] = dp->attrib;
	/* Dirty everything from the cursor to the right edge */
	if(dp->col < dirtp->lcol)
		dirtp->lcol = dp->col;
	dirtp->rcol = dp->cols-1;
}
/* Delete line containing cursor, moving lines below up one line */
static void
ddelline(struct display *dp)
{
	uint8 *cp1,*cp2;
	int row;
	struct dirty *dirtp;

	for(row=dp->row,dirtp = &dp->dirty[row];row < dp->rows-1;row++,dirtp++){
		cp1 = vbufloc(dp,row,0);
		cp2 = vbufloc(dp,row+1,0);
		memcpy(cp1,cp2,dp->cols*2);
		/* Dirty entire line */
		dirtp->lcol = 0;
		dirtp->rcol = dp->cols-1;
	}
	/* Clear bottom line */
	dclreol(dp,dp->rows-1,0);
}		
/* Insert blank line where cursor is. Push existing lines down one */
static void
dinsline(struct display *dp)
{
	uint8 *cp1,*cp2;
	int row;
	struct dirty *dirtp;

	/* Copy lines down */
	for(row = dp->rows-1,dirtp = &dp->dirty[row];row > dp->row;row--){
		cp1 = vbufloc(dp,row-1,0);
		cp2 = vbufloc(dp,row,0);
		memcpy(cp2,cp1,2*dp->cols);
		/* Dirty entire line */
		dirtp->lcol = 0;
		dirtp->rcol = dp->cols-1;
	}
	/* Clear current line */
	dclreol(dp,dp->row,0);
}

/* Process an argument to an attribute set command */
static void
dattrib(
struct display *dp,
int val
){
	switch(val){
	case 0:	/* Normal white on black */
		dp->attrib = 0x7;
		break;
	case 1:	/* High intensity */
		dp->attrib |= 0x8;
		break;
	case 5:	/* Blink on */
		dp->attrib |= 0x80;
		break;
	case 7:	/* Reverse video (black on white) */
		dp->attrib = 0x70;
		break;
	default:
		if(val >= 30 && val < 38){
			/* Set foreground color */
			dp->attrib = (dp->attrib & ~0x7) | fgattr[val - 30];
		} else if(val >= 40 && val < 48){
			/* Set background color */
			dp->attrib = (dp->attrib & ~0x70) | ((bgattr[val - 40]) << 4);
		}
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
	int row,rowchange;
	struct dirty *dirtp;

	rowchange = 0;
	switch(c){
	case ESC:
		dp->state = DISP_ESCAPE;
		return;
	case CTLQ:	/*****/
	case '\0':	/* Ignore nulls and bells */
	case BELL:
		break;
	case '\b':	/* Backspace */
		if(dp->col > 0){
			dp->col--;
			dp->flags.dirty_cursor = 1;
		}
		break;
	case FF:	/* Page feed */
		dclrscr(dp);
		break;
	case '\t':	/* Tab */
		while(dp->col < dp->cols-1){
			if(dp->tabstops[++dp->col])
				break;
		}
		dp->flags.dirty_cursor = 1;
		break;
	case '\n':	/* Move cursor down one row */
		dp->row++;
		rowchange = 1;
		dp->flags.dirty_cursor = 1;
		break;
	case '\r':	/* Move cursor to beginning of current row */
		dp->col = 0;
		dp->flags.dirty_cursor = 1;
		break;
	default:	/* Display character on screen */
		/* Compute location in screen buffer memory */
		cp = vbufloc(dp,dp->row,dp->col);
		/* Normal display */
		if(c != *cp || cp[1] != dp->attrib){
			dirtp = &dp->dirty[dp->row];
			if(dp->col < dirtp->lcol)
				dirtp->lcol = dp->col;
			if(dp->col > dirtp->rcol)
				dirtp->rcol = dp->col;
		}
		*cp++ = c;
		*cp = dp->attrib;
		dp->flags.dirty_cursor = 1;
		/* Update cursor position, wrapping if necessary */
		if(++dp->col == dp->cols){
			if(dp->flags.no_line_wrap){
				dp->col--;
			} else {
				dp->col = 0;
				dp->row++;
				rowchange = 1;
			}
		}
	}
	/* Scroll screen if necessary */
	if(rowchange && dp->row >= dp->rows){
		dp->row--;
		/* Scroll screen up */
		dp->virttop = (dp->virttop + 1) % dp->size;
		if(dp->virttop > dp->maxtop)
			dp->maxtop = dp->virttop;
		if(!dp->flags.scrollbk)
			dp->realtop = (dp->realtop + 1) % dp->size;
		if(!dp->flags.no_scroll){
			for(row=0,dirtp=dp->dirty;row <dp->rows;row++,dirtp++){
				dirtp->lcol = 0;
				dirtp->rcol = dp->cols-1;
			}
		}
		dclreol(dp,dp->row,0);
	}
}

/* Clear from specified location to end of line. Cursor is not moved */
static void
dclreol(
struct display *dp,
int row,
int col
){
	uint8 *cp = vbufloc(dp,row,col);
	struct dirty *dirtp = &dp->dirty[row];
	int i;

	for(i=dp->cols - col;i!=0;i--){
		*cp++ = ' ';
		*cp++ = dp->attrib;
	}
	/* Dirty from current column to right edge */
	if(col < dirtp->lcol)
		dirtp->lcol = col;
	dirtp->rcol = dp->cols-1;
}
/* Move cursor to top left corner, clear screen */
static void
dclrscr(struct display *dp)
{
	dclreod(dp,0,0);
	dp->row = dp->col = 0;
	dp->flags.dirty_cursor = 1;
}
/* Return pointer into screen buffer for specified cursor location.
 * Not guaranteed to be valid past the end of the current line due to
 * scrolling
 */
static uint8 *
rbufloc(
struct display *dp,
int row,
int col
){
	row = (row + dp->realtop) % dp->size;
	return dp->buf + 2*(col + dp->cols*row);
}
static uint8 *
vbufloc(
struct display *dp,
int row,
int col
){
	row = (row + dp->virttop) % dp->size;
	return dp->buf + 2*(col + dp->cols*row);
}
/* Immediately display short debug string on lower right corner of display */
void
debug(char *s)
{
	int i;
	static uint8 msg[2*DSIZ];

	if(msg[1] != 0x7){
		/* One time initialization to blanks with white-on-black */
		for(i=0;i<DSIZ;i++){
			msg[2*i] = ' ';
 			msg[2*i+1] = 0x7;
		}
	}
	if(s == NULL)
		return;
	for(i=0;i<DSIZ && *s != '\0';i++)
		msg[2*i] = (uint8) *s++;

	for(;i<DSIZ;i++)
		msg[2*i] = ' ';

	puttext(DCOL,ROWS,COLS,ROWS,msg);
}
static int
sadjust(struct display *dp,int lines)
{
	int curscroll,newscroll;

	curscroll = (dp->size + dp->virttop - dp->realtop) % dp->size;
	newscroll = curscroll + lines;
	if(newscroll < 0)
		newscroll = 0;
	else if(newscroll > dp->maxtop)
		newscroll = dp->maxtop;
	else if(newscroll > dp->size - dp->rows)
		newscroll = dp->size - dp->rows;

	if(newscroll != curscroll){
		dp->realtop -= newscroll - curscroll;
		while(dp->realtop < 0)
			dp->realtop += dp->size;
		while(dp->realtop >= dp->size)
			dp->realtop -= dp->size;
		dp->flags.dirty_screen = 1;
		alert(Display,1);
	}
	return newscroll;
}
