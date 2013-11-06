#include <fcntl.h>
#include "engopt.h"
#include <pthread.h>
#include "eng_productdata.h"

#define PRODUCTINFO_FILE  "/dev/block/platform/sprd-sdhci.3/by-name/miscdata"
#define PRODUCTINFO_FILE0  "/productinfo/productinfo.bin"

int eng_read_productnvdata(char *databuf,  int data_len)
{
	int ret = 0;
	int len;

	int fd = open(PRODUCTINFO_FILE,O_RDONLY);
	if (fd >= 0){
		ENG_LOG("%s open Ok PRODUCTINFO_FILE = %s ",__FUNCTION__ , PRODUCTINFO_FILE);
		len = read(fd, databuf, data_len);

		if (len <= 0){
			ret = 1;
			ENG_LOG("%s read fail PRODUCTINFO_FILE = %s ",__FUNCTION__ , PRODUCTINFO_FILE);
		}
		close(fd);
	} else {
		ENG_LOG("%s open fail PRODUCTINFO_FILE = %s ",__FUNCTION__ , PRODUCTINFO_FILE);
		ret = 1;
	}
	return ret;
}

int eng_write_productnvdata(char *databuf,  int data_len)
{
	int ret = 0;
	int len;

	int fd = open(PRODUCTINFO_FILE,O_WRONLY);
	if (fd >= 0){
		ENG_LOG("%s open Ok PRODUCTINFO_FILE = %s ",__FUNCTION__ , PRODUCTINFO_FILE);
		len = write(fd, databuf, data_len);

		if (len <= 0){
			ret = 1;
			ENG_LOG("%s read fail PRODUCTINFO_FILE = %s ",__FUNCTION__ , PRODUCTINFO_FILE);
		}
		close(fd);
	} else {
		ENG_LOG("%s open fail PRODUCTINFO_FILE = %s ",__FUNCTION__ , PRODUCTINFO_FILE);
		ret = 1;
	}

	if(ret ==0){
		fd = open(PRODUCTINFO_FILE0,O_WRONLY);
		if (fd >= 0){
			ENG_LOG("%s open Ok PRODUCTINFO_FILE = %s ",__FUNCTION__ , PRODUCTINFO_FILE0);
			len = write(fd, databuf, data_len);

			if (len <= 0){
				ENG_LOG("%s read fail PRODUCTINFO_FILE = %s ",__FUNCTION__ , PRODUCTINFO_FILE0);
			}
			close(fd);
		} else {
			ENG_LOG("%s open fail PRODUCTINFO_FILE = %s ",__FUNCTION__ , PRODUCTINFO_FILE0);
		}
	}
  return ret;
}

