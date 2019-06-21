#ifndef	_ERRNO_H
#define	_ERRNO_H

#define kerrno (Curproc->perrno)

extern int ksys_nerr;
extern const char *ksys_errlist[];

#define	kEMIN		1
#define	kEINVAL		1
#define	kEWOULDBLOCK	2
#define	kENOTCONN	3
#define	kESOCKTNOSUPPORT	4
#define	kEAFNOSUPPORT	5
#define	kEISCONN	6
#define	kEOPNOTSUPP	7
#define	kEALARM		8
#define	kEABORT		9
#define	kEINTR		10
#define	kECONNREFUSED	11
#define	kEMSGSIZE	12
#define	kEADDRINUSE	13
#define	kEBADF		14
#define	kEMFILE		15
#define	kEFAULT		16
#define	kENOMEM		17
#define	kEMAX		17

/* Translate native error numbers to KA9Q errno */
int translate_sys_errno(int);

#endif	/* _ERRNO_H */
