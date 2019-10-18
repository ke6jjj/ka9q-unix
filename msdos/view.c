/* Random access file viewer. PC specific */
#include "top.h"

#include "lib/std/stdio.h"
#include <conio.h>
#include "lib/std/errno.h"
#include "global.h"
#include "core/session.h"
#include "core/tty.h"
#include "commands.h"
#include "core/socket.h"

#include <dos.h>

static long lineseek(kFILE *fp,long offset,int nlines,int width);
static int ctlproc(int c);

int
doview(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	kFILE *fp;

	if((fp = kfopen(argv[1],READ_TEXT)) == NULL){
		kprintf("Can't read %s\n",argv[1]);
		return 1;
	}
	newproc("view",512,view,0,(void *)fp,strdup(Cmdline),0);
	return 0;	
}
/* Random-access file display program. Used both to read local
 * files with the "view" command, and by the FTP client to view
 * directory listings, temporary copies of read files, etc.
 *
 */
void
view(s,p1,p2)
int s;		/* If non-zero, poll interval for a changing file */
void *p1;	/* Open file pointer to read from */
void *p2;	/* If non-null, name to give to session. We free it */
{
	struct session *sp;
	kFILE *fp;
	char *name;
	int c;
	long offset = 0;
	int row,col;
	int cols;
	int rows;
	int32 polldelay = 0;
	struct text_info text_info;

	gettextinfo(&text_info);
	cols = text_info.screenwidth;
	rows = text_info.screenheight-1;	/* Allow for status line */

	fp = (kFILE *)p1;
	if(p2 != NULL)
		name = (char *)p2;
	else
		name = kfpname(fp);

	if((sp = newsession(name,VIEW,1)) == NULL)
		return;

	if(p2 != NULL)
		free(name);

	if(s != 0)
		polldelay = s;
	sp->ctlproc = ctlproc;
	/* Put tty into raw mode so single-char responses will work */
	sp->ttystate.echo = sp->ttystate.edit = 0;
	for(;;){
		kfseek(fp,offset,kSEEK_SET);
		kputchar(FF);	/* Clear screen */
		/* Display a screen's worth of data, keeping track of
		 * cursor location so we know when the screen is full
		 */
		col = row = 0;
		while((c = kgetc(fp)),c != kEOF){
			switch(c){
			case '\n':
				row++;
				col = 0;
				break;
			case '\t':
				if(col < cols - 8)
					col = (col + 8) & ~7;
				break;
			default:
				col++;
				break;
			}
			if(col >= cols){
				/* Virtual newline caused by wraparound */
				col = 0;
				row++;
			}
			if(row >= rows)
				break;	/* Screen now full */
			kputchar(c);
		}
#ifdef	notdef
		if(kfeof(fp) && offset != 0){
			/* Hit end of file. Back up proper number of
			 * lines and try again.
			 */
			offset = lineseek(fp,offset,row-rows,cols);
			continue;
		}
#endif
		kfflush(kstdout);
		/* If we hit the end of the file and the file may be
		 * growing, then set an alarm to time out the getchar()
		 */
		do {
			if(kfeof(fp) && polldelay != 0){
				kalarm(polldelay);
			}
			c = kgetchar();	/* Wait for user keystroke */
			kalarm(0L);	/* Cancel alarm */
			if(c != -1 || kerrno != kEALARM)
				break;	/* User hit key */
			/* Alarm timeout; see if more data arrived by
			 * clearing the EOF flag, trying to read
			 * another byte, and then testing EOF again
			 */
			kclearerr(fp);
			(void)kgetc(fp);
			c = ' ';	/* Simulate a no-op keypress */
		} while(kfeof(fp));
		switch(c){
		case 'h':	/* Home */
		case 'H':
		case '<':	/* For emacs users */
			offset = 0;
			break;
		case 'e':	/* End */
		case '>':	/* For emacs users */
			kfseek(fp,0L,kSEEK_END);
			offset = lineseek(fp,kftell(fp),-rows,cols);
			break;
		case CTLD:	/* Down one half screen (for VI users) */
			if(!kfeof(fp))
				offset = lineseek(fp,offset,rows/2,cols);
			break;
		case CTLU:	/* Up one half screen (for VI users) */
			offset = lineseek(fp,offset,-rows/2,cols);
			break;
		case 'd':	/* down line */
		case CTLN:	/* For emacs users */
		case 'j':	/* For vi users */
			if(!kfeof(fp))
				offset = lineseek(fp,offset,1,cols);
			break;
		case 'D':	/* Down page */
		case CTLV:	/* For emacs users */
			if(!kfeof(fp))
				offset = lineseek(fp,offset,rows,cols);
			break;
		case 'u':	/* up line */
		case CTLP:	/* for emacs users */
		case 'k':	/* for vi users */
			offset = lineseek(fp,offset,-1,cols);
			break;
		case 'U':	/* Up page */
		case 'v':	/* for emacs users */
			offset = lineseek(fp,offset,-rows,cols);
			break;
		case CTLC:
		case 'q':
		case 'Q':
		case ESC:
			goto done;
		default:
			break;	/* Redisplay screen */
		}
	}
done:	kfclose(fp);
	freesession(&sp);
}
/* Given a starting offset into an open file stream, scan forwards
 * or backwards the specified number of lines and return a pointer to the
 * new offset.
 */
static long
lineseek(fp,start,nlines,width)
kFILE *fp;	/* Open file stream */
long start;	/* Offset to start searching backwards from */
int nlines;	/* Number of lines to move forward (+) or back (-) */
int width;	/* Screen width (max line size) */
{
	long offset;
	long *pointers;
	int col = 0;
	int c;
	int newlines = 0;

	if(nlines == 0)
		return start;	/* Nothing to do */

	if(nlines > 0){		/* Look forward requested # of lines */
		kfseek(fp,start,kSEEK_SET);
		col = 0;
		while((c = kgetc(fp)),c != kEOF){
			switch(c){
			case '\n':
				newlines++;
				col = 0;
				break;
			case '\t':
				if(col < width - 8)
					col = (col + 8) & ~7;
				break;
			default:
				col++;
				break;
			}
			if(col >= width){
				/* Virtual newline caused by wraparound */
				col = 0;
				newlines++;
			}
			if(newlines >= nlines)
				break;	/* Found requested count */
		}
		return kftell(fp);	/* Could be EOF */
	}
	/* Backwards scan (the hardest)
	 * Start back up at most (width + 2) chars/line from the start.
	 * This handles full lines followed by expanded newline
	 * sequences
	 */
	nlines = -nlines;
	offset = (width + 2)*(nlines + 1);
	if(offset > start)
		offset = 0;	/* Go to the start of the file */
	else
		offset = start - offset;
	kfseek(fp,offset,kSEEK_SET);

	/* Keep a circular list of the last 'nlines' worth of offsets to
	 * each line, starting with the first
	 */
	pointers = (int32 *)calloc(sizeof(long),nlines);
	pointers[newlines++ % nlines] = offset;

	/* Now count newlines up but not including the original
	 * starting point
	 */
	col = 0;
	for(;;){
		c = kgetc(fp);
		switch(c){
		case kEOF:
			goto done;
		case '\n':
			col = 0;
			offset = kftell(fp);
			if(offset >= start)
				goto done;
			pointers[newlines++ % nlines] = offset;
			break;
		case '\t':
			if(col < width - 8)
				col = (col + 8) & ~7;
			break;
		default:
			col++;
			break;
		}
		if(col >= width){
			/* Virtual newline caused by wraparound */
			col = 0;
			offset = kftell(fp);
			if(offset >= start)
				goto done;
			pointers[newlines++ % nlines] = offset;
		}
	}
	done:;
	if(newlines >= nlines){
		/* Select offset pointer nlines back */
		offset = pointers[newlines % nlines];
	} else {
		/* The specified number of newlines wasn't seen, so
		 * go to the start of the file
		 */
		offset = 0;
	}
	free(pointers);
	return offset;
}

/* Handle special keystrokes */
static int
ctlproc(c)
int c;
{
	switch(c){
	case 256 + CURSHOM:	/* HOME */
		kputc('h',Current->input);
		break;
	case 256 + CURSUP:	/* Cursor up */
		kputc('u',Current->input);
		break;
	case 256 + PAGEUP:	/* Page up */
		kputc('U',Current->input);
		break;
	case 256 + CURSEND:	/* End */
		kputc('e',Current->input);
		break;
	case 256 + CURSDWN:	/* Cursor down */
		kputc('d',Current->input);
		break;
	case 256 + PAGEDWN:	/* Page down */
		kputc('D',Current->input);
		break;
	default:
		return c;
	}
	kfflush(Current->input);
	return 0;
}

