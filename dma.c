/* PC DMA support functions for DJGPP. Copyright 1996 Phil Karn */
#include "top.h"

#include "stdio.h"
#include <dos.h>
#include <dpmi.h>
#include "global.h"
#include "dma.h"
#include "nospc.h"

/* I/O port addresses for DMA page registers on the PC/AT */
static uint Page_regs[] = {
	0x87,0x83,0x81,0x82,0x8f,0x8b,0x89,0x8a
};

/* Allocate a block of DOS real memory suitable for DMA
 * Returns pointer to the physical buffer address
 */
int32
dma_malloc(
int *selector,		/* Return selector through here */
unsigned len,
int align	/* 0-> within 64K, 1-> within 128K */
){
	int i,j,good,segment,selectors[20];
	__dpmi_meminfo meminfo;

	if(len > 131072 ||  (align == 0 && len > 65536))
		return NULL;	/* Too large for requested alignment */
	/* Try up to 20 times to allocate DOS memory of the requested
	 * size that doesn't cross a 64K/128K boundary
	 */
	good = 0;
	for(i=0;i<20;i++){
		segment = __dpmi_allocate_dos_memory((len+15)/16,&selectors[i]);
		if(segment == -1)
			break;	/* Allocate failed, give up */

		/* Check to see if buffer crosses 64K boundary */
		if(align == 0 &&
		 (16*segment & ~0xffff) == 
		 ((16*segment + len - 1) & ~0xffff)){
			good = 1;
			break;
		}
		if((16*segment & ~0x1ffff) ==
		 ((16*segment + len - 1) & ~0x1ffff)){
			good = 1;
			break;
		}
	}		
	/* Free all the memory we can't use */
	for(j=0;j<i;j++)
		__dpmi_free_dos_memory(selectors[j]);
	if(!good)
		return 0;	/* Failed */
	/* Success! Map it into virtual space */
	*selector = selectors[i];
	return  16 * segment;
}

/* Disable QEMM DMA translation */
int
dis_dmaxl(chan)
int chan;	/* DMA channel number */
{
	union REGS regs;
	struct SREGS segregs;

	return 0;	/*****/
	regs.x.ax = 0x810b;
	regs.x.bx = chan;
	regs.x.dx = 0;
	int86x(0x4b,&regs,&regs,&segregs);
	if(regs.x.cflag)
		return -1;

	return 0;
}

/* Re-enable QEMM DMA translation */
int
ena_dmaxl(chan)
int chan;
{
	union REGS regs;
	struct SREGS segregs;

	return 0;	/*****/
	regs.x.ax = 0x810c;
	regs.x.bx = chan;
	regs.x.dx = 0;
	int86x(0x4b,&regs,&regs,&segregs);
	if(regs.x.cflag)
		return -1;

	return 0;
}

/* Set up a 8237 DMA controller channel to point to a specified buffer */
int
setup_dma(chan,physaddr,length,mode)
int chan;
int32 physaddr;
uint length;
int mode;	/* Read/kwrite, etc */
{
	int dmaport;
	int i_state;

	if(length == 0 || chan < 0 || chan > 7 || chan == 4)
		return -1;

	i_state = disable();
	dma_disable(chan);
	outportb(Page_regs[chan],physaddr >> 16); /* Store in 64K DMA page */
	if(chan < 4){
		/* 8-bit DMA */
		length--;
		outportb(DMA1BASE+DMA_MODE,mode|chan);	/* Select mode */
		outportb(DMA1BASE+DMA_RESETFF,0);	 /* reset byte pointer flipflop */

		/* Output buffer start (dest) address */
		dmaport = DMA1BASE + 2*chan;
		outportb(dmaport,(uint8)physaddr);
		outportb(dmaport,(uint8)(physaddr >> 8));

		/* output DMA maximum byte count */
		dmaport++;
		outportb(dmaport,(uint8)length);
		outportb(dmaport,(uint8)(length >> 8));
	} else {
		/* 16-bit DMA */
		length >>= 1;	/* count is 16-bit words */
		length--;
		physaddr >>= 1;

		outportb(DMA2BASE+2*DMA_MODE,mode|(chan & 3));/* Select mode */
		outportb(DMA2BASE+2*DMA_RESETFF,0);	 /* reset byte pointer flipflop */

		/* Output buffer start (dest) address */
		dmaport = DMA2BASE + 4*(chan & 3);
		outportb(dmaport,(uint8)physaddr);
		outportb(dmaport,(uint8)(physaddr >> 8));

		/* output DMA maximum byte count */
		dmaport += 2;
		outportb(dmaport,(uint8)length);
		outportb(dmaport,(uint8)(length >> 8));
	}
	/* Unmask channel (start DMA) */
	dma_enable(chan);
	restore(i_state);
	return 0;
}

/* Return current count on specified DMA channel */
uint
dma_cnt(chan)
int chan;
{
	int dmaport;
	uint bytecount;

	if(chan < 4){
		outportb(DMA1BASE+DMA_RESETFF,0); /* reset firstlast ff */
		dmaport = DMA1BASE + 2*chan + 1;
	} else {
		outportb(DMA2BASE+2*DMA_RESETFF,0);
		dmaport = DMA2BASE + 4*(chan&3) + 2;
	}
	bytecount = inportb(dmaport);
	bytecount |= inportb(dmaport) << 8;
	return bytecount;
}

/* Disable DMA on specified channel, return previous status */
int
dma_disable(chan)
int chan;
{
	if(chan < 4){
		outportb(DMA1BASE+DMA_MASK, DMA_DISABLE|chan);
	} else {
		outportb(DMA2BASE+2*DMA_MASK, DMA_DISABLE|(chan & 3));
	}
	return 0;
}
/* Enable DMA on specified channel */
int
dma_enable(chan)
int chan;
{
	if(chan < 4){
		outportb(DMA1BASE+DMA_MASK, DMA_ENABLE|chan);
	} else {
		outportb(DMA2BASE+2*DMA_MASK,DMA_ENABLE|(chan & 3));
	}
	return 0;
}
