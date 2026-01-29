#include "top.h"

#include "unix/display.h"
#include "unix/display_crs.h"
#include "unix/display_hdl.h"

#include "core/proc.h"
#include "core/session.h"
#include "core/display.h"

static int g_headed;

void
unix_display_init(int headed)
{
	g_headed = headed;

	if (g_headed) {
		/* Start up CURSES */
		curses_display_start();

		/* start keyboard proc */
		curses_keyboard_start();
	}
}

void
unix_display_keyboard_stop()
{
	if (g_headed)
		curses_keyboard_stop();
}

void
unix_display_stop()
{
	if (g_headed)
		/* curses deinit */
		curses_display_stop();
}

/* Display-tending process */
void
display(int i,void *v1,void *v2)
{
	struct session *sp;
	struct display *dp;

	if (!g_headed)
		/* Nothing to do on headless instances */
		return;

	for(;;){
		sp = Current;
		if(sp == NULL || sp->output == NULL
		 || (dp = (struct display *)sp->output->ptr) == NULL){
			/* Something weird happened */
			ppause(500L);
			continue;
		}
#if 0
		statline(dp,sp);
#endif
		dupdate(dp);
#if 0
		kalarm(100L);	/* Poll status every 100 ms */
#endif
		kwait(dp);
#if 0
		kalarm(0L);
#endif
	}
}

#if 0
/* Compose status line for bottom of screen */
static void
statline(struct display *dp,struct session *sp)
{
	int attr;
	char buf[81];
	struct text_info text_info;
	int unack;
	int s1;
	int s = -1;

	/* Determine attribute to use */
	gettextinfo(&text_info);
	if(text_info.currmode == MONO)
		attr = 0x07;	/* Regular white on black */
	else
		attr = 0x02;	/* Green on black */

	if(sp->network != NULL && (s = kfileno(sp->network)) != -1){
		unack = socklen(s,1);
		if(sp->type == FTP && (s1 = kfileno(sp->cb.ftp->data)) != -1)
			unack += socklen(s1,1);
	}
	memset(buf,' ',80);
	buf[80] = '\0';
	sprintf(&buf[0],"%2d: %s",sp->index,sp->name);

	if(dp->flags.scrollbk){
		int off;
		off = dp->virttop - dp->realtop;
		if(off < 0)
			off += dp->size;
		sprintf(&buf[54],"Scroll:%-5u",off);
	} else if(s != -1 && unack != 0){
		sprintf(&buf[54],"Unack: %-5u",unack);
	} else
		sprintf(&buf[54],"            ");
	sprintf(&buf[66],"F8:nxt F10:cmd");
	statwrite(0,buf,sizeof(buf)-1,attr);
}
#endif

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
int size,	/* Size of virtual screen, lines */
int *do_buffered,
const char **desired_eol
){
	if (g_headed)
		return newdisplay_curses(rows, cols, noscrol, size,
			do_buffered, desired_eol);
	else
		return newdisplay_headless(do_buffered, desired_eol);
}

/* Close a display - simply get rid of the memory */
void
closedisplay(struct display *dp)
{
	if(dp == NULL)
		return;
	if (dp->cookie == D_COOKIE)
		closedisplay_curses(dp);
	else if (dp->cookie == D_COOKIE_HEADLESS)
		closedisplay_headless(dp);
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
	if(dp == NULL)
               return;

	if (dp->cookie == D_COOKIE)
		displaywrite_curses(dp, buf, cnt);
	else if (dp->cookie == D_COOKIE_HEADLESS)
		displaywrite_headless(dp, buf, cnt);
}

/* Make the real screen look like the virtual one.  */
void
dupdate(struct display *dp)
{
	if (dp->cookie == D_COOKIE)
		dupdate_curses(dp);
	/* If headless, there's nothing to do */
}

/* Enter (or exit) scroll mode. For us that just signals whether the
 * cursor should be displayed.
 */
void
dscrollmode(
struct display *dp,
int flag
){
	if (dp->cookie == D_COOKIE)
		dscrollmode_curses(dp, flag);
	/* If headless, there's nothing to do */
}

/* Position scroll viewport at top of buffer? */
void
dhome(struct display *dp)
{
	if (dp == NULL)
		return;
	if (dp->cookie == D_COOKIE)
		dhome_curses(dp);
	/* If headless, there's nothing to do */
}

/* Position scroll viewport at bottom of buffer? */
void
dend(struct display *dp)
{
	if(dp == NULL)
		return;
	if (dp->cookie == D_COOKIE)
		dend_curses(dp);
	/* If headless, there's nothing to do */
}
void
dpgup(struct display *dp)
{
	if(dp == NULL)
		return;
	if (dp->cookie == D_COOKIE)
		dpgup_curses(dp);
	/* If headless, there's nothing to do */
}
void
dpgdown(struct display *dp)
{
	if(dp == NULL)
		return;
	if (dp->cookie == D_COOKIE)
		dpgdown_curses(dp);
	/* If headless, there's nothing to do */
}
void
dcursup(struct display *dp)
{
	if(dp == NULL)
		return;
	if (dp->cookie == D_COOKIE)
		dcursup_curses(dp);
	/* If headless, there's nothing to do */
}
void
dcursdown(struct display *dp)
{
	if(dp == NULL)
		return;
	if (dp->cookie == D_COOKIE)
		dcursdown_curses(dp);
	/* If headless, there's nothing to do */
}

/* Immediately display short debug string on lower right corner of display */
void
debug(char *s)
{
	/* Implement me */
	return;
}

/* Read from the keyboard input FIFO, blocking until something is
 * available.
 */
int
kbread(void)
{
	if (g_headed)
		return kbread_curses();
	/* In headless mode, there's no input to read */
	return -1;
}
