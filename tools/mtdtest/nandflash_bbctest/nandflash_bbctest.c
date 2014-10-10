#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <utils/Log.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <mtd/mtd-user.h>

#undef LOG_TAG
#define LOG_TAG "nandflash_bbctest"

#define debug_info(fmt, args...) 	\
	do {                     	\
        	ALOGE(fmt, ## args);    \
		printf(fmt, ## args);   \
	} while(0)

typedef enum {
false = 0,
true = 1,
} bool;

mtd_info_t mtd = {0};
unsigned int blk_num = 0;
static unsigned char *bbt = NULL;
static char *default_mtd = "/dev/mtd/mtd4";
static char *CTL_DEV = "/sys/kernel/debug/nfc_base/allowEraseBadBlock";
static const char *RECORD_FILE = "/data/nandflash_bbctest_record.txt";
static const char *RESULT_FILE = "/data/nandflash_bbctest_result.txt";
unsigned int bad_pos = 0;
unsigned int bad_len = 2;

static int scan_bad_block_bbt(char *mtd_device)
{
	int ret = 0;
	int bad = 0;
	int fd = 0;
	loff_t offset = 0;

	if(mtd_device == NULL) {
		debug_info("mtd_divice should not be NULL!\n");
		return -EINVAL;
	}

	/* Open and size the device */
	if ((fd = open(mtd_device, O_RDWR)) < 0) {
		debug_info("%s  open error, %d\n" , __FUNCTION__, errno);
		return -EIO;
	}

	do {
		if ((ret = ioctl(fd, MEMGETBADBLOCK, &offset)) < 0) {
			debug_info("%s MEMGETBADBLOCK error\n", __FUNCTION__);
			close(fd);
			return -EPERM;
		}
		if (ret == 1) {
			debug_info("block %d is bad through scan bbt\n", ((unsigned int)offset)/mtd.erasesize);
			bad++;
		}
		offset+=  mtd.erasesize;
	} while ( offset < mtd.size );

	close(fd);
	return bad;
}

static int scan_bad_block_oob(char *mtd_device)
{
	int fd = 0;
	int ret = 0;
	int bad = 0;
	unsigned int i = 0;
	unsigned int offset = 0;
	unsigned char *buf = NULL;
	struct mtd_oob_buf oob_info = {0};

	if(!mtd_device) {
		debug_info("mtd_divice should not be NULL!\n");
		return -EINVAL;
	}

	/* Open and size the device */
	if ((fd = open(mtd_device, O_RDWR)) < 0) {
		debug_info("%s  open error, %d\n" , __FUNCTION__, errno);
		return -EIO;
	}

	buf = (unsigned char *)malloc(mtd.oobsize);
	if(!buf) {
		debug_info("%s, alloc buf failed!\n" , __FUNCTION__);
		close(fd);
		return -ENOMEM;
	}

	oob_info.ptr = buf;
	oob_info.length = mtd.oobsize;

	do {
		oob_info.start = offset;
		memset(buf, 0xff, mtd.oobsize);
		if ((ret = ioctl(fd, MEMREADOOB, &oob_info)) < 0) {
			debug_info("%s MEMREADOOB error\n", __FUNCTION__);
			free(buf);
			close(fd);
			return -EPERM;
		}
		for(i = bad_pos; i < (bad_pos + bad_len); i++) {
			if(buf[i] != 0xff) {
				bad ++;
				debug_info("block %d is bad through scan oob\n", ((unsigned int)offset)/mtd.erasesize);
				break;
			}
		}
		offset +=  mtd.erasesize;
	} while ( offset < mtd.size );

	free(buf);
	close(fd);
	return bad;
}

static void erase_prepare(char *path, bool enable)
{
	unsigned int value = enable ? 1 : 0;
	char cmdline[70] = {0};

	sprintf(cmdline, "echo %d > %s", value, path);
	system(cmdline);
}

static int erase_block(char *mtd_device, unsigned int ebnum, bool force)
{
        int i = 0;
	int fd = 0;
	int ret = 0;
        erase_info_t erase = {0};

	if(!mtd_device) {
		debug_info("mtd_divice should not be NULL!\n");
		return -EINVAL;
	}

	if ((fd = open(mtd_device, O_RDWR)) < 0) {
		debug_info("%s  open error, %d\n" , __FUNCTION__, errno);
		return -EIO;
	}

        erase.length = mtd.erasesize;
        erase.start = mtd.erasesize * ebnum;
        if(bbt[ebnum] == 1 && !force) {
                debug_info("block %d is bad_block!\n", ebnum);
                ret = -EPERM;
		goto out;
        }

        if (ioctl(fd,MEMERASE,&erase) != 0) {
                debug_info("\n%s Erase error, %d %s\n", __FUNCTION__, errno, strerror(errno));
                ret = -EPERM;
		goto out;
        }

out:
	close(fd);
        return ret;
}

static int get_mtd_info(char* mtd_device)
{
	int i = 0;
	int fd = 0;
	int bad = 0;
	int ret = 0;
	loff_t offset = 0;

	if(!mtd_device) {
		debug_info("mtd_divice should not be NULL!\n");
		return -EINVAL;
	}
	if ((fd = open(mtd_device, O_RDWR)) < 0) {
		debug_info("%s  open error, %d\n" , __FUNCTION__, errno);
		return -EIO;
	}

	if (ioctl(fd, MEMGETINFO, &mtd) != 0){
		debug_info("%s MEMGETINFO error\n", __FUNCTION__);
		close(fd);
		return -EPERM;

	}

	blk_num = mtd.size/mtd.erasesize;
	bbt = (unsigned char *)malloc(blk_num);
	if(!bbt) {
		close(fd);
		return -ENOMEM;
	}
	memset(bbt, 0, blk_num);

	do {
		if ((ret = ioctl(fd, MEMGETBADBLOCK, &offset)) < 0) {
			debug_info("%s MEMGETBADBLOCK error\n", __FUNCTION__);
			close(fd);
			return -EPERM;
		}
		if (ret == 1) {
			bad++;
			bbt[((unsigned int)offset)/mtd.erasesize] = 1;
		}
		offset+=  mtd.erasesize;
	} while ( offset < mtd.size );

	debug_info ("mtd.type= ");
	switch (mtd.type)
	{
	case MTD_ABSENT:
		debug_info ("MTD_ABSENT");
		break;
	case MTD_RAM:
		debug_info ("MTD_RAM");
		break;
	case MTD_ROM:
		debug_info ("MTD_ROM");
		break;
	case MTD_NORFLASH:
		debug_info ("MTD_NORFLASH");
		break;
	case MTD_NANDFLASH:
		debug_info ("MTD_NANDFLASH");
		break;
	default:
		debug_info ("(unknown type - new MTD API maybe?)");
	 }

	debug_info ("\nmtd.flags = ");
	if (mtd.flags == MTD_CAP_ROM)
		debug_info ("MTD_CAP_ROM");
	else if (mtd.flags == MTD_CAP_RAM)
		debug_info ("MTD_CAP_RAM");
	else if (mtd.flags == MTD_CAP_NORFLASH)
		debug_info ("MTD_CAP_NORFLASH");
	else if (mtd.flags == MTD_CAP_NANDFLASH)
		debug_info ("MTD_CAP_NANDFLASH");
	else if (mtd.flags == MTD_WRITEABLE)
		debug_info ("MTD_WRITEABLE");
	else
		debug_info ("MTD_UNKNOWN FLAG");

	debug_info("\n");
	debug_info("    size  \t pagesize\t  block  \t  obsize  \tbadblock/totalblock\n");
	debug_info ("0x%.8x\t0x%.8x\t0x%.8x\t0x%.8x\t %d/%d\t\n", mtd.size,  mtd.writesize,mtd.erasesize,
		mtd.oobsize,bad,blk_num);

	close(fd);
	return 0;
}

/*mark one block as bad block*/
static int bbctest_mark_bad(char* mtd_device, unsigned int blk)
{
	int fd = 0;
	loff_t offset = 0;

	if(!mtd_device) {
		debug_info("mtd_divice should not be NULL!\n");
		return -EINVAL;
	}

	if ((fd = open(mtd_device, O_RDWR)) < 0) {
		debug_info("%s  open error\n" , __FUNCTION__);
		return -EIO;
	}

	offset = blk * mtd.erasesize;
	if (ioctl(fd, MEMSETBADBLOCK, &offset) != 0){
		debug_info("%s, mark block %d as bad error!\n", __FUNCTION__, blk);
		close(fd);
		return -EPERM;
	}

	close(fd);
	return 0;
}

static int bbctest_main(char *mtd_device)
{
	int i = 0;
	int ret = 0;
	int ctl_dev = 0;
	FILE *fd = NULL;
	unsigned int test_blk = 0;
	char system_cmd[100] = {0};
	unsigned int is_first_enten = 1;
	unsigned int select_blk[3] = {0};
	unsigned int pos_start = 0, pos_end = 0;
	int bad_count_oob = 0, bad_count_bbt = 0;
	char *txt_buf = NULL, *tmp_buf = NULL, *tmp_ptr = NULL;

	/* Open and size the device */
	if(!mtd_device) {
		debug_info("mtd_device is null!\n");
		return -EINVAL;
	}

	debug_info("\nmtd_device is:%s\n", mtd_device);

	if(access(RECORD_FILE, 0)) {//file is not exist
		debug_info("This is the first time enten in %s!\n", __func__);
		for(i = 0; i < 3; i++)
		{
			while((test_blk < blk_num) && bbt[test_blk]) {
				test_blk++;
			}
			if(test_blk < blk_num) {
				select_blk[i] = test_blk++;
			} else {
				debug_info("There isn't enougn good blocks!\n");
				ret = -EINVAL;
				goto exit;
			}
		}

		debug_info("we will mark block: %d, %d, %d, as bad block!\n", select_blk[0], select_blk[1], select_blk[2]);
		for(i = 0; i < 3; i ++)
		{
			ret = bbctest_mark_bad(mtd_device, select_blk[i]);
			if(ret) {
				debug_info("mark blk %d as bad failed!\n", select_blk[i]);
				goto exit;
			}
		}

		fd = (FILE *)fopen(RECORD_FILE, "w+");
		if(!fd) {
			debug_info("open %s failed!\n", RECORD_FILE);
			return -EIO;
		}

		txt_buf = (char *)malloc(100);
		if(!txt_buf) {
			debug_info("alloc txt_buf failed!\n");
			goto exit;
		}
		sprintf(txt_buf, "bad=#%d#%d#%d#", select_blk[0], select_blk[1], select_blk[2]);
		fwrite(txt_buf, 1, 100, fd);
		fclose(fd);
		free(txt_buf);
		system("reboot");
	} else {
		debug_info("This is the second time enten in %s!\n", __func__);
		system("su");
		fd = (FILE *)fopen(RESULT_FILE, "w+");
		if(!fd) {
			debug_info("open file %s failed!\n", RESULT_FILE);
			goto clean;
		}

		bad_count_bbt = scan_bad_block_bbt(mtd_device);
		if(bad_count_bbt < 0) {
			debug_info("scan_bad_block_bbt failed!\n");
			fprintf(fd, "fail\n");
			fprintf(fd, "scan_bad_block_bbt failed!\n");
			fclose(fd);
			goto clean;
		}

		debug_info("the bad_count_bbt is %d\n", bad_count_bbt);

		bad_count_oob = scan_bad_block_oob(mtd_device);
		if(bad_count_oob < 0) {
			debug_info("scan_bad_block_oob failed!\n");
			fprintf(fd, "fail\n");
			fprintf(fd, "scan_bad_block_oob failed!\n");
			fclose(fd);
			goto clean;
		}
		debug_info("the bad_count_oob is %d\n", bad_count_oob);

		if(bad_count_oob != bad_count_bbt) {
			debug_info("compare bad_count_bbt and bad_count_oob failed, bbt: %d, oob: %d\n", bad_count_bbt, bad_count_oob);
			fprintf(fd, "fail\n");
			fprintf(fd, "the bad count from bbt and oob are not equal!\n");
			fclose(fd);
		} else {
			fprintf(fd, "success\n");
			fclose(fd);
		}

clean:
		fd = (FILE *)fopen(RECORD_FILE, "r");
		if(!fd) {
			debug_info("open %s failed!\n", RECORD_FILE);
			return -EINVAL;
		}

		txt_buf = (char *)malloc(100);
		if(!txt_buf) {
			debug_info("alloc txt_buf failed!\n");
			goto exit;
		}
		fread(txt_buf, 1, 100, fd);
		debug_info("the txt_buf is %s\n", txt_buf);
		tmp_buf = (char *)malloc(100);
		if(!tmp_buf) {
			debug_info("alloc tmp_buf failed!\n");
			goto exit;
		}

		erase_prepare(CTL_DEV, true);
		tmp_ptr = txt_buf;
		for(i = 0; i < 3; i ++) {
			while(txt_buf[pos_start] != '#') {
				pos_start ++;
			}
			pos_start ++;
			pos_end = pos_start + 1;
			while(txt_buf[pos_end] != '#') {
				pos_end ++;
			}
			pos_end --;
			memset(tmp_buf, 0, 100);
			memcpy(tmp_buf, txt_buf + pos_start, pos_end - pos_start + 1);
			select_blk[i] = strtoul(tmp_buf, NULL, 0);
			ret = erase_block(mtd_device, select_blk[i], 1);
			if(ret) {
				debug_info("clean the fake bad block failed!\n");
			}
			pos_start = pos_end ++;
		}
		sprintf(system_cmd, "rm %s", RECORD_FILE);
		system(system_cmd);
		erase_prepare(CTL_DEV, false);
	}

exit:
	if(txt_buf)
		free(txt_buf);
	if(tmp_buf)
		free(tmp_buf);
	if(fd)
		fclose(fd);
	if(ctl_dev)
		close(ctl_dev);
	return ret;
}

int main(int argc, char **argv)
{
	char *cmd = NULL;
	int ret  = -EINVAL;
	char *mtd_device = NULL;

	bad_pos = 0;
	bad_len = 2;
	mtd_device = default_mtd;
	switch(argc)
	{
		case 1:
			debug_info("Select the default values!\n");
		break;
		case 2:
		{
			mtd_device = strdup(argv[1]);
		}
		break;
		case 4:
		{
			mtd_device = strdup(argv[1]);
			bad_pos = strtoul(argv[2], NULL, 0);
			bad_len = strtoul(argv[3], NULL, 0);;
		}
		break;
		default:
		{
			debug_info("You should input right parameters!\n");
			return -EINVAL;
		}
		break;
	}
	debug_info("choose the mtd, %s, bad_pos: %d, bad_len: %d!\n", mtd_device, bad_pos, bad_len);

	ret = get_mtd_info(mtd_device);
	if(ret)
		return ret;
	ret = bbctest_main(mtd_device);

	if(bbt)
		free(bbt);
	return ret;
}
