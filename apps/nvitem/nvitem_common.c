#include "nvitem_common.h"
unsigned short calc_Checksum(unsigned char *dat, unsigned long len)
{
	unsigned short num = 0;
	unsigned long chkSum = 0;
	while (len > 1) {
		num = (unsigned short)(*dat);
		dat++;
		num |= (((unsigned short)(*dat)) << 8);
		dat++;
		chkSum += (unsigned long)num;
		len -= 2;
	}
	if (len) {
		chkSum += *dat;
	}
	chkSum = (chkSum >> 16) + (chkSum & 0xffff);
	chkSum += (chkSum >> 16);
	return (~chkSum);
}

/*
	TRUE(1): pass
	FALSE(0): fail
*/
BOOLEAN ChkNVEcc(uint8 * buf, uint32 size, uint16 checksum)
{
	uint16 crc, crcOri;

	crc = calc_Checksum(buf, size);

	return (crc == checksum);
}
