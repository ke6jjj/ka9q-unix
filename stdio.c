/* Standard I/O routines with socket support
 * Replaces those in Borland C++ library
 * Copyright 1992 Phil Karn, KA9Q
 */
#include "top.h"

#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#if	defined(__TURBOC__) && defined(MSDOS)
#define __IN_OPEN	1	/* Less stringent open() proto in io.h */
#include <io.h>
#endif
#ifdef UNIX
#include <unistd.h>
#endif
#ifdef HAVE_FUNOPEN
#include <stdio.h>
#endif
#include <assert.h>
#include "global.h"
#include "stdio.h"
#include "mbuf.h"
#include "proc.h"
#include "usock.h"
#include "socket.h"
#include "display.h"
#include "sb.h"
#include "asy.h"
#include "errno.h"

#define	_CREAT(a,b)	creat((a),(b))
#ifdef UNIX
#define _OPEN(a,b)	open((a),(b))
#define	_CLOSE(a)	close((a))
#define	_READ(a,b,c)	read((a),(b),(c))
#define	_WRITE(a,b,c)	write((a),(b),(c))
#else
#define _OPEN(a,b)	_open((a),(b))
#define	_CLOSE(a)	_close((a))
#define	_READ(a,b,c)	_read((a),(b),(c))
#define	_WRITE(a,b,c)	_write((a),(b),(c))
#endif
#define	_LSEEK(a,b,c)	lseek((a),(b),(c))
#define	_DUP(a)		dup((a))

static void _fclose(kFILE *fp);
static struct mbuf *_fillbuf(kFILE *fp,int cnt);
static kFILE *_fcreat(void);

kFILE *_Files;
int _clrtmp = 1;
extern unsigned *Refcnt;

#ifndef HAVE_FUNOPEN
/* Defined in format.c */
int _format(void putter(char,void *),void *,const char *,va_list);
#else
static int fun_write(void *, const char *, int);
#endif

/* Open a file and associate it with a (possibly specified) stream */
kFILE *
kfreopen(
char *filename,
char *mode,
kFILE *fp
){
	int modef;
	int textmode = 0;
	int create = 0;
	int append = 0;
	int fd;
	struct stat statbuf;

	if(strchr(mode,'r') != NULL){
		modef = O_RDONLY;
	} else if(strchr(mode,'w') != NULL){
		create = 1;
		modef = O_WRONLY;
	} else if(strchr(mode,'a') != NULL){
		modef = O_WRONLY;
		append = 1;
		if(stat(filename,&statbuf) == -1 && errno == ENOENT)
			create = 1;	/* Doesn't exist, so create */
	} else
		return NULL;	/* No recognizable mode! */

	if(strchr(mode,'+') != NULL)
		modef = O_RDWR;	/* Update implies R/W */

	if(strchr(mode,'t') != NULL)
		textmode = 1;
	
	if(create)
		fd = _CREAT(filename,S_IREAD|S_IWRITE);
	else
		fd = _OPEN(filename,modef);
	if(fd == -1)
		return NULL;

	if(fp != NULL){
		_fclose(fp);
	} else {
		if((fp = _fcreat()) == NULL){
			_CLOSE(fd);
			if(create)
				unlink(filename);
			return NULL;
		}
	}
	fp->fd = fd;
	fp->offset = 0;
	fp->type = _FL_FILE;
	fp->bufmode = _kIOFBF;
	fp->ptr = strdup(filename);
	fp->flags.ascii = textmode;
	fp->flags.append = append;
	fp->bufsize = kBUFSIZ;
	kseteol(fp,Eol);
	return fp;
}
/* Associate a file or socket descripter (small integer) with a stream */
kFILE *
kfdopen(
int handle,
char *mode
){
	kFILE *fp;
	int textmode = 0;
	int append = 0;

	if(handle == -1)
		return NULL;

	if(strchr(mode,'a') != NULL)
		append = 1;

	if(strchr(mode,'t') != NULL)
		textmode = 1;
	
	if((fp = _fcreat()) == NULL)
		return NULL;

	fp->fd = handle;
	fp->bufmode = _kIOFBF;
	fp->type = _fd_type(handle);
	fp->flags.ascii = textmode;
	fp->flags.append = append;

	fp->bufsize = kBUFSIZ;
	/* set default eol sequence, can be overridden by user */
	switch(fp->type){
	case _FL_SOCK:
		kseteol(fp,eolseq(handle));	/* Socket eol seq */
		break;
	case _FL_FILE:
		kseteol(fp,Eol);	/* System end-of-line sequence */
		break;
	default:
		assert(1 == 0);
	}
	fp->refcnt = 1;

	return fp;
}
/* Create a stream in pipe mode (whatever is written can be
 * read back). These always work in binary mode.
 */
kFILE *
pipeopen(void)
{
	kFILE *fp;

	if((fp = _fcreat()) == NULL)
		return NULL;

	fp->fd = -1;
	fp->type = _FL_PIPE;
	fp->bufmode = _kIOFBF;
	fp->bufsize = kBUFSIZ;

	strcpy(fp->eol,"\r\n");
	return fp;
}
/* Open an asynch port for direct I/O. This must have already been attached
 * as a NOS interface. All packet-mode I/O is suspended until this stream
 * is closed.
 */
kFILE *
asyopen(
char *name,	/* Name of interface */
char *mode	/* Usual fopen-style mode (used only for text/binary) */
){
	kFILE *fp;
	int dev;
	int textmode = 0;

	if((dev = asy_open(name)) == -1)
		return NULL;
	if((fp = _fcreat()) == NULL)
		return NULL;

	if(strchr(mode,'t') != NULL)
		textmode = 1;

	fp->fd = dev;
	fp->type = _FL_ASY;
	fp->bufmode = _kIOFBF;
	fp->flags.ascii = textmode;

	fp->bufsize = kBUFSIZ;
	strcpy(fp->eol,"\r\n");
	return fp;
}
/* Create a new display screen and associate it with a stream. */
kFILE *
displayopen(
char *mode,
int noscrol,
int sfsize
){
	kFILE *fp;
	int textmode = 0;

	if(strchr(mode,'t') != NULL)
		textmode = 1;

	if((fp = _fcreat()) == NULL)
		return NULL;

	fp->fd = -1;
	fp->type = _FL_DISPLAY;
	fp->bufmode = _kIOFBF;
	fp->flags.ascii = textmode;

	fp->ptr = newdisplay(0,0,noscrol,sfsize);
	fp->bufsize = kBUFSIZ;
	strcpy(fp->eol,"\r\n");
	return fp;
}

#ifdef SOUND
/* Open the sound card driver on a stream */
kFILE *
soundopen()
{
	kFILE *fp;

	if((fp = _fcreat()) == NULL)
		return NULL;

	fp->fd = -1;
	fp->type = _FL_SOUND;
	fp->bufmode = _kIOFBF;
	fp->flags.ascii = 0;

	fp->bufsize = kBUFSIZ;
	return fp;
}
#endif

/* Read string from stdin into buf until newline, which is not retained */
char *
kgets(char *s)
{
	int c;
	char *cp;

	cp = s;
	for(;;){
		if((c = kgetc(kstdin)) == kEOF)
			return NULL;

		if(c == '\n')
			break;

		if(s != NULL)
			*cp++ = c;
	}
	if(s != NULL)
		*cp = '\0';
	return s;
}

/* Read a line from a stream into a buffer, retaining newline */
char *
kfgets(
char *buf,	/* User buffer */
int len,	/* Length of buffer */
kFILE *fp	/* Input stream */
){
	int c;
	char *cp;

	cp = buf;
	while(len-- > 1){	/* Allow room for the terminal null */
		if((c = kgetc(fp)) == kEOF){
			return NULL;
		}
		if(buf != NULL)
			*cp++ = c;
		if(c == '\n')
			break;
	}
	if(buf != NULL)
		*cp = '\0';
	return buf;
}

/* Do printf on a stream */
int
kfprintf(kFILE *fp,const char *fmt,...)
{
	va_list args;
	int len;

	va_start(args,fmt);
	len = kvfprintf(fp,fmt,args);
	va_end(args);
	return len;
}
/* Printf on standard output stream */
int
kprintf(const char *fmt,...)
{
	va_list args;
	int len;

	va_start(args,fmt);
	len = kvfprintf(kstdout,fmt,args);
	va_end(args);
	return len;
}
/* variable arg version of printf */
int
kvprintf(const char *fmt, va_list args)
{
	return kvfprintf(kstdout,fmt,args);
}

#ifndef HAVE_FUNOPEN
static void
putter(char c,void *p)
{
	kfputc(c,(kFILE *)p);
}
#else
static int
fun_write(void *p, const char *buf, int sz)
{
	return kfwrite(buf, 1, sz, (kFILE *)p);
}
#endif

int
kvfprintf(kFILE *fp,const char *fmt, va_list args)
{
	if(fp == NULL || fp->cookie != _COOKIE)
		return -1;
#ifdef HAVE_FUNOPEN
	int res = vfprintf(fp->osfp, fmt, args);
	fflush(fp->osfp);
	return res;
#else
	return _format(putter,(void *)fp,fmt, args);
#endif
}

#ifndef USE_SYSTEM_SPRINTF
static void
sputter(char c,void *p)
{
	char **cpp;

	cpp = (char **)p;
	*(*cpp)++ = c;
}

int
vsprintf(char *s,const char *fmt,va_list args)
{
	int r;
	r = _format(sputter,(void *)&s,fmt,args);
	*s = '\0';
	return r;
}
int
sprintf(char *s,const char *fmt,...)
{
	va_list args;
	int len;

	va_start(args,fmt);
	len = kvsprintf(s,fmt,args);
	va_end(args);
	return len;
}
#endif

/* put a char to a stream */ 
int
kfputc(int c,kFILE *fp)
{
	int nbytes;
	struct mbuf *bp;
	int eol;

	if(c == '\n' && fp->flags.ascii){
		nbytes = strlen(fp->eol);
		eol = 1;
	} else {
		nbytes = 1;
		eol = 0;
	}
	bp = fp->obuf;
	if(bp != NULL && bp->size - bp->cnt < nbytes && kfflush(fp) == kEOF)
		return kEOF;
	if(fp->obuf == NULL)
		fp->obuf = ambufw(max(nbytes,fp->bufsize));

	bp = fp->obuf;
	if(eol)
		memcpy(&bp->data[bp->cnt],fp->eol,nbytes);
	else
		bp->data[bp->cnt] = c;
	bp->cnt += nbytes;

	if(bp->cnt == bp->size || (fp->bufmode == _kIONBF)
	 || ((fp->bufmode == _kIOLBF) && eol)){
		if(kfflush(fp) == kEOF)
			return kEOF;
	}
	return c;
}
/* put a string to a stream */
int
kfputs(char *buf,kFILE *fp)
{
	int cnt,len;

	len = strlen(buf);
	cnt = kfwrite(buf,1,len,fp);
	if(cnt != len)
		return kEOF;
	return buf[len-1];
}

/* Put a string to standard output */
int
kputs(char *s)
{
	if(kfputs(s,kstdout) == kEOF)
		return kEOF;
	kputchar('\n');
	return 1;
}

/* Return a conservative estimate of the bytes ready to be read */
int
kfrrdy(kFILE *fp)
{
	/* Just return the bytes on the stdio input buffer */
	if(fp->ibuf != NULL)
		return len_p(fp->ibuf);
	return 0;
}

/* Read a character from the stream */
int
kfgetc(kFILE *fp)
{
	int c;

	if(fp == NULL || fp->cookie != _COOKIE)
		return kEOF;
	c = _kfgetc(fp);
	if(!fp->flags.ascii || c == kEOF || c != fp->eol[0])
		return c;
	/* First char of newline sequence encountered */
	if(fp->eol[1] == '\0')
		return '\n';	/* Translate 1-char eol sequence */
	/* Try to read next input character */
	if((c = _kfgetc(fp)) == kEOF)
		return fp->eol[0];	/* Got a better idea? */
	if(c == fp->eol[1]){
		/* Translate two-character eol sequence into newline */
		return '\n';
	} else {
		/* CR-NUL sequence on Internet -> bare CR (kludge?) */
		if(c != '\0')
			kungetc(c,fp);
		/* Otherwise return first char unchanged */
		return fp->eol[0];
	}
}
/* Read a character from a stream without newline processing */
int
_kfgetc(kFILE *fp)
{
	struct mbuf *bp;

	if(fp == NULL || fp->cookie != _COOKIE)
		return kEOF;
	kfflush(fp);
	if((bp = fp->ibuf) == NULL || bp->cnt == 0)
		if(_fillbuf(fp,1) == NULL)
			return kEOF;
	if(fp->type == _FL_PIPE)
		ksignal(&fp->obuf,1);
	return PULLCHAR(&fp->ibuf);
}

/* Flush output on a stream. All actual output is done here. */
int
kfflush(kFILE *fp)
{
	struct mbuf *bp;
	int cnt;

	if(fp == NULL || fp->cookie != _COOKIE || fp->obuf == NULL)
		return 0;

	bp = fp->obuf;
	fp->obuf = NULL;
	switch(fp->type){
#ifdef SOUND
	case _FL_SOUND:
		return sb_send(&bp);
#endif
	case _FL_ASY:
		return asy_send(fp->fd,&bp);
	case _FL_PIPE:
		append(&fp->ibuf,&bp);
		ksignal(&fp->ibuf,1);
		while(len_p(fp->ibuf) >= kBUFSIZ)
			kwait(&fp->obuf);	/* Hold at hiwat mark */	
		return 0;
	case _FL_SOCK:
		return send_mbuf(fp->fd,&bp,0,NULL,0);
	case _FL_FILE:
		do {
			if(fp->flags.append)
				_LSEEK(fp->fd,0L,kSEEK_END);
			else
				_LSEEK(fp->fd,fp->offset,kSEEK_SET);
			cnt = _WRITE(fp->fd,bp->data,bp->cnt);
			if(cnt > 0)
				fp->offset += cnt;
			if(cnt != bp->cnt){
				fp->flags.err = 1;
				free_p(&bp);
				return kEOF;
			}
			free_mbuf(&bp);
		} while(bp != NULL);
		return 0;
	case _FL_DISPLAY:
		do {
			displaywrite(fp->ptr,bp->data,bp->cnt);
			free_mbuf(&bp);
		} while(bp != NULL);
		return 0;
	}
	return 0;	/* Can't happen */
}

/* Set the end-of-line sequence on a stream */
int
kseteol(kFILE *fp,char *seq)
{
	if(fp == NULL || fp->cookie != _COOKIE)
		return -1;
	if(seq != NULL)
		strncpy(fp->eol,seq,sizeof(fp->eol));
	else
		*fp->eol = '\0';
	return 0;
}
/* Enable/disable eol translation, return previous state */
int
kfmode(kFILE *fp,int mode)
{
	int prev;

	if(fp == NULL || fp->cookie != _COOKIE)
		return -1;
	kfflush(fp);
	prev = fp->flags.ascii;
	fp->flags.ascii = mode;
	return prev;
}
/* Control blocking behavior for fread on network, pipe and asy streams */
int
kfblock(kFILE *fp,int mode)
{
	int prev;

	if(fp == NULL || fp->cookie != _COOKIE)
		return -1;
	prev = fp->flags.partread;
	fp->flags.partread = mode;
	return prev;
}

int
kfclose(kFILE *fp)
{
	if(fp == NULL || fp->cookie != _COOKIE){
		return -1;
	}
	if(--fp->refcnt != 0)
		return 0;	/* Others are still using it */
	_fclose(fp);
	if(fp->prev != NULL)
		fp->prev->next = fp->next;
	else
		_Files = fp->next;

	if(fp->next != NULL)
		fp->next->prev = fp->prev;
	free(fp);
	return 0;
}
int
kfseek(
kFILE *fp,
long offset,
int whence
){
	struct stat statbuf;

	if(fp == NULL || fp->cookie != _COOKIE || fp->type != _FL_FILE){
		kerrno = kEINVAL;
		return -1;
	}
	/* Optimize for do-nothing seek */ 
#ifdef	notdef
	if(whence == kSEEK_SET && fp->offset == offset)
		return 0;
#endif
	kfflush(fp);	/* Flush output buffer */
	/* On relative seeks, adjust for data in input buffer */
	switch(whence){
	case kSEEK_SET:
		fp->offset = offset;	/* Absolute seek */
		break;
	case kSEEK_CUR:
		/* Relative seek, adjusting for buffered data */
		fp->offset += offset - len_p(fp->ibuf);
		break;
	case kSEEK_END:
		/* Find out how big the file currently is */
		if(fstat(fp->fd,&statbuf) == -1)
			return -1;	/* "Can't happen" */
		fp->offset = statbuf.st_size + offset;
		break;
	}
	/* Toss input buffer */
	free_p(&fp->ibuf);
	fp->ibuf = NULL;
	fp->flags.eof = 0;
	return 0;
}
long
kftell(kFILE *fp)
{
	if(fp == NULL || fp->cookie != _COOKIE || fp->type != _FL_FILE)
		return -1;
	kfflush(fp);
	return fp->offset - len_p(fp->ibuf);
}

int
kungetc(int c,kFILE *fp)
{
	if(fp == NULL || fp->cookie != _COOKIE)
		return -1;

	if(c == '\n' && fp->flags.ascii){
		pushdown(&fp->ibuf,fp->eol,strlen(fp->eol));
	} else {
		pushdown(&fp->ibuf,&c,1);
	}
	return c;
}
size_t
kfwrite(
const void *ptr,
size_t size,
size_t n,
kFILE *fp
){
	struct mbuf *bp;
	const uint8 *icp;
	uint8 *ocp;
	size_t bytes;
	size_t cnt;
	size_t asize;
	int room;
	int newlines = 0;
	int eollen = 1;
	int doflush = 0;
	
	if(fp == NULL || fp->cookie != _COOKIE || size == 0)
		return 0;
	icp = ptr;
	if(n == 1)	/* Avoid multiply in common case when n==1 */
		bytes = size;
	else
		bytes = size*n;

	/* Optimization for large binary file writes */
	if(fp->type == _FL_FILE && !fp->flags.ascii && bytes >= fp->bufsize){
		kfflush(fp);
		if(fp->flags.append)
			_LSEEK(fp->fd,0L,kSEEK_END);
		else
			_LSEEK(fp->fd,fp->offset,kSEEK_SET);
		cnt = _WRITE(fp->fd,icp,bytes);
		if(cnt > 0)
			fp->offset += cnt;
		if(cnt != bytes)
			return cnt/size;
		return n;
	}
	if(fp->flags.ascii){
		/* Count the newlines in the input buffer */
		newlines = memcnt(ptr,'\n',bytes);
		if(newlines != 0){
			eollen = strlen(fp->eol);
			if(fp->bufmode == _kIOLBF)
				doflush = 1;
		}
	}
	while(bytes != 0){
		bp = fp->obuf;
		if(bp != NULL && bp->cnt + eollen > bp->size){
			/* Current obuf is full; flush it */
			if(kfflush(fp) == kEOF)
				return (bytes - n*size)/size;
		}
		if((bp = fp->obuf) == NULL){
			/* Allocate a new output buffer. The size is the
			 * larger of the buffer size or the amount of data
			 * we have to write (including any expanded newlines)
			 */
			asize = bytes+(eollen-1)*newlines;
			asize = max(fp->bufsize,asize);
			bp = fp->obuf = ambufw(asize);
		}
		if(fp->flags.ascii && newlines != 0){
			/* Copy text to buffer, expanding newlines */
			ocp = bp->data + bp->cnt;
			room = bp->size - bp->cnt;
			for(;room >= eollen && bytes != 0;icp++,bytes--){
				if(*icp == '\n'){
					memcpy(ocp,fp->eol,eollen);
					ocp += eollen;
					room -= eollen;
					newlines--;
				} else {
					*ocp++ = *icp;
					room--;
				}
			}
			bp->cnt = ocp - bp->data;
		} else {
			/* Simply copy binary data to buffer */
			cnt = min(bp->size - bp->cnt,bytes);
			memcpy(bp->data+bp->cnt,icp,cnt);
			bp->cnt += cnt;
			icp += cnt;
			bytes -= cnt;
		}
	}
	/* The final flush. Flush if the stream is unbuffered,
	 * the output buffer is full, or the stream is line buffered
	 * and we've written at least one newline (not necessarily the
	 * last character)
	 */
	if(fp->bufmode == _kIONBF || bp->cnt == bp->size || doflush){
		if(kfflush(fp) == kEOF)
			return (bytes - n*size)/size;
	}
	return n;
}
static struct mbuf *
_fillbuf(kFILE *fp,int cnt)
{
	struct mbuf *bp;
	int i;

	if(fp->ibuf != NULL)
		return fp->ibuf;	/* Stuff already in the input buffer */

	switch(fp->type){
#ifdef SOUND
	case _FL_SOUND:
		fp->ibuf = sb_recv();
		return fp->ibuf;
#endif
	case _FL_ASY:
		fp->ibuf = ambufw(kBUFSIZ);
		i = asy_read(fp->fd,fp->ibuf->data,kBUFSIZ);
		if(i < 0)
			return NULL;
		fp->ibuf->cnt = i;
		return fp->ibuf;
	case _FL_PIPE:
		while(fp->ibuf == NULL)
			if((kerrno = kwait(&fp->ibuf)) != 0)	/* Wait for something */
				return NULL;
		return fp->ibuf;
	case _FL_SOCK:
		/* Always grab everything available from a socket */
		if(recv_mbuf(fp->fd,&fp->ibuf,0,NULL,0) <= 0
		 && kerrno != kEALARM){
			fp->flags.eof = 1;
		}
		return fp->ibuf;
	case _FL_FILE:
		/* Read from file */
		cnt = max(fp->bufsize,cnt);
		bp = ambufw(cnt);		
		_LSEEK(fp->fd,fp->offset,kSEEK_SET);
		cnt = _READ(fp->fd,bp->data,cnt);
		if(cnt < 0)
			fp->flags.err = 1;
		if(cnt == 0)
			fp->flags.eof = 1;
		if(cnt <= 0){
			free_p(&bp);	/* Nothing successfully read */
			return NULL;
		}
		fp->offset += cnt;	/* Update pointer */
		/* Buffer successfully read, store it */
		bp->cnt = cnt;
		fp->ibuf = bp;
		return bp;
	case _FL_DISPLAY:	/* Displays are write-only */
		return NULL;
	}
	return NULL;	/* Can't happen */
}
size_t
kfread(
void *ptr,
size_t size,
size_t n,
kFILE *fp
){
	struct mbuf *bp;
	size_t bytes;
	size_t cnt;
	int c;
	size_t tot = 0;
	uint8 *ocp;
	uint8 *cp;

	if(fp == NULL || fp->cookie != _COOKIE || size == 0)
		return 0;
	kfflush(fp);
	bytes = n*size;

	ocp = ptr;
	while(bytes != 0){
		/* Optimization for large binary file reads */
		if(fp->ibuf == NULL
		 && fp->type == _FL_FILE && !fp->flags.ascii
		 && bytes >= kBUFSIZ){
			_LSEEK(fp->fd,fp->offset,kSEEK_SET);
			tot = _READ(fp->fd,ocp,bytes);
			if(tot > 0)
				fp->offset += tot;
			if(tot != bytes)
				return tot/size;
			return n;
		}
		/* Replenish input buffer if necessary */
		if(fp->ibuf == NULL){
			if(tot != 0 && fp->flags.partread){
				/* Would block for more data */
				return tot/size;	
			}
		 	if(_fillbuf(fp,bytes) == NULL){
				/* eof or error */
				return tot/size;
			}
		}
		/* In this pass, read the lesser of the buffer size,
		 * the requested amount, or the amount up to the next
		 * eol sequence (if ascii mode)
		 */
		bp = fp->ibuf;
		cnt = min(bp->cnt,bytes);
		if(fp->flags.ascii
		 && (cp = memchr(bp->data,fp->eol[0],cnt)) != NULL)
			cnt = min(cnt,cp - bp->data);
		if(cnt != 0){
			cnt = pullup(&fp->ibuf,ocp,cnt);
			ocp += cnt;
			tot += cnt;
			bytes -= cnt;
		} else {
			/* Hit a eol sequence, use fgetc to translate */
			if((c = kfgetc(fp)) == kEOF)
				return tot/size;

			*ocp++ = c;
			tot++;
			bytes--;
		}
	}
	if(fp->type == _FL_PIPE)
		ksignal(&fp->obuf,1);
	return n;
}
void
kperror(const char *s)
{
	kfprintf(kstderr,"%s: errno %d",s,kerrno);
	if(kerrno < ksys_nerr)
		kfprintf(kstderr,": %s\n",ksys_errlist[kerrno]);
	else
		kfprintf(kstderr,"\n");
}
int
ksetvbuf(
kFILE *fp,
char *buf,	/* Ignored; we alloc our own */
int type,
int size
){
	if(fp == NULL || fp->cookie != _COOKIE)
		return -1;
	kfflush(fp);
	if(size == 0)
		type = _kIONBF;
	switch(type){
	case _kIOFBF:
		fp->bufsize = size;
		break;
	case _kIOLBF:
		fp->bufsize = size;
		break;
	case _kIONBF:
		fp->bufsize = 1;
		break;
	default:
		return -1;	/* Invalid */
	}
	fp->bufmode = type;
	return 0;
}
void
ksetbuf(kFILE *fp,char *buf)
{
	if(buf == NULL)
		ksetvbuf(fp,NULL,_kIONBF,0);
	else
		ksetvbuf(fp,buf,_kIOFBF,kBUFSIZ);
}
kFILE *
ktmpfile(void)
{
	static int num;
	struct stat statbuf;
	kFILE *fp;
	char *fname;
	char *tmpdir;
	char *cp;

	/* Determine directory to use. First look for $TMP environment
	 * variable, then use the compiled-in-default, then use the
	 * current directory.
	 */
	if((cp = getenv("TMP")) != NULL
	 && stat(cp,&statbuf) == 0 && (statbuf.st_mode & S_IFDIR)){
		fname = malloc(strlen(cp) + 11);
		tmpdir = malloc(strlen(cp) + 2);
		strcpy(tmpdir,cp);
		strcat(tmpdir,"/");
	} else if(stat(Tmpdir,&statbuf) == 0 && (statbuf.st_mode & S_IFDIR)){
		fname = malloc(strlen(Tmpdir) + 11);
		tmpdir = malloc(strlen(Tmpdir) + 2);
		strcpy(tmpdir,Tmpdir);
		strcat(tmpdir,"/");
	} else {
		fname = malloc(10);
		tmpdir = strdup("");
	}
	for(;;){
		sprintf(fname,"%stemp.%03d",tmpdir,num);
		if(stat(fname,&statbuf) == -1 && errno == ENOENT)
			break;
		num++;
	}
	free(tmpdir);
	fp = kfopen(fname,"w+b");
	free(fname);
	if(fp != NULL)
		fp->flags.tmp = 1;
	return fp;
}
/* Do everything to close a stream except freeing the descriptor
 * The reference count is left unchanged, and the descriptor is still
 * on the list
 */
static void
_fclose(kFILE *fp)
{
	struct stat statbuf;
	char *buf;
	long i;
	int n;

	if(fp == NULL || fp->cookie != _COOKIE)
		return;
	if(_clrtmp && fp->flags.tmp){
		/* Wipe temp file for security */
		krewind(fp);
		fstat(kfileno(fp),&statbuf);
		buf = malloc(kBUFSIZ);
		memset(buf,0,kBUFSIZ);
		i = statbuf.st_size;
		while(i > 0){
			n = kfwrite(buf,1,min(i,kBUFSIZ),fp);
			kwait(NULL);
			if(n < kBUFSIZ)
				break;
			i -= n;
		}
		free(buf);
	}
	kfflush(fp);
	switch(fp->type){
	case _FL_ASY:
		asy_close(fp->fd);
		break;
	case _FL_SOCK:
		close_s(fp->fd);
		break;
	case _FL_FILE:
		_CLOSE(fp->fd);
		fp->offset = 0;
		break;
	case _FL_DISPLAY:
		closedisplay(fp->ptr);
		fp->ptr = NULL;
		break;
	default:
		break;
	}
	free_p(&fp->obuf);	/* Should be NULL anyway */
	free_p(&fp->ibuf);
	if(fp->flags.tmp)
		unlink(fp->ptr);
	free(fp->ptr);
	fp->ptr = NULL;
	fp->flags.err = fp->flags.eof = fp->flags.ascii = 0;
	fp->flags.append = fp->flags.tmp = fp->flags.partread = 0;
	fp->fd = -1;
#ifdef HAVE_FUNOPEN
	fclose(fp->osfp);
#endif
}
/* allocate a new file pointer structure, init a few fields and put on list */
static kFILE *
_fcreat(void)
{
	kFILE *fp;

	if((fp = (kFILE *)calloc(1,sizeof(kFILE))) == NULL)
		return NULL;

	fp->cookie = _COOKIE;
	fp->refcnt = 1;
	fp->next = _Files;
#ifdef HAVE_FUNOPEN
	fp->osfp = funopen(fp, NULL, fun_write, NULL, NULL);
#endif
	_Files = fp;
	if(fp->next != NULL)
		fp->next->prev = fp;
	return fp;
}

int
kread(int fd,void *buf,unsigned cnt)
{
	int type = _fd_type(fd);
	int res;

	if(fd < 0){
		kerrno = kEINVAL;
		return -1;
	}
	switch(type){
	case _FL_FILE:
		res = (int)_READ(fd,buf,cnt);
		if (res == -1)
			kerrno = translate_sys_errno(errno);
		return res;
	case _FL_SOCK:
		return krecv(fd,buf,cnt,0);
	case _FL_ASY:
		return asy_read(fd,buf,cnt);
	default:
		kerrno = kEINVAL;
		return -1;
	}
}
int
kwrite(int fd,const void *buf,unsigned cnt)
{
	int type = _fd_type(fd);
	int res;

	if(fd < 0){
		kerrno = kEINVAL;
		return -1;
	}
	switch(type){
	case _FL_FILE:
		res = (int)_WRITE(fd,buf,cnt);
		if (res == -1)
			kerrno = translate_sys_errno(errno);
		return res;
	case _FL_SOCK:
		return ksend(fd,buf,cnt,0);
	case _FL_ASY:
		return asy_write(fd,buf,cnt);
	default:
		kerrno = kEINVAL;
		return -1;
	}
}

/* This entry point is provided for applications that want to call open()
 * directly, instead of using fopen()
 */
#ifndef UNIX
int
open(const char *file,int mode,...)
{
	return _open(file,mode);
}
#endif

int
kclose(int fd)
{
	int type = _fd_type(fd);

	if(fd < 0){
		kerrno = kEINVAL;
		return -1;
	}
	switch(type){
	case _FL_FILE:
		return _CLOSE(fd);
	case _FL_SOCK:
		return close_s(fd);
	case _FL_ASY:
		return asy_close(fd);
	default:
		kerrno = kEINVAL;
		return -1;
	}
}

void
kfcloseall(void)
{
	kFILE *fp,*fpnext;

	kflushall();
	for(fp = _Files;fp != NULL;fp=fpnext){
		fpnext = fp->next;
		kfclose(fp);
	}
}
void
kflushall(void)
{
	kFILE *fp;

	for(fp = _Files;fp != NULL;fp=fp->next){
		kfflush(fp);
	}
}
kFILE *
kfdup(kFILE *fp)
{
	kFILE *nfp;

	if(fp == NULL || fp->cookie != _COOKIE)
		return NULL;	/* Invalid arg */
	switch(fp->type){
	case _FL_FILE:
		/* Allocate new file pointer structure so each can
		 * have its own read/write pointer and buffering
		 */
		if((nfp = _fcreat()) == NULL)
			return NULL;
		nfp->fd = _DUP(fp->fd);
		nfp->offset = fp->offset;
		nfp->type = fp->type;
		nfp->bufmode = fp->bufmode;
		nfp->flags = fp->flags;
		strcpy(nfp->eol,fp->eol);
		nfp->bufsize = fp->bufsize;
		nfp->ptr = strdup(fp->ptr);
		fp = nfp;
		break;
	default:	/* These just share the same file pointer */
		fp->refcnt++;
		break;
	}
	return fp;
}
char *
kfpname(kFILE *fp)
{
	if(fp == NULL || fp->cookie != _COOKIE)
		return NULL;
	if(fp->type == _FL_FILE)
		return fp->ptr;
	return NULL;
}


int
dofiles(int argc,char *argv[],void *p)
{
	kFILE *fp;
	int i;

	kprintf("       fp   fd   ref      eol   type mod buf  flags\n");
	for(fp = _Files;fp != NULL;fp = fp->next){
		kprintf("%9p ",fp);
		if(fp->fd != -1)
			kprintf("%4d",fp->fd);
		else
			kprintf("    ");
		kprintf("%6d ",fp->refcnt);
		for(i=0;i<EOL_LEN-1;i++){
			if(fp->eol[i] != '\0')
				kprintf("   %02x",fp->eol[i]);
			else
				kprintf("     ");
		}
		switch(fp->type){
		case _FL_SOCK:
			kprintf(" sock");
			break;
		case _FL_FILE:
			kprintf(" file");
			break;
		case _FL_DISPLAY:
			kprintf(" disp");
			break;
		case _FL_PIPE:
			kprintf(" pipe");
			break;
		case _FL_ASY:
			kprintf(" asy ");
			break;
#ifdef SOUND
		case _FL_SOUND:
			kprintf(" snd ");
			break;
#endif
		}
		kprintf("%4s",fp->flags.ascii ? " txt" : " bin");
		switch(fp->bufmode){
		case _kIONBF:
			kprintf(" none");
			break;
		case _kIOLBF:
			kprintf(" line");
			break;
		case _kIOFBF:
			kprintf(" full");
			break;
		}
		if(fp->flags.eof)
			kprintf(" EOF");
		if(fp->flags.err)
			kprintf(" ERR");
		if(fp->flags.append)
			kprintf(" APND");
		if(fp->flags.tmp)
			kprintf(" TMP");
		if(fp->type == _FL_FILE && fp->ptr != NULL)
			kprintf(" (%s seek=%lu)",(char *)fp->ptr,kftell(fp));
		kputchar('\n');
	}
	return 0;
}
