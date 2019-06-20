/* System error list
 *
 * Copyright 1991 Phil Karn, KA9Q
 * Copyright 2017, 2019 Jeremy Cooper, KE6JJJ
 */
#include "top.h"

#include "errno.h"

int ksys_nerr = kEMAX + 1;

const char *ksys_errlist[] = {
	"no error",                     /*  0 */
	"invalid argument",             /*  1 - kEINVAL */
	"operation would block",        /*  2 - kEWOULDBLOCK */
	"not connected",                /*  3 - kENOTCONN */
	"socket type not supported",    /*  4 - kESOCKTNOSUPPORT */
	"address family not supported", /*  5 - kEAFNOSUPPORT */
	"is connected",                 /*  6 - kEISCONN */
	"operation not supported",      /*  7 - kEOPNOTSUPP */
	"alarm",                        /*  8 - kEALARM */
	"abort",                        /*  9 - kEABORT */
	"interrupt",                    /* 10 - kEINTR */
	"connection refused",           /* 11 - kECONNREFUSED */
	"message size",                 /* 12 - kEMSGSIZE */
	"address in use",               /* 13 - kEADDRINUSE */
	"bad file descriptor",          /* 14 - kEBADF */
	"too many files open",          /* 15 - kEMFILE */
	"bad address",                  /* 16 - kEFAULT */
	"out of memory"                 /* 17 - kENOMEM */
};
