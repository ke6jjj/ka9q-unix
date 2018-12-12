/* 16 bit CRC-CCITT stuff. Extracted from Bill Simpson's PPP */

#define FCS_START	0xffff	/* Starting bit string for FCS calculation */
#define FCS_FINAL	0xf0b8	/* FCS when summed over frame and sender FCS */

#define FCS(fcs, c)		(((uint16)fcs >> 8) ^ Fcstab[((fcs) ^ (c)) & 0xff])

extern uint16 Fcstab[];
int crc_check(uint8 *buf,uint len);
void crc_gen(uint8 *buf,uint len);

void crc_init(uint16 *crc);
void crc_update(uint8 *buf, uint len, uint16 *crc);
int crc_final_check(uint16 crc);
void crc_final_write(uint8 *buf, uint16 crc);
