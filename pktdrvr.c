/* Driver for FTP Software's packet driver interface. (PC specific code)
 * Rewritten Feb 1996 for DPMI with DJGPP Phil Karn
 */
#include "top.h"

#include <sys/types.h>
#include "stdio.h"
#include <go32.h>
#include <dos.h>
#include <dpmi.h>

#include "global.h"
#include "proc.h"
#include "mbuf.h"
#include "netuser.h"
#include "enet.h"
#include "arcnet.h"
#include "ax25.h"
#include "slip.h"
#include "kiss.h"
#include "iface.h"
#include "arp.h"
#include "trace.h"
#include "pktdrvr.h"
#include "config.h"
#include "devparam.h"

#define	C_FLAG	1
static long access_type(int intno,int if_class,int if_type,
	int if_number, uint rm_segment,uint rm_offset );
static int driver_info(int intno,int handle,int *version,
	int *class,int *type,int *number,int *basic);
static int release_type(int intno,int handle);
static int get_address(int intno,int handle,uint8 *buf,int len);
static int set_rcv_mode(int intno,int handle,int mode);
static int pk_raw(struct iface *iface,struct mbuf **bpp);
static int pk_stop(struct iface *iface);
static void pkint(_go32_dpmi_registers *reg);

static struct pktdrvr Pktdrvr[PK_MAX];
static int Derr;

void sim_tx(int dev,void *arg1,void *unused);	/*****/
void pk_rx(int dev,void *p1,void *p2);


/*
 * Send routine for packet driver
 */

int
pk_send(
struct mbuf **bpp,	/* Buffer to send */
struct iface *iface,	/* Pointer to interface control block */
int32 gateway,		/* Ignored  */
uint8 tos
){
	if(iface == NULL){
		free_p(bpp);
		return -1;
	}
	return (*iface->raw)(iface,bpp);
}

/* Send raw packet (caller provides header) */
static int
pk_raw(
struct iface *iface,	/* Pointer to interface control block */
struct mbuf **bpp	/* Data field */
){
	_go32_dpmi_registers reg;
	struct pktdrvr *pp;
	uint size;
	int offset;

	iface->rawsndcnt++;
	iface->lastsent = secclock();

	dump(iface,IF_TRACE_OUT,*bpp);
	pp = &Pktdrvr[iface->dev];
	size = len_p(*bpp);

	/* Perform class-specific processing, if any */
	switch(pp->class){
	case CL_ETHERNET:
		if(size < RUNT){
			/* Pad the packet out to the minimum */
#ifdef	SECURE
			/* Do it securely with zeros */
			struct mbuf *bp1;

			bp1 = ambufw(RUNT-size);
			bp1->cnt = RUNT-size;
			memset(bp->data,0,bp1->cnt);
			append(bpp,&bp1);
#endif
			size = RUNT;
		}
		break;
	case CL_KISS:
		/* This *really* shouldn't be done here, but it was the
		 * easiest way. Put the type field for KISS TNC on front.
		 */
		pushdown(bpp,NULL,1);
		(*bpp)->data[0] = PARAM_DATA;
		size++;
		break;
	}
	/* Copy packet to contiguous real mode buffer */
	offset = 0;
	while(*bpp != NULL){
		dosmemput((*bpp)->data,(*bpp)->cnt,_go32_info_block.linear_address_of_transfer_buffer+offset);
		offset += (*bpp)->cnt;
		free_mbuf(bpp);
	}
	/* Call the packet driver to send it */
	memset(&reg,0,sizeof(reg));
	reg.h.ah = SEND_PKT;
	reg.x.si = _go32_info_block.linear_address_of_transfer_buffer & 15;
	reg.x.ds = _go32_info_block.linear_address_of_transfer_buffer >> 4;
	reg.x.cx = size;
	_go32_dpmi_simulate_int(pp->intno,&reg);
	if(reg.x.flags & C_FLAG){
		Derr = reg.h.dh;
		return -1;
	} else
		return 0;
	return 0;
}


/* Packet driver receive upcall routine. Called by the packet driver TSR
 * twice for each incoming packet: first with ax == 0 to allocate a buffer,
 * and then with ax == 1 to signal completion of the copy.
 */
static void
pkint(_go32_dpmi_registers *reg)
{
	int i;
	uint len,blen;
	int i_state;
	struct mbuf *buffer;
	struct pktdrvr *pp;

	i_state = disable();
	/* This is not really legal, since handles are not guaranteed to
	 * be globally unique. But it's extremely expedient.
	 */
	for(i=0;i<PK_MAX;i++){
		if(Pktdrvr[i].handle == reg->x.bx)
			break;
	}
	if(i == PK_MAX){
		restore(i_state);
		reg->x.es = reg->x.di = 0;
		return;	/* Unknown handle */
	}
	pp = &Pktdrvr[i];
	len = reg->x.cx;

	switch(reg->x.ax){
	case 0:	/* Space allocate call */
		if(len + sizeof(len) > pp->dossize - pp->cnt){
			/* Buffer overflow */
			reg->x.es = 0;
			reg->x.di = 0;
			pp->overflows++;
			break;
		}
		if(pp->wptr + len + sizeof(len) > pp->dossize){
			/* Not enough room at end of DOS buffer for length
			 * plus data, so write zero length and wrap around
			 */
			uint zero = 0;
			pp->cnt += pp->dossize - pp->wptr;
			dosmemput(&zero,sizeof(zero),pp->dosbase+pp->wptr);
			pp->wptr = 0;
		}
		/* Write length into DOS buffer */
		dosmemput(&len,sizeof(len),pp->dosbase+pp->wptr);
		pp->wptr += sizeof(len);
		pp->cnt += sizeof(len);
		/* Pass new pointer to packet driver */
		reg->x.es = (pp->dosbase+pp->wptr) / 16;
		reg->x.di = (pp->dosbase+pp->wptr) % 16;
		break;
	case 1:	/* Packet complete call */
		/* blen is len rounded up to next boundary, to keep the
		 * next packet on a clean boundary
		 */
		blen = (len + sizeof(len) - 1) & ~(sizeof(len)-1);
		pp->wptr += blen;
		pp->cnt += blen;
		if(pp->wptr + sizeof(len) > pp->dossize){
			/* No room left for another len field, wrap */
			pp->cnt += pp->dossize - pp->wptr;
			pp->wptr = 0;
		}
		ksignal(&pp->cnt,1);
	default:
		break;
	}
	restore(i_state);
}

/* Shut down the packet interface */
static int
pk_stop(
struct iface *iface
){
	struct pktdrvr *pp;
	_go32_dpmi_seginfo dosmem;

	pp = &Pktdrvr[iface->dev];
	/* Call driver's release_type() entry */
	if(release_type(pp->intno,pp->handle) == -1)
		kprintf("%s: release_type error code %u\n",iface->name,Derr);

	pp->iface = NULL;
	dosmem.size = pp->dossize/16;
	dosmem.rm_segment = pp->dosbase / 16;
	_go32_dpmi_free_dos_memory(&dosmem);
	_go32_dpmi_free_real_mode_callback(&pp->rmcb_seginfo);
	return 0;
}
/* Attach a packet driver to the system
 * argv[0]: hardware type, must be "packet"
 * argv[1]: software interrupt vector, e.g., x7e
 * argv[2]: interface label, e.g., "trw0"
 * argv[3]: receive buffer size in kb
 * argv[4]: maximum transmission unit, bytes, e.g., "1500"
 * argv[5]: IP address (optional)
 */
int
pk_attach(
int argc,
char *argv[],
void *p
){
	struct iface *if_pk;
	int class,type;
	char sig[9];
	unsigned int intno;
	long handle;
	int i;
#ifdef	ARCNET
	static uint8 arcip[] = {ARC_IP};
	static uint8 arcarp[] = {ARC_ARP};
#endif
	struct pktdrvr *pp;
	char tmp[25];
	char *cp;
	unsigned long pkt_addr;
	unsigned short vec[2];
	_go32_dpmi_seginfo dosmem;

	for(i=0;i<PK_MAX;i++){
		if(Pktdrvr[i].iface == NULL)
			break;
	}
	if(i >= PK_MAX){
		kprintf("Too many packet drivers\n");
		return -1;
	}
	if(if_lookup(argv[2]) != NULL){
		kprintf("Interface %s already exists\n",argv[2]);
		return -1;
	}
	intno = htoi(argv[1]);
	/* Verify that there's really a packet driver there, so we don't
	 * go off into the ozone (if there's any left)
	 */
	dosmemget(intno*4,4,vec);
	pkt_addr = vec[1] * 16 + vec[0];
	if(pkt_addr == 0){
		kprintf("No packet driver loaded at int 0x%x\n",intno);
		return -1;
	}
	dosmemget(pkt_addr+3,9,sig);
	if(strcmp(sig,"PKT DRVR") != 0){
		kprintf("No packet driver loaded at int 0x%x\n",intno);
		return -1;
	}
	/* Find out what we've got */
 	if(driver_info(intno,-1,NULL,&class,&type,NULL,NULL) < 0){
		kprintf("driver_info call failed\n");
		return -1;
	}
	pp = &Pktdrvr[i];
	dosmem.size = 64*atoi(argv[3]); /* KB -> paragraphs */
	if(_go32_dpmi_allocate_dos_memory(&dosmem)){
		kprintf("DOS memory allocate failed, max size = %d\n",dosmem.size*16);
		return -1;
	}
	pp->dossize = dosmem.size * 16;
	pp->dosbase = dosmem.rm_segment * 16;
	pp->overflows = pp->wptr = pp->rptr = pp->cnt = 0;

	if_pk = (struct iface *)callocw(1,sizeof(struct iface));
	if_pk->name = strdup(argv[2]);
	if(argc > 5)
		if_pk->addr = resolve(argv[5]);
	else
		if_pk->addr = Ip_addr;

	if_pk->mtu = atoi(argv[4]);
	if_pk->dev = i;
	if_pk->raw = pk_raw;
	if_pk->stop = pk_stop;
	pp->intno = intno;
	pp->iface = if_pk;

	pp->rmcb_seginfo.pm_offset = (int)pkint;
	if((i = _go32_dpmi_allocate_real_mode_callback_retf(&pp->rmcb_seginfo,
	 &pp->rmcb_registers)) != 0){
		kprintf("real mode callback alloc failed: %d\n",i);
		return -1;
	}
	pp->handle = access_type(intno,class,ANYTYPE,0,pp->rmcb_seginfo.rm_segment,
	  pp->rmcb_seginfo.rm_offset);

	switch(class){
	case CL_ETHERNET:
		setencap(if_pk,"Ethernet");

		/**** temp set multicast flag ****/
/*		i = set_rcv_mode(intno,pp->handle,5);
		printf("set_rcv_mode returns %d, Derr = %d\n",i,Derr); */

		/* Get hardware Ethernet address from driver */
		if_pk->hwaddr = mallocw(EADDR_LEN);
		get_address(intno,pp->handle,if_pk->hwaddr,EADDR_LEN);
		if(if_pk->hwaddr[0] & 1){
			kprintf("Warning! Interface '%s' has a multicast address:",
			 if_pk->name);
			kprintf(" (%s)\n",
			 (*if_pk->iftype->format)(tmp,if_pk->hwaddr));
		}
		break;
#ifdef	ARCNET
	case CL_ARCNET:
		if_pk->output = anet_output;
		/* Get hardware ARCnet address from driver */
		if_pk->hwaddr = mallocw(AADDR_LEN);
		get_address(intno,pp->handle,if_pk->hwaddr,AADDR_LEN);
		break;
#endif
	case CL_SERIAL_LINE:
		setencap(if_pk,"SLIP");
		break;
#ifdef	AX25
	case CL_KISS:	/* Note that the raw routine puts on the command */
	case CL_AX25:
		setencap(if_pk,"AX25");
		if_pk->hwaddr = mallocw(AXALEN);
		memcpy(if_pk->hwaddr,Mycall,AXALEN);
		break;
#endif
	case CL_SLFP:
		setencap(if_pk,"SLFP");
		get_address(intno,pp->handle,(uint8 *)&if_pk->addr,4);
		break;
	default:
		kprintf("Packet driver has unsupported class %u\n",class);
		free(if_pk->name);
		free(if_pk);
		return -1;
	}
	pp->class = class;
	if_pk->next = Ifaces;
	Ifaces = if_pk;
	cp = if_name(if_pk," tx");
	if(strchr(argv[3],'s') == NULL)
		if_pk->txproc = newproc(cp,768,if_tx,if_pk->dev,if_pk,NULL,0);
	else
		if_pk->txproc = newproc(cp,768,sim_tx,if_pk->dev,if_pk,NULL,0);
	free(cp);
	cp = if_name(if_pk," rx");
	if_pk->rxproc = newproc(cp,768,pk_rx,if_pk->dev,if_pk,pp,0);
	free(cp);
	return 0;
}
static long
access_type(
int intno,
int if_class,
int if_type,
int if_number,
uint rm_segment,
uint rm_offset
){
	_go32_dpmi_registers reg;
	int i;

	memset(&reg,0,sizeof(reg));
	reg.h.ah = ACCESS_TYPE;	/* Access_type() function */
	reg.h.al = if_class;	/* Class */
	reg.x.bx = if_type;	/* Type */
	reg.h.dl = if_number;	/* Number */
	reg.x.es = rm_segment;	/* Address of rm receive handler */
	reg.x.di = rm_offset;
	_go32_dpmi_simulate_int(intno,&reg);
	if(reg.x.flags & C_FLAG){
		Derr = reg.h.dh;
		return -1;
	} else
		return reg.x.ax;
}
static int
release_type(
int intno,
int handle
){
	_go32_dpmi_registers reg;

	memset(&reg,0,sizeof(reg));
	reg.x.bx = handle;
	reg.h.ah = RELEASE_TYPE;
	_go32_dpmi_simulate_int(intno,&reg);
	if(reg.x.flags & C_FLAG){
		Derr = reg.h.dh;
		return -1;
	} else
		return 0;
}
static int
driver_info(
int intno,
int handle,
int *version,
int *class,
int *type,
int *number,
int *basic
){
	_go32_dpmi_registers reg;

	memset(&reg,0,sizeof(reg));
	reg.h.ah = DRIVER_INFO;
	reg.x.bx = handle;
	reg.h.al = 0xff;
	_go32_dpmi_simulate_int(intno,&reg);
	if(reg.x.flags & C_FLAG){
		Derr = reg.h.dh;
		return -1;
	}
	if(version != NULL)
		*version = reg.x.bx;
	if(class != NULL)
		*class = reg.h.ch;
	if(type != NULL)
		*type = reg.x.dx;
	if(number != NULL)
		*number = reg.h.cl;
	if(basic != NULL)
		*basic = reg.h.al;
	return 0;
}
static int
get_address(
int intno,
int handle,
uint8 *buf,
int len
){
	_go32_dpmi_registers reg;

	memset(&reg,0,sizeof(reg));
	reg.h.ah = GET_ADDRESS;
	reg.x.bx = handle;
	reg.x.di = _go32_info_block.linear_address_of_transfer_buffer & 15;
	reg.x.es = _go32_info_block.linear_address_of_transfer_buffer >> 4;
	reg.x.cx = len;
	_go32_dpmi_simulate_int(intno,&reg);
	if(reg.x.flags & C_FLAG){
		Derr = reg.h.dh;
		return -1;
	}
	dosmemget(_go32_info_block.linear_address_of_transfer_buffer,len,buf);
	return 0;
}
static int
set_rcv_mode(
int intno,
int handle,
int mode
){
	_go32_dpmi_registers reg;

	memset(&reg,0,sizeof(reg));
	reg.h.ah = SET_RCV_MODE;
	reg.x.cx = mode;
	reg.x.bx = handle;
	_go32_dpmi_simulate_int(intno,&reg);
	if(reg.x.flags & C_FLAG){
		Derr = reg.h.dh;
		return -1;
	}
	return 0;
}

void
pk_rx(int dev,void *p1,void *p2)
{
	struct iface *iface = (struct iface *)p1;
	struct pktdrvr *pp = (struct pktdrvr *)p2;
	uint len,blen;	/* len type must match size field in pkint */
	struct mbuf *bp;
	int i,cadj;

loop:	while(disable(),i=(volatile)pp->cnt,enable(),i == 0)
		kwait(&pp->cnt);

	cadj = 0;
	/* Extract size */
	dosmemget(pp->dosbase+pp->rptr,sizeof(len),&len);
	if(len == 0){
		/* Writer wrapped around */
		cadj += pp->dossize - pp->rptr;
		pp->rptr = 0;
		dosmemget(pp->dosbase,sizeof(len),&len);
	}
	/* Copy the packet into an mbuf and queue for the router */
	bp = ambufw(len+sizeof(struct iface *));
	bp->data += sizeof(struct iface *);
#ifdef	debug
	kprintf("overf %d cnt %d start %d len %d\n",pp->overflows,
		pp->cnt,pp->rptr+sizeof(len),len);
#endif
	dosmemget(pp->dosbase+pp->rptr+sizeof(len),len,bp->data);
	bp->cnt = len;
	net_route(iface,&bp);

	/* figure length rounded up to next boundary */
	blen = sizeof(len) + (len + sizeof(len) - 1) & ~(sizeof(len)-1);

	cadj += blen;
	pp->rptr += blen;
	if(pp->rptr + sizeof(len) > pp->dossize){
		cadj += pp->dossize - pp->rptr;
		pp->rptr = 0;
	}
	disable();
	pp->cnt -= cadj;
	enable();

	goto loop;
}
