/* 16 bit CRC-CCITT stuff. Extracted from Bill Simpson's PPP */

#define FCS_START	0xffff	/* Starting bit string for FCS calculation */
#define FCS_FINAL	0xf0b8	/* FCS when summed over frame and sender FCS */

#define FCS(fcs, c)		(((uint16)fcs >> 8) ^ Fcstab[((fcs) ^ (c)) & 0xff])

extern unsigned short Fcstab[];
int crc_check(unsigned char *buf,unsigned int len);
void crc_gen(unsigned char *buf,unsigned int len);

