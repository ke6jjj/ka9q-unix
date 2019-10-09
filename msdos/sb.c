#define	TXBUF	1400
#define	RXBUF	8192
#define	SAMPRATE 8000U

#include "top.h"

#include "global.h"
#include "stdio.h"
#include <dos.h>
#include "mbuf.h"
#include "dma.h"
#include "sb.h"
#include "nospc.h"
#include "socket.h"
#include "cmdparse.h"
#include "netuser.h"

struct gain {
	char *name;
	int regl;
	int regr;
	double scale;
	double offset;
};
struct sbdriver {
	int irq,dma8,dma16;

	enum { IDLE, DAC, ADC } state;

	int32 dmabuf;	/* Physical base of DMA buffer */
	int selector;	/* Protected mode selector of DMA buffer */
	int dmasize;	/* Size of DMA buffer */

	int32 ioptr;	/* Physical I/O pointer into DMA buffer */
	volatile int bufcnt;	/* Count of bytes in DMA buffer */
	int base;	/* Base register address */
	volatile int pause;	/* Paused due to data underflow */
	long samprate;
	int stereo;
	long stuffsamples;
	long interrupts;
} Sb;

static int sb_read_data(int base);
static int sb_reset(int base);
static int sb_setup(int base,int irq,int dma8,int dma16);
static int sb_write_byte(int base,int data);
static int sb_write_word_le(int base,int data);
static int sb_write_word_be(int base,int data);
static int sb_read_data(int base);
static int sb_read_mix(int base,int reg);
static int sb_write_mix(int base,int reg,int data);
static void dumpgains(int);
static void dumpgain(int,struct gain *);
unsigned short pull16le(struct mbuf **bpp);
void sbhandle(int dev);

int S = -1;

int dosbatt(),dosbdet(),dogain(),dossend(),dosamprate(),dostereo();
int dosbcal(),dosbidle(),dostcplisten(),dostatus();

struct gain Gaintab[] = {
	"master",	SB_MV,		SB_MV+1,	4, -62,
	"dac",		SB_DAC,		SB_DAC+1,	4, -62,
	"midi",		SB_MIDI, 	SB_MIDI+1,	4, -62,
	"cd",		SB_CD,		SB_CD+1,	4, -62,
	"line",		SB_LINE,	SB_LINE+1,	4, -62,
	"mic",		SB_MIKE,	SB_MIKE,	4, -62,
	"pcspkr",	SB_PCSPKR,	SB_PCSPKR,	64/6., -18,
	"adc",		SB_ADC,		SB_ADC+1,	64/6., 0,
	"output",	SB_OUTG,	SB_OUTG+1,	64/6., 0,
	"treble",	SB_TREB,	SB_TREB+1,	8, -16,
	"bass",		SB_BASS,	SB_BASS+1,	8, -16,
	NULL,
};
static struct cmds Scmds[] = {
	"attach",	dosbatt,	0, 6,
		"sound attach sb16 <base_addr> <irq> <8bitdma> <16bitdma>",
	"calibrate",	dosbcal,	0, 1,
		"sound calibrate <samprate>",
	"detach",	dosbdet,	0, 1,
		"sound detach",
	"gain",		dogain,		0, 1,
		"sound gain [<parameter> [<setting>]]",
	"idle",		dosbidle,	0, 0,
		"sound idle",
	"listen",	dostcplisten,	0, 0, NULL,
	"samprate",	dosamprate,	0, 0, NULL,
	"send",		dossend,	4096, 2, "sound send <host> [<port>]",
	"status",	dostatus,	0, 0, NULL,
	"stereo",	dostereo,	0, 0, NULL,
	NULL,
};

dosound(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Scmds,argc,argv,p);
}

dostatus(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int i;

	kprintf("Samp rate: %lu Hz",Sb.samprate);
	if(Sb.stereo)
		kprintf(" Stereo;");
	else
		kprintf(" Mono;");
	kprintf(" Interrupts: %lu",Sb.interrupts);
	switch(Sb.state){
	case IDLE:
		kprintf(" Idle\n");
		break;
	case ADC:
		kprintf(" A/D\n");
		break;
	case DAC:
		kprintf(" D/A: %d bytes; ",Sb.bufcnt);
		if(Sb.pause)
			kprintf(" Paused");
		kprintf("\n");
		break;
	}
	return 0;
}


dosbidle(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	/* This will indirectly cause any transfer to stop */
	sb_idle();
	return 0;
}

dosbatt(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int major,minor;

	if(strcmp(argv[1],"sb16") == 0){
		if(Sb.base != 0){
			kprintf("Only one sound card currently supported\n");
			return 1;
		}
		Sb.base = htoi(argv[2]);
		Sb.irq = atoi(argv[3]);
		Sb.dma8 = atoi(argv[4]);
		Sb.dma16 = atoi(argv[5]);
		if(sb_setup(Sb.base,Sb.irq,Sb.dma8,Sb.dma16) == -1){
			kprintf("setup failed\n");
			return 0;
		}
		sb_write_byte(Sb.base,0xe1);
		major = sb_read_data(Sb.base);
		minor = sb_read_data(Sb.base);
		kprintf("SoundBlaster DSP version %d.%d\n",major,minor);
		Sb.state = IDLE;
	} else {
		kprintf("Only sb16 currently supported\n");
		return -1;
	}
	return 0;
}
dosbdet(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	sbshut();
	return 0;
}
void
sbshut(void)
{
	if(Sb.base == 0)
		return;	/* No card attached */
	maskoff(Sb.irq);
	freevect(Sb.irq);
	sb_reset(Sb.base);
	Sb.base = 0;
	__dpmi_free_dos_memory(Sb.selector);
	Sb.dmabuf = 0;
	kshutdown(S,2);	/* Blow it away */
	S = -1;
}
dosamprate(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setlong(&Sb.samprate,"Sample rate:",argc,argv);
}
dostereo(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Sb.stereo,"Stereo",argc,argv);
}
dogain(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct gain *gp;
	double x;

	if(argc == 1){
		/* Dump all settings */
		dumpgains(Sb.base);
		return 0;
	}
	for(gp = Gaintab;gp->name != NULL;gp++){
		if(strncmp(argv[1],gp->name,strlen(argv[1])) == 0)
			break;
	}
	if(gp->name == NULL){
		kprintf("Valid parameters:");
		for(gp = Gaintab;gp->name != NULL;gp++)
			kprintf(" %s",gp->name);
		kprintf("\n");
		return 1;
	}
	if(argc == 2){
		dumpgain(Sb.base,gp);
		return 1;
	}
	x = (atof(argv[2]) - gp->offset) * gp->scale;
	x = max(0,x);
	x = min(x,255);
	sb_write_mix(Sb.base,gp->regl,(int) x);
	if(argc > 3){
		x = (atof(argv[3]) - gp->offset) * gp->scale;
		x = max(0,x);
		x = min(x,255);
	}
	sb_write_mix(Sb.base,gp->regr,(int) x);
	return 0;
}
void
dumpgains(base)
int base;
{
	struct gain *gp;

	for(gp = Gaintab;gp->name != NULL;gp++)
		dumpgain(base,gp);
}
void
dumpgain(base,gp)
int base;
struct gain *gp;
{
	double l,r;

	l = sb_read_mix(base,gp->regl);
	r = sb_read_mix(base,gp->regr);
	kprintf("%s ",gp->name);
	if(l != r){
		kprintf("left %.0f right %.0f dB\n",
		 l / gp->scale + gp->offset,
		 r / gp->scale + gp->offset);
	} else {
		kprintf("%.0f dB\n",l / gp->scale + gp->offset);
	}
}
void
sndrcv(s,p1,p2)
int s;
void *p1,*p2;
{
	struct mbuf *bp;

	while(recv_mbuf(s,&bp,0,NULL,NULL) > 0)
		sb_send(&bp);
	kclose(s);
}

dostcplisten(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(sb_dac(Sb.samprate,Sb.stereo,RXBUF) == -1)
		return 1;

	return start_tcp(2010,"sound",sndrcv,2048);
}
dossend(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ksockaddr_in sock;
	int s;
	int32 seq = 0;
	struct mbuf *bp;

	s = ksocket(kAF_INET,kSOCK_DGRAM,0);
	sock.sin_family = kAF_INET;
	sock.sin_port = (argc > 2) ? atoi(argv[2]) : 2010;
	sock.sin_addr.s_addr = resolve(argv[1]);
	if(kconnect(s,(struct ksockaddr *)&sock,sizeof(sock)) == -1){
		kprintf("Connect failed\n");
		kclose(s);
		return -1;
	}
	if(sb_adc(Sb.samprate,Sb.stereo,TXBUF) == -1){
		kclose(s);
		return 1;
	}
	for(;;){
		bp = sb_recv();
		pushdown(&bp,NULL,4);
		put32(bp->data,seq);
		seq += len_p(bp) - 4;
		send_mbuf(s,&bp,0,NULL,NULL);
	}
	return 0;
}

int
sb_setup(base,irq,dma8,dma16)
int base;	
int irq;
int dma8;
int dma16;
{
	int dma;

	if(sb_reset(base) == -1){
		kprintf("Soundblaster reset failed\n");
		return -1;
	}
	/* Set new interrupt vector */
	if(setvect(irq,0,sbhandle,0) == -1){
		kprintf("IRQ %u out of range\n",irq);
		return -1;
	}
	switch(irq){
	case 2:
	case 9:
		sb_write_mix(base,0x80,1);
		break;
	case 5:
		sb_write_mix(base,0x80,2);
		break;
	case 7:
		sb_write_mix(base,0x80,4);
		break;
	case 10:
		sb_write_mix(base,0x80,8);
		break;
	default:
		kprintf("invalid irq %d\n",irq);
		return -1;
	}
	maskon(irq);
	if(dma8 < 0 || dma8 > 3 || dma8 == 2){
		kprintf("dma8 invalid\n");
		return -1;
	}
	if(dma16 < 5 || dma16 > 7){
		kprintf("dma16 invalid\n");
		return -1;
	}
	dis_dmaxl(dma16);
	dis_dmaxl(dma8);
	dma = (1 << dma16) | (1<<dma8);
	sb_write_mix(base,0x81,dma);

	return 0;
}
/* Enter A/D mode */
int
sb_adc(rate,stereo,bufsiz)
long rate;	/* Sampling rate, Hz */
int stereo;	/* 1 = stereo, 0 = mono */
int bufsiz;	/* Size of mbufs to be generated (1/2 DMA buffer) */
{
	if(Sb.base == 0){
		kprintf("No SB16 card attached\n");
		return -1;
	}
	if(Sb.state != IDLE){
		kprintf("SB16 not idle\n");
		return -1;
	}
	/* Allocate DMA buffer */
	if((Sb.ioptr = Sb.dmabuf = dma_malloc(&Sb.selector,2*bufsiz,1)) == NULL){
		kprintf("Can't alloc dma buffer\n");
		return -1;
	}
	Sb.state = ADC;
	Sb.dmasize = 2*bufsiz;
	setup_dma(Sb.dma16,Sb.dmabuf,Sb.dmasize,0x54);

	/* Set sampling rate */
	sb_write_byte(Sb.base,0x42);
	sb_write_word_be(Sb.base,rate);

	sb_write_byte(Sb.base,0xbe);	/* 16-bit input, auto-init, fifo on */
	if(stereo)
		sb_write_byte(Sb.base,0x30);	/* stereo, signed */
	else
		sb_write_byte(Sb.base,0x10);	/* mono, signed */
	/* Write number of 16-bit words in half buffer, minus 1 */
	sb_write_word_le(Sb.base,Sb.dmasize/4-1);
	return 0;
}
/* Enter DAC mode */
int
sb_dac(rate,stereo,bufsiz)
long rate;	/* Sampling rate, Hz */
int stereo;	/* 1 = stereo, 0 = mono */
int bufsiz;	/* Size of buffer unit (1/2 DMA buffer) */
{
	if(Sb.base == 0){
		kprintf("No SB16 card attached\n");
		return -1;
	}
	if(Sb.state != IDLE){
		kprintf("SB16 not idle\n");
		return -1;
	}
	/* Allocate DMA buffer */
	if((Sb.ioptr = Sb.dmabuf = dma_malloc(&Sb.selector,2*bufsiz,1)) == 0){
		kprintf("Can't alloc dma buffer\n");
		return -1;
	}
	Sb.state = DAC;
	Sb.dmasize = 2*bufsiz;
	Sb.stuffsamples = 0;
	Sb.bufcnt = 0;
	{
		char *buf;

		buf = malloc(Sb.dmasize);
		memset(buf,0,Sb.dmasize);	
		dosmemput(buf,Sb.dmasize,Sb.dmabuf);
		free(buf);
	}
	sb_write_byte(Sb.base,0xd1);	/* spkr on */
	setup_dma(Sb.dma16,Sb.dmabuf,Sb.dmasize,0x58);

	/* Set up sampling rate */
	sb_write_byte(Sb.base,0x41);
	sb_write_word_be(Sb.base,rate);

	sb_write_byte(Sb.base,0xb6);	/* 16-bit output, auto-init, fifo on */
	if(stereo)
		sb_write_byte(Sb.base,0x30);	/* stereo, signed */
	else
		sb_write_byte(Sb.base,0x10);	/* mono, signed */
	/* Number of 16-bit words in a half-buffer, minus 1 */
	sb_write_word_le(Sb.base,Sb.dmasize/4-1);
	Sb.pause = 1;
	sb_write_byte(Sb.base,0xd5);	/* Pause until we get data */
	return 0;
}
/* Return soundblaster to idle condition */
int
sb_idle(void)
{
	int cnt;

	switch(Sb.state){
	case IDLE:
		return 0;	/* Already idle */		
	case DAC:
		/* Wait for output queue to drain to below one buffer */
		disable();
		while(Sb.bufcnt >= Sb.dmasize/2)
			kwait(&Sb);

		/* If an incomplete buffer remains, pad it out */ 
		cnt = Sb.bufcnt;
		enable();
		if(cnt > 0){
			struct mbuf *bp;

			bp = ambufw(Sb.dmasize/2 - cnt);
			bp->cnt = Sb.dmasize/2 - cnt;
			memset(bp->data,0,bp->cnt);
			sb_send(&bp);
		}	
		disable();
		while(!Sb.pause)
			kwait(&Sb);
		enable();
	case ADC:	/* note fall-thru */
		/* stop conversion */
		sb_write_byte(Sb.base,0xd9);
		break;
	}
	Sb.bufcnt = 0;
	Sb.state = IDLE;
	kwait(NULL);
	/* Ought to wait for last interrupt here */
	dma_disable(Sb.dma16);
	__dpmi_free_dos_memory(Sb.selector);
	Sb.dmabuf = 0;

	return 0;
}
/* Send buffer to the DAC, starting it if it was paused, and blocking as
 * needed
 */
int
sb_send(struct mbuf **bpp)
{
	int cnt;

	if(bpp == NULL || Sb.state != DAC)
		return -1;

	while(*bpp != NULL){
		/* Wait for space to open up in DMA buffer */
		disable();
		while(Sb.bufcnt >= Sb.dmasize)
			kwait(&Sb);
		cnt = Sb.dmasize-Sb.bufcnt;
		enable();

		/* In this round, we transfer the lesser of:
		 * -the available space in the DMA buffer
		 * -the data remaining in this packet
		 * -the free space in the high end of the DMA buffer
		 */
		cnt = min(cnt,(*bpp)->cnt);
		cnt = min(cnt,Sb.dmabuf+Sb.dmasize-Sb.ioptr);
		if(cnt == 0)
			break;	/* Nothing more to do */

		dosmemput((*bpp)->data,cnt,Sb.ioptr);

		disable();
		Sb.bufcnt += cnt;	/* Add to bytes in buffer */
		/* Resume output if there's now at least one full buffer */
		if(Sb.pause && Sb.bufcnt >= Sb.dmasize/2){
			Sb.pause = 0;
			sb_write_byte(Sb.base,0xd6);
		}
		enable();

		(*bpp)->data += cnt;
		(*bpp)->cnt -= cnt;
		if((*bpp)->cnt == 0)
			free_mbuf(bpp);

		/* Increment pointer, wrap around if necessary */
		Sb.ioptr += cnt;
		if(Sb.ioptr >= Sb.dmabuf + Sb.dmasize)
			Sb.ioptr -= Sb.dmasize;
	}
	return 0;
}
/* Read a buffer from the A/D, blocking if necessary */
struct mbuf *
sb_recv(void)
{
	struct mbuf *bp;
	int i;

	if(Sb.state != ADC)
		return NULL;	/* Not in A/D mode */

	while((i = Sb.bufcnt) == 0)
		kwait(&Sb);

	bp = alloc_mbuf(i);
	dosmemget(Sb.ioptr,i,bp->data);
	bp->cnt = i;
	Sb.ioptr += i;
	if(Sb.ioptr >= Sb.dmabuf + Sb.dmasize)
		Sb.ioptr -= Sb.dmasize;
	disable();
	Sb.bufcnt -= i;
	enable();
	return bp;
}

/* Reset Soundblaster card */
static int
sb_reset(int base)
{
	unsigned int i;

	outportb(base+SB_RESET,1);
	for(i=100;i !=0;i--)
		;
	outportb(base+SB_RESET,0);
	for(i=65535;i != 0;i--){
		if(sb_read_data(base) == 0xaa)
			break;
	}
	if(i == 0)
		return -1;
	return 0;
}
/* Wait for read data to become available, then read it. Return -1 on timeout */
static int
sb_read_data(int base)
{
	unsigned int i;

	for(i=65535;i!=0;i--){
		if(inportb(base+SB_RB_STAT) & 0x80)
			break;
	}
	if(i == 0)
		return -1;	/* Timeout */
	return inportb(base+SB_READ_DATA);
}

/* Write data byte to soundblaster when it's ready. return 0 normally, -1 on err */
static int
sb_write_byte(int base, int data)
{
	int i;

	for(i=65535;i!=0;i--){
		if((inportb(base+SB_WB_STAT) & 0x80) == 0)
			break;
	}
	if(i == 0)
		return -1;
	outportb(base+SB_WB,data);
	return 0;
}
/* Write 16-bit word in big-endian order */
static int
sb_write_word_be(int base,int data)
{
	sb_write_byte(base,data >> 8);
	sb_write_byte(base,data);
	return 0;
}
/* Write 16-bit word in little-endian order */
static int
sb_write_word_le(int base,int data)
{
	sb_write_byte(base,data);
	sb_write_byte(base,data >> 8);
	return 0;
}
/* Read the mixer */
static int
sb_read_mix(int base,int reg)
{
	outportb(base+SB_MIX_INDEX,reg);
	return inportb(base+SB_MIX_DATA);
}

/* Write data to soundblaster when it's ready. return 0 normally, -1 on err */
static int
sb_write_mix(int base,int reg,int data)
{
	outportb(base+SB_MIX_INDEX,reg);
	outportb(base+SB_MIX_DATA,data);
	return 0;
}

dosbcal(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	long rate;

	rate = atol(argv[1]);
	sb_calibrate(rate);
	return 0;
}

/* Find DC offsets of each channel */
sb_calibrate(long rate)
{
	long leftavg,rightavg;
	uint8 omixl,omixr;
	long samples;
	struct mbuf *bp;
	uint left,right;

	/* Save previous mixer state,
	 * then turn everything off
	 */
	omixl = sb_read_mix(Sb.base,SB_INMIXL);
	omixr = sb_read_mix(Sb.base,SB_INMIXR);
	sb_write_mix(Sb.base,SB_INMIXL,0);		/* All inputs off */
	sb_write_mix(Sb.base,SB_INMIXR,0);

	/* Collect and analyze some data */
	sb_adc(rate,1,TXBUF);
	leftavg = rightavg = 0;
	samples = 0;

	bp = sb_recv();
	while(bp != NULL){
		left = pull16le(&bp);
		right = pull16le(&bp);

		leftavg += left;
		rightavg += right;		
		samples++;
	}
	sb_idle();
	/* Restore previous mixer switches */
	sb_write_mix(Sb.base,SB_INMIXL,omixl);
	sb_write_mix(Sb.base,SB_INMIXR,omixr);

	leftavg /= samples;
	rightavg /= samples;
	kprintf("Left channel avg: %ld Right channel avg: %ld\n",
	 leftavg,rightavg);

	return 0;
}
unsigned short
pull16le(struct mbuf **bpp)
{
	uint x;

	x = pull16(bpp);
	return (x >> 8) | (x << 8);
}
/* Soundblaster interrupt handler */
void
sbhandle(int dev)
{
	int i;

	Sb.interrupts++;
	i = sb_read_mix(Sb.base,0x82);
	if(i & 1)	/* 8-bit transfers not used, reset anyway */
		(void)inportb(Sb.base+0xe);

	if(i & 2)
		(void)inportb(Sb.base+0xf);
	else
		return;

	switch(Sb.state){
	case IDLE:
		break;
	case ADC:
		if(Sb.bufcnt != Sb.dmasize)
			Sb.bufcnt += Sb.dmasize/2;
		ksignal(&Sb,1);
		break;
	case DAC:
		Sb.bufcnt -= Sb.dmasize/2;	/* Amount sent */
		if(Sb.bufcnt < Sb.dmasize/2){
			/* buffer not ready, pause */
			Sb.pause = 1;
			sb_write_byte(Sb.base,0xd5);
		}
		ksignal(&Sb,1);
		break;
	}
}
