#ifndef	_DISPLAY_H
#define	_DISPLAY_H

#include "top.h"

#ifndef	_STDIO_H
#include "stdio.h"
#endif

/* Extended keyboard codes for function keys */
#define	F1	59	/* Function key 1 */
#define	F2	60
#define	F3	61
#define	F4	62
#define	F5	63
#define	F6	64
#define	F7	65
#define	F8	66
#define	F9	67
#define	F10	68

#define	CURSHOM	71	/* Home key */
#define	CURSUP	72	/* Up arrow key */
#define	PAGEUP	73	/* Page up key */
#define	CURSLEFT 75	/* Cursor left key */
#define CURSRIGHT 77	/* Cursor right key */	
#define	CURSEND	79	/* END key */
#define	CURSDWN	80	/* Down arrow key */
#define	PAGEDWN	81	/* Page down key */

#ifdef MSDOS
#define	AF1	104	/* ALT-F1 */
#define	AF2	105
#define	AF3	106
#define	AF4	107
#define	AF5	108
#define	AF6	109
#define	AF7	110
#define	AF8	111
#define	AF9	112
#define	AF10	113
#define	AF11	139
#define	AF12	140
#endif

/* Display is opaque for most users of this code */
struct display;

struct display *newdisplay(int rows,int cols,int noscrol,int sfsize);
void displaywrite(struct display *dp,const void *buf,int cnt);
void dupdate(struct display *dp);
void closedisplay(struct display *dp);
void statwrite(int col,void *buf,int cnt,int attrib);
/* These functions adjust the scroll back view */
void dscrollmode(struct display *dp,int flag);
void dhome(struct display *dp);
void dend(struct display *dp);
void dpgup(struct display *dp);
void dpgdown(struct display *dp);
void dcursup(struct display *dp);
void dcursdown(struct display *dp);
void debug(char *s);

#endif /*_DISPLAY_H */

