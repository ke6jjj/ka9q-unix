#ifndef	_KA9Q_STDIO_H
#define	_KA9Q_STDIO_H

#ifndef __TURBOC__
/* Include system stdio for sprintf and sscanf variants */
#include <stdio.h>
#endif

#ifdef MODERN_UNIX
#include <stdarg.h>
#endif

#ifndef	_GLOBAL_H
#include "global.h"
#endif

#define	EOL_LEN	3

#define	_fd_type(fd)	(((fd) >> 13) & 3)
#define	_fd_seq(fd)	((fd) & 8191)
#define	_mk_fd(fd,type)	((fd) | ((type) << 13))

struct _file{
	unsigned cookie;		/* Detect bogus file pointers */
#define	_COOKIE	0xdead
	int refcnt;
	struct _file *prev;
	struct _file *next;

	int fd;			/* File, socket or asy descriptor */
	long offset;		/* Seek offset, type == _FL_FILE only */

	enum {
		_FL_FILE,	/* Associated with file */
		_FL_SOCK,	/* Associated with network socket */
		_FL_ASY,	/* Asynch port */
		_FL_DISPLAY,	/* Associated with display driver */
		_FL_PIPE	/* Pipe mode */
#ifdef SOUND
,		_FL_SOUND	/* Sound mode */
#endif
	} type;

	enum {
		_kIOFBF=1,	/* Full buffering */
		_kIOLBF,	/* Line buffering */
		_kIONBF		/* No buffering */
	} bufmode;		/* Output buffering mode */

	struct {
		unsigned int err:1;	/* Error on stream */
		unsigned int eof:1;	/* EOF seen */
		unsigned int ascii:1;	/* Ascii (newline translate) mode */
		unsigned int append:1;	/* Always seek to end before writing */
		unsigned int tmp:1;	/* Delete on close */
		unsigned int partread:1;/* Allow partial reads from fread() */
	} flags;
	struct mbuf *obuf;	/* Output buffer */
	struct mbuf *ibuf;	/* Input buffer */
	char eol[EOL_LEN];	/* Text mode end-of-line sequence, if any */
	int bufsize;		/* Size of buffer to use */
	void *ptr;		/* File name or display pointer */
#ifdef HAVE_FUNOPEN
	FILE *osfp;
#endif
};

typedef struct _file kFILE;

#ifndef NULL
#define	NULL	0
#endif
#define	kBUFSIZ	2048
#define	kEOF	(-1)

#define	kSEEK_SET	0
#define	kSEEK_CUR	1
#define	kSEEK_END	2

#ifndef _PROC_H
#include "proc.h"
#endif

#define	kstdout	Curproc->output
#define	kstdin	Curproc->input
#define	kstderr	Curproc->output

#define	STREAM_BINARY	0
#define	STREAM_ASCII	1

#define	FULL_READ	0
#define	PART_READ	1

kFILE *asyopen(char *name,char *mode);
int kclose(int fd);
kFILE *displayopen(char *mode,int noscrol,int sfsize);
int kfblock(kFILE *fp,int mode);
int kfclose(kFILE *fp);
void kfcloseall(void);
kFILE *kfdopen(int handle,char *mode);
kFILE *kfdup(kFILE *fp);
int kfflush(kFILE *fp);
int kfgetc(kFILE *fp);
int _kfgetc(kFILE *fp);
char *kfgets(char *buf,int len,kFILE *fp);
void kflushall(void);
int kfmode (kFILE *fp,int mode);
char *kfpname(kFILE *fp);
int kfprintf(kFILE *fp,const char *fmt,...);
int kfputc(int c,kFILE *fp);
int kfputs(char *buf,kFILE *fp);
size_t kfread(void *ptr,size_t size,size_t n,kFILE *fp);
int kfrrdy(kFILE *fp);
kFILE *kfreopen(char *name,char *mode,kFILE *fp);
int kfseek(kFILE *fp,long offset,int whence);
long kftell(kFILE *fp);
size_t kfwrite(const void *ptr,size_t size,size_t n,kFILE *fp);
char *kgets(char *s);
void kperror(const char *s);
kFILE *pipeopen(void);
int kprintf(const char *fmt,...);
int kputs(char *s);
#ifndef NO_STD_DUPLICATION
int rename(const char *,const char *); /* From regular library */
#endif
void ksetbuf(kFILE *fp,char *buf);
int kseteol(kFILE *fp,char *seq);
int ksetvbuf(kFILE *fp,char *buf,int type,int size);
#ifndef USE_SYSTEM_SPRINTF
int sprintf(char *,const char *, ...);
#endif
#ifndef NO_STD_DUPLICATION
int sscanf(char *,char *,...);	/* From regular library */
#endif
kFILE *soundopen(void);
kFILE *ktmpfile(void);
#ifndef NO_STD_DUPLICATION
char *tmpnam(char *);	/* From regular library */
#endif
int kungetc(int c,kFILE *fp);
#ifndef NO_STD_DUPLICATION
int unlink(const char *);	/* From regular library */
#endif
int kvfprintf(kFILE *fp,const char *fmt, va_list args);
int kvprintf(const char *fmt, va_list args);
#ifndef USE_SYSTEM_SPRINTF
int vsprintf(char *,const char *,va_list);
#endif

extern int _clrtmp;	/* Flag controlling wipe of temporary files on close */

/* Macros */
#define	kfeof(fp)	((fp)->flags.eof)
#define kferror(fp)	((fp)->flags.err)
#define	kfileno(fp)	((fp) != NULL ? (fp)->fd : -1)
#define kfopen(s,m)	(kfreopen((s),(m),NULL))
#define	kputc(c,fp)	(kfputc((c),(fp)))
#define	kgetc(fp)	(kfgetc((fp)))
#define	kgetchar()	(kgetc(kstdin))
#define	kclearerr(fp)	((fp)->flags.eof = (fp)->flags.err = 0)
#define krewind(fp)	((void)kfseek((fp),0L,kSEEK_SET),kclearerr((fp)))
#define	kputchar(c)	(kputc((c),kstdout))

#endif /* _KA9Q_STDIO_H */
