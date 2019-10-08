/* RIP packet tracing
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "../../top.h"

#include "../../global.h"
#include "../../mbuf.h"
#include "../../timer.h"
#include "../../trace.h"

#include "../../lib/inet/netuser.h"
#include "../../service/rip/rip.h"

void
rip_dump(fp,bpp)
kFILE *fp;
struct mbuf **bpp;
{
	struct rip_route entry;
	int i;
	int cmd,version;
	uint len;
	
	kfprintf(fp,"RIP: ");
	cmd = PULLCHAR(bpp);
	version = PULLCHAR(bpp);
	switch(cmd){
	case RIPCMD_REQUEST:
		kfprintf(fp,"REQUEST");
		break;
	case RIPCMD_RESPONSE:
		kfprintf(fp,"RESPONSE");
		break;
	default:
		kfprintf(fp," cmd %u",cmd);
		break;
	}

	pull16(bpp);	/* remove one word of padding */

	len = len_p(*bpp);
	kfprintf(fp," vers %u entries %u:\n",version,len / RIPROUTE);

	i = 0;
	while(len >= RIPROUTE){
		/* Pull an entry off the packet */
		pullentry(&entry,bpp);
		len -= RIPROUTE;

		if(entry.addr_fam != RIP_IPFAM) {
			/* Skip non-IP addresses */
			continue;
		}
		kfprintf(fp,"%-16s%-3u ",inet_ntoa(entry.target),entry.metric);
		if((++i % 3) == 0){
			kputc('\n',fp);
		}
	}
	if((i % 3) != 0)
		kputc('\n',fp);
}
