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
	"invalid argument",             /*  1 - EINVAL */
	"operation would block",        /*  2 - EWOULDBLOCK */
	"not connected",                /*  3 - ENOTCONN */
	"socket type not supported",    /*  4 - ESOCKTNOSUPPORT */
	"address family not supported", /*  5 - EAFNOSUPPORT */
	"is connected",                 /*  6 - EISCONN */
	"operation not supported",      /*  7 - EOPNOTSUPP */
	"alarm",                        /*  8 - EALARM */
	"abort",                        /*  9 - EABORT */
	"interrupt",                    /* 10 - EINTR */
	"connection refused",           /* 11 - ECONNREFUSED */
	"message size",                 /* 12 - EMSGSIZE */
	"address in use",               /* 13 - EADDRINUSE */
	"bad file descriptor",          /* 14 - EBADF */
	"too many files open",          /* 15 - EMFILE */
	"bad address",                  /* 16 - EFAULT */
	"out of memory"                 /* 17 - kENOMEM */
};
