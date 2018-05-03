#ifndef _GLOBAL_H
#define	_GLOBAL_H

#include <stdlib.h>
#include <string.h>

/* Global definitions used by every source file.
 * Some may be compiler dependent.
 *
 * This file depends only on internal macros or those defined on the
 * command line, so it may be safely included first.
 */

#if	!defined(AMIGA) && (defined(MAC) || defined(MSDOS))
/* These compilers require special kopen modes when reading binary files.
 *
 * "The single most brilliant design decision in all of UNIX was the
 * choice of a SINGLE character as the end-of-line indicator" -- M. O'Dell
 *
 * "Whoever picked the end-of-line conventions for MS-DOS and the Macintosh
 * should be shot!" -- P. Karn's corollary to O'Dell's declaration
 */
#define	READ_BINARY	"rb"
#define	WRITE_BINARY	"wb"
#define	APPEND_BINARY	"ab+"
#define	READ_TEXT	"rt"
#define	WRITE_TEXT	"wt"
#define	APPEND_TEXT	"at+"

#else

#define	READ_BINARY	"r"
#define	WRITE_BINARY	"w"
#define	APPEND_BINARY	"a+"
#define	READ_TEXT	"r"
#define	WRITE_TEXT	"w"
#define	APPEND_TEXT	"a+"

#endif

#include <sys/types.h>

#ifndef HAVE_STDINT
/* These two lines assume that your compiler's longs are 32 bits and
 * shorts are 16 bits. It is already assumed that chars are 8 bits,
 * but it doesn't matter if they're signed or unsigned.
 */
typedef long int32;		/* 32-bit signed integer */
typedef unsigned int uint;	/* 16 or 32-bit unsigned integer */
typedef unsigned long uint32;	/* 32-bit unsigned integer */
typedef unsigned short uint16;	/* 16-bit unsigned integer */
typedef unsigned char byte_t;	/*  8-bit unsigned integer */
typedef unsigned char uint8;	/* 8-bit unsigned integer */
typedef unsigned long long uint64;	/* 64-bit unsigned integer */
#define	MAXINT16 0xffff		/* Largest 16-bit integer */
#define	MAXINT32 0xffffffff	/* Largest 32-bit integer */
#define	NBBY	8		/* 8 bits/byte */
#else
#include <stdint.h>
#include <limits.h>
typedef int32_t int32;		/* 32-bit signed integer */
typedef unsigned int uint;	/* 16 or 32-bit unsigned integer */
typedef uint32_t uint32;	/* 32-bit unsigned integer */
typedef uint16_t uint16;	/* 16-bit unsigned integer */
typedef uint8_t byte_t;		/*  8-bit unsigned integer */
typedef uint8_t uint8;		/* 8-bit unsigned integer */
typedef uint64_t uint64;	/* 64-bit unsigned integer */
#define	MAXINT16 UINT16_MAX	/* Largest 16-bit integer */
#define	MAXINT32 UINT32_MAX 	/* Largest 32-bit integer */
#ifndef NBBY
#define NBBY CHAR_BIT           /* Number of bits per byte */
#endif
#endif


#define	HASHMOD	7		/* Modulus used by hash_ip() function */

/* The "interrupt" keyword is non-standard, so make it configurable */
#if	defined(__TURBOC__) && defined(MSDOS)
#define	INTERRUPT	void interrupt
#else
#define	INTERRUPT	void
#endif

/* Note that these definitions are on by default if none of the Turbo-C style
 * memory model definitions are on; this avoids having to change them when
 * porting to 68K environments.
 */
#if	!defined(__TINY__) && !defined(__SMALL__) && !defined(__MEDIUM__) && !defined(__GNUC__)
#define	LARGEDATA	1
#endif

#if	!defined(__TINY__) && !defined(__SMALL__) && !defined(__COMPACT__) && !defined(__GNUC__)
#define	LARGECODE	1
#endif

/* Since not all compilers support structure assignment, the ASSIGN()
 * macro is used. This controls how it's actually implemented.
 */
#ifdef	NOSTRUCTASSIGN	/* Version for old compilers that don't support it */
#define	ASSIGN(a,b)	memcpy((char *)&(a),(char *)&(b),sizeof(b);
#else			/* Version for compilers that do */
#define	ASSIGN(a,b)	((a) = (b))
#endif

/* Define null object pointer in case stdio.h isn't included */
#ifndef	NULL
/* General purpose NULL pointer */
#define	NULL 0
#endif

/* standard boolean constants */
#define FALSE 0
#define TRUE 1
#define NO 0
#define YES 1

#define CTLA 0x1
#define CTLB 0x2
#define CTLC 0x3
#define CTLD 0x4
#define CTLE 0x5
#define CTLF 0x6
#define CTLG 0x7
#define CTLH 0x8
#define CTLI 0x9
#define CTLJ 0xa
#define CTLK 0xb
#define CTLL 0xc
#define CTLM 0xd
#define CTLN 0xe
#define CTLO 0xf
#define CTLP 0x10
#define CTLQ 0x11
#define CTLR 0x12
#define CTLS 0x13
#define CTLT 0x14
#define CTLU 0x15
#define CTLV 0x16
#define CTLW 0x17
#define CTLX 0x18
#define CTLY 0x19
#define CTLZ 0x1a

#define	BELL	CTLG
#define	BS	CTLH
#define	TAB	CTLI
#define	LF	CTLJ
#define	FF	CTLL
#define	CR	CTLM
#define	XON	CTLQ
#define	XOFF	CTLS
#define	ESC	0x1b
#define	DEL	0x7f

/* string comparison portability */
#ifdef MODERN_UNIX
#define STRNICMP(a,b,c)	strncasecmp((a),(b),(c))
#define STRICMP(a,b)	strcasecmp((a),(b))
#else
#define STRNICMP(a,b,c)	strnicmp((a),(b),(c))
#define STRICMP(a,b)	stricmp((a),(b))
#endif

/* string equality shorthand */
#define STREQ(x,y) (strcmp(x,y) == 0)

/* Extract a short from a long */
#define	hiword(x)	((uint16)((x) >> 16))
#define	loword(x)	((uint16)(x))

/* Extract a byte from a short */
#define	hibyte(x)	((unsigned char)((x) >> 8))
#define	lobyte(x)	((unsigned char)(x))

/* Extract nibbles from a byte */
#define	hinibble(x)	(((x) >> 4) & 0xf)
#define	lonibble(x)	((x) & 0xf)

/* Various low-level and miscellaneous functions */
int availmem(void);
#ifndef USE_SYSTEM_MALLOC
void *callocw(unsigned nelem,unsigned size);
#else
#define callocw(nelem,size) calloc((nelem),(size))
#endif
int dirps(void);
#ifndef USE_SYSTEM_MALLOC
void free(void *);
#endif
#define FREE(p)		{free(p); p = NULL;}
int kgetopt(int argc,char *argv[],char *opts);
void getrand(unsigned char *buf,int len);
int htob(char c);
int htoi(char *);
int readhex(uint8 *,char *,int);
long htol(char *);
char *inbuf(uint port,char *buf,uint cnt);
uint hash_ip(int32 addr);
int istate(void);
#ifdef UNIX
int enable(void);
int disable(void);
void giveup(void);
#endif
void logmsg(int s,char *fmt, ...);
int ilog2(uint x);
void *htop(const char *);
#ifndef USE_SYSTEM_MALLOC
void *malloc(size_t nb);
void *mallocw(size_t nb);
#else
#define mallocw(nb) malloc((nb))
#endif
int memcnt(const uint8 *buf,uint8 c,int size);
void memxor(uint8 *,uint8 *,unsigned int);
char *outbuf(uint port,char *buf,uint cnt);
int32 rdclock(void);
void restore(int);
void rip(char *);
char *smsg(char *msgs[],unsigned nmsgs,unsigned n);
void stktrace(void);
#if	!defined __TURBOC__ && !defined(HAVE_STRDUP)
char *strdup(const char *);
#endif
int urandom(unsigned int n);
int wildmat(char *s,char *p,char **argv);

#ifdef	AZTEC
#define	krewind(fp)	kfseek(fp,0L,0);
#endif

#if	defined(__TURBOC__) && defined(MSDOS)
#define movblock(so,ss,do,ds,c)	movedata(ss,so,ds,do,c)

#else

/* General purpose function macros already defined in turbo C */
#ifndef	min
#define	min(x,y)	((x)<(y)?(x):(y))	/* Lesser of two args */
#endif
#ifndef max
#define	max(x,y)	((x)>(y)?(x):(y))	/* Greater of two args */
#endif
#ifdef	MSDOS
#define MK_FP(seg,ofs)	((void far *) \
			(((unsigned long)(seg) << 16) | (unsigned)(ofs)))
#endif
#endif	/* __TURBOC __ */

#ifdef	AMIGA
/* super kludge de WA3YMH */
#ifndef	fileno
#include <stdio.h>
#endif
#define kfclose(fp)	amiga_fclose(fp)
extern int amiga_fclose(kFILE *);
extern kFILE *ktmpfile(void);

extern char *ksys_errlist[];
extern int kerrno;
#endif

/* Externals used by getopt */
extern int koptind;
extern char *koptarg;

/* Threshold setting on available memory */
extern int32 Memthresh;

#ifndef HAVE_UNIX_TIMEOFDAY
/* System clock - count of ticks since startup */
extern int32 Clock;
#endif

/* Various useful strings */
extern char Badhost[];
extern char Nospace[];
extern char Notval[];
extern char *Hostname;
extern char Version[];
extern char Whitespace[];

/* Your system's end-of-line convention */
extern char Eol[];

/* Your system OS - set in files.c */
extern char System[];

/* Your system's temp directory */
extern char *Tmpdir;

extern unsigned Nfiles;	/* Maximum number of kopen files */
extern unsigned Nsock;	/* Maximum number of kopen sockets */

extern void (*Gcollect[])();

#endif	/* _GLOBAL_H */
