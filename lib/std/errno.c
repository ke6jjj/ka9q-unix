/* Host error to NET error conversion.
 *
 * Copyright 1991 Phil Karn, KA9Q
 * Copyright 2017 Jeremy Cooper, KE6JJJ
 */
#include "top.h"

#include <errno.h>
#include "lib/std/errno.h"

int
translate_sys_errno(int err)
{
	switch (err) {
	case EINVAL: return kEINVAL;
	case EWOULDBLOCK: return kEWOULDBLOCK;
	case ENOTCONN: return kENOTCONN;
	case ESOCKTNOSUPPORT: return kESOCKTNOSUPPORT;
	case EAFNOSUPPORT: return kEAFNOSUPPORT;
	case EISCONN: return kEISCONN;
	case EOPNOTSUPP: return kEOPNOTSUPP;
	case EINTR: return kEINTR;
	case ECONNREFUSED: return kECONNREFUSED;
	case EMSGSIZE: return kEMSGSIZE;
	case EADDRINUSE: return kEADDRINUSE;
	case EBADF: return kEBADF;
	case EMFILE: return kEMFILE;
	case EFAULT: return kEFAULT;
	case ENOMEM: return kENOMEM;
	default:
		return kEINVAL;
	}
}

