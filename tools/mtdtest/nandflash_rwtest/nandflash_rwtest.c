#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <mtd/mtd-user.h>
#include <time.h>
#include <utils/Log.h>

#define BITS_PER_BYTE 8
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_down(x, y) ((x) & ~__round_mask(x, y))
#define TAUSWORTHE(s,a,b,c,d) ((s&c)<<d) ^ (((s <<a) ^ s)>>b)

#undef LOG_TAG
#define LOG_TAG "nandflash_rwtest"

#define debug_info(fmt, args...) 	\
	do {                     	\
        	ALOGE(fmt, ## args);    \
		printf(fmt, ## args);   \
	} while(0)

struct rnd_state {
        unsigned int s1, s2, s3;
};

struct rnd_state state1 = {0};
static unsigned int test_count     = 0;    /*times to repeat the testing*/
static unsigned char *bbt = NULL;
static char *default_mtd = "/dev/mtd/mtd4";

static inline unsigned int __seed(unsigned int x, unsigned int m)
{
        return (x < m) ? x + m : x;
}

/**
 * prandom_seed_state - set seed for prandom_unsigned int_state().
 * @state: pointer to state structure to receive the seed.
 * @seed: arbitrary 64-bit value to use as a seed.
 */
static inline void prandom_seed_state(struct rnd_state *state, unsigned long long seed)
{
        unsigned int i = (seed >> 32) ^ (seed << 10) ^ seed;

        state->s1 = __seed(i, 1);
        state->s2 = __seed(i, 7);
        state->s3 = __seed(i, 15);
}

unsigned int prandom_32_state(struct rnd_state *state)
{

        state->s1 = TAUSWORTHE(state->s1, 13, 19, 4294967294UL, 12);
        state->s2 = TAUSWORTHE(state->s2, 2, 25, 4294967288UL, 4);
        state->s3 = TAUSWORTHE(state->s3, 3, 11, 4294967280UL, 17);

        return (state->s1 ^ state->s2 ^ state->s3);
}

void prandom_bytes_state(struct rnd_state *state, void *buf, int bytes)
{
        int i = 0;
        unsigned int j = 0;
        unsigned char *p = buf;
        unsigned int random = 0;

        for (i = 0; i < round_down(bytes, sizeof(unsigned int)); i += sizeof(unsigned int)) {
                random = prandom_32_state(state);

                for (j = 0; j < sizeof(unsigned int); j++) {
                        p[i + j] = random;
                        random >>= BITS_PER_BYTE;
                }
        }
        if (i < bytes) {
                random = prandom_32_state(state);

                for (; i < bytes; i++) {
                        p[i] = random;
                        random >>= BITS_PER_BYTE;
                }
        }
}

void dump_buf(unsigned char *buf, unsigned int num, unsigned int col_num)
{
	unsigned int i = 0, j = 0;

	if(col_num == 0)
		col_num = 32;

	for(i = 0; i < num; i ++)
	{
		if((i%col_num) == 0) {
			if(i != 0)
				debug_info("\n");
			debug_info("%.8x:", j);
			j += col_num;
		}
		if((i%4) == 0)
			debug_info(" ");
		debug_info("%.2x", buf[i]);
	}
	debug_info("\n");
}

static int mtd_erase(int fd, int ebnum)
{
	unsigned int i = 0;
	mtd_info_t mtd;

	if (ioctl(fd,MEMGETINFO,&mtd) == 0) {
		erase_info_t erase;
		erase.length = mtd.erasesize;
		if(ebnum == -1) {
			for(i = 0; i < mtd.size/mtd.erasesize; i ++)
			{
				if(bbt[i] == 1) {
					continue;
				}
				erase.start = mtd.erasesize * i;

				if (ioctl(fd,MEMERASE,&erase) != 0) {
					debug_info("\n%s Erase error, %d %s\n", __FUNCTION__, errno, strerror(errno));
					return -EPERM;
				}
			}
		} else {
				if(bbt[ebnum] == 1) {
					debug_info("block %d is bad_block!\n", ebnum);
					return -1;
				}
				erase.start = mtd.erasesize * ebnum;

				if (ioctl(fd,MEMERASE,&erase) != 0) {
					debug_info("\n%s Erase error, %d %s\n", __FUNCTION__, errno, strerror(errno));
					return -EPERM;
				}
		}
	} else {
		debug_info("%s MEMGETINFO error\n", __FUNCTION__);
		return -EPERM;
	}

	return 0;
}

/*
@fd: the handle of device
@ebnum:compose write option with @page
-2    :write the whole mtdpartion by random value
-1    :write nothing
other :special block
@page :compose write option with @ebnum
-1    :the special block will be jumped when write
other :the special block will be jumped expect the special page
*/
static int mtd_write(int fd, int ebnum, int page)
{
	int ret = -EINVAL;
	loff_t offset = 0;
	mtd_info_t mtd = {0};
	unsigned char *buf = NULL;

	if (ioctl(fd, MEMGETINFO, &mtd) == 0) {
		buf = (unsigned char *) malloc (mtd.writesize);
		if(!buf)
			return -ENOMEM;
	} else {
		debug_info("%s MEMGETINFO error\n", __FUNCTION__);
		return -EPERM;
	}

	if(ebnum == -1) {
		free(buf);
		return 0;
	}

	for (offset = 0; offset < mtd.size; offset += mtd.writesize) {
		if(bbt[offset/mtd.erasesize] == 1) {//jump the bad_block
			offset += mtd.erasesize;
			continue;
		}
		if (offset != lseek (fd, offset, SEEK_SET))
		{
			debug_info("%s lseek error, %d %s\n", __FUNCTION__, errno, strerror(errno));
			free (buf);
			return -EIO;
		}

		prandom_bytes_state(&state1, buf, mtd.writesize);

		if((ebnum == -2) ||//normal write
		   (((offset/mtd.erasesize)!= ebnum) ||//not special block
		   ((page != -1) && (((offset - ebnum * mtd.erasesize)/mtd.writesize) == page)))) {
			buf[0] = (unsigned char)(offset/mtd.erasesize);//block num
        	        buf[1] = (unsigned char)((offset%mtd.erasesize)/mtd.writesize);//page num
        	        buf[2] = 0;
        	        buf[3] = 0;
        	        buf[4] = 'u';//magic
        	        buf[5] = 'b';
        	        buf[6] = 'i';
        	        buf[7] = '&';
			ret = write (fd, buf, mtd.writesize);
			if (ret < 0) {
				free (buf);
				debug_info( "%s write error %d %s\n", __FUNCTION__, errno, strerror(errno));
				return -EFAULT;
			}
		}
	}

	free (buf);
	return ret;
}

/*
@fd: the handle of device
@ebnum: compose the read option with @page
-2   :the whole mtdpartion should be random value
-1   :the whole mtdpartion should be 0xff
other:special block
@page:compose the read option with @ebnum
-1   :the special block should be 0xff
other:the special block should be 0xff except the special page
*/
static int mtd_read(int fd, int ebnum, int page)
{
	int ret = 0;
	loff_t offset = 0;
	mtd_info_t mtd = {0};
	unsigned char *buf = NULL;
	unsigned char *verify_buf = NULL;

	ret = ioctl(fd,MEMGETINFO,&mtd);
	if (ret == 0) {
		buf = (unsigned char *) malloc (mtd.writesize);
		verify_buf = (unsigned char *) malloc (mtd.writesize);
		if(!buf || !verify_buf) {
			ret = -ENOMEM;
			goto out;
		}
		memset(buf, 0, mtd.writesize);
		memset(verify_buf, 0, mtd.writesize);
	} else {
		debug_info("\n%s MEMGETINFO error, %d %s\n", __FUNCTION__, errno, strerror(errno));
		return ret;
	}

	for (offset = 0; offset < mtd.size; offset += mtd.writesize) {
		if(bbt[offset/mtd.erasesize] == 1) {//bad block
			offset += mtd.erasesize;
			continue;
		}
		if (offset != lseek (fd, offset, SEEK_SET))
		{
			debug_info("%s lseek error, %d %s\n", __FUNCTION__, errno, strerror(errno));
			ret = -EIO;
			goto out;
		}

		ret = read (fd, buf, mtd.writesize);
		if (ret < 0) {
			     debug_info("\n%s read error, %d %s\n", __FUNCTION__, errno, strerror(errno));
			     goto out;
		}

		prandom_bytes_state(&state1, verify_buf, mtd.writesize);

		if((ebnum == -1) ||//the whole mtdpartion are 0xff
		   (((((unsigned int)offset)/mtd.erasesize) == ebnum) && ((page == -1) || (((offset - ebnum * mtd.erasesize)/mtd.writesize) != page)))) {
			memset(verify_buf, 0xff, mtd.writesize);
		} else {
			verify_buf[0] = (unsigned char)(offset/mtd.erasesize);//block num
	                verify_buf[1] = (unsigned char)((offset%mtd.erasesize)/mtd.writesize);//page num
	                verify_buf[2] = 0;
	                verify_buf[3] = 0;
	                verify_buf[4] = 'u';//magic
	                verify_buf[5] = 'b';
	                verify_buf[6] = 'i';
	                verify_buf[7] = '&';
		}

		if(memcmp(buf, verify_buf, mtd.writesize) != 0) {
			debug_info("error, addr:%.8x, the write value and the read value are not match!\n", (unsigned int)offset);
			if((verify_buf[4] == 'u') && (verify_buf[5] == 'b') && (verify_buf[6] == 'i') && (verify_buf[7]) == '&') {
				debug_info("we want to get %d:%d, but really get %d:%d\n", verify_buf[0], verify_buf[1], buf[0], buf[1]);
				debug_info("dump the write_buf first:\n");
				dump_buf(verify_buf, mtd.writesize, 0);
				debug_info("dump the read_buf then:\n");
				dump_buf(buf, mtd.writesize, 0);
			}
			if(ebnum != -2) {
				ret = -1;
				goto out;
			}
		}
	}
out:
	free(verify_buf);
	free (buf);
	return ret;
}

static int mtd_pretest(char *mtd_device)
{
	int fd = 0;
	int ret = 0;
	mtd_info_t mtd = {0};
	unsigned int ebnum = 10, page = 10;

	/* Open and size the device */
	if(mtd_device == NULL) {
		debug_info( "Error:mtd_device is null!");
		return -EINVAL;
	}
	if ((fd = open(mtd_device, O_SYNC | O_RDWR )) < 0) {
		debug_info("%s  open error, %d\n" , __FUNCTION__, errno);
		return -EIO;
	}

	if (ioctl(fd, MEMGETINFO, &mtd) != 0) {
		debug_info("%s MEMGETINFO error\n", __FUNCTION__);
		ret = -EPERM;
		goto out;
	}

	while((ebnum < (mtd.size/mtd.erasesize)) && bbt[ebnum])
		ebnum ++;

	if(ebnum == (mtd.size/mtd.erasesize)) {
		ebnum = 0;
		while((ebnum < (mtd.size/mtd.erasesize)) && bbt[ebnum])
			ebnum ++;

		if(ebnum == (mtd.size/mtd.erasesize)) {
			debug_info("error, the whole mtd_partion are bad_block!\n");
			ret = - 1;
			goto out;
		}
	}
	debug_info("The ebnum is %d\n", ebnum);

	ret = mtd_erase(fd, -1); if(ret < 0){goto out;}//erase the whole mtdpartion first
	ret = mtd_read(fd, -1, -1); if(ret < 0){goto out;}//verify the whole mtdpartion has been cleaned
	debug_info("1.1 successfully!\n");

	prandom_seed_state(&state1, 1);
	ret = mtd_write(fd, -2, -1); if(ret < 0) {goto out;}//write the whole mtdpartion

	ret = mtd_erase(fd, ebnum); if(ret < 0){goto out;}//erase one block
	prandom_seed_state(&state1, 1);
	ret = mtd_read(fd, ebnum, -1); if(ret < 0){goto out;}//verify the whole mtdpartion except one block
	debug_info("1.2 successfully!\n");

	prandom_seed_state(&state1, 1);
	ret = mtd_erase(fd, -1); if(ret < 0){goto out;}
	ret = mtd_write(fd, ebnum, page); if(ret < 0) {goto out;}//write only one page
	prandom_seed_state(&state1, 1);
	ret = mtd_read(fd, ebnum, page); if(ret < 0){goto out;}//verify the whole mtdpartion, expect all 0xff but one page has value
	debug_info("mtd_pretest successfully!\n");
out:
	close(fd);
	return ret;
}

/*show the mtd info*/
static int show_mtd_info(char* mtd_device)
{
	int i = 0;
	int fd = 0;
	int bad = 0;
	int ret = 0;
	loff_t offset = 0;
	mtd_info_t mtd = {0};

	if(mtd_device == NULL){
		debug_info("mtd_divice should not be NULL!\n");
		return -EINVAL;
	}
	/* Open and size the device */
	if ((fd = open(mtd_device, O_RDWR)) < 0){
		debug_info("%s  open error\n" , __FUNCTION__);
		return -EIO;
	}

	if (ioctl(fd, MEMGETINFO, &mtd) != 0){
		debug_info("%s MEMGETINFO error\n", __FUNCTION__);
		return -EPERM;

	}
	bbt = (unsigned char *)malloc(mtd.size/mtd.erasesize);
	if(!bbt) {
		close(fd);
		return -ENOMEM;
	}
	memset(bbt, 0, mtd.size/mtd.erasesize);

	/* Check all the blocks in an erase block for bad blocks */
	do {
		if ((ret = ioctl(fd, MEMGETBADBLOCK, &offset)) < 0) {
			debug_info("%s MEMGETBADBLOCK error\n", __FUNCTION__);
			return -EPERM;
		}
		if (ret == 1) {
			bad++;
			bbt[offset/mtd.erasesize] = 1;
		}
		offset+=  mtd.erasesize;
	} while ( offset < mtd.size );


	debug_info("mtd.type= ");
	switch (mtd.type)
	{
	case MTD_ABSENT:
		debug_info("MTD_ABSENT");
		break;
	case MTD_RAM:
		debug_info("MTD_RAM");
		break;
	case MTD_ROM:
		debug_info("MTD_ROM");
		break;
	case MTD_NORFLASH:
		debug_info("MTD_NORFLASH");
		break;
	case MTD_NANDFLASH:
		debug_info("MTD_NANDFLASH");
		break;
	default:
		debug_info("(unknown type - new MTD API maybe?)");
	 }

	debug_info("\nmtd.flags = ");
	if (mtd.flags == MTD_CAP_ROM)
		debug_info("MTD_CAP_ROM");
	else if (mtd.flags == MTD_CAP_RAM)
		debug_info("MTD_CAP_RAM");
	else if (mtd.flags == MTD_CAP_NORFLASH)
		debug_info("MTD_CAP_NORFLASH");
	else if (mtd.flags == MTD_CAP_NANDFLASH)
		debug_info("MTD_CAP_NANDFLASH");
	else if (mtd.flags == MTD_WRITEABLE)
		debug_info("MTD_WRITEABLE");
	else
		debug_info("MTD_UNKNOWN FLAG");

	debug_info("\n");
	debug_info("   size  \t pagesize\t  block  \t  obsize  \tbadblock/totalblock\n");
	debug_info(" 0x%.8x\t0x%.8x\t0x%.8x\t0x%.8x\t %d/%d\t\n", mtd.size,  mtd.writesize,mtd.erasesize,
		mtd.oobsize,bad,mtd.size/mtd.erasesize);
	return 0;
}

static int mtd_rw_test(char *mtd_device)
{
	int fd = 0;
	int ret = 0;
	unsigned int i = 0;
	mtd_info_t mtd = {0};

	/* Open and size the device */
	if(mtd_device == NULL) {
		debug_info( "Error:mtd_device is null!");
		return -EINVAL;
	}
	if ((fd = open(mtd_device, O_SYNC | O_RDWR )) < 0) {
		debug_info("%s  open error, %d\n" , __FUNCTION__, errno);
		return -EIO;
	}

	if (ioctl(fd,MEMGETINFO,&mtd) != 0) {
		debug_info("%s MEMGETINFO error\n", __FUNCTION__);
		close(fd);
		return -EPERM;
	}

	for(i = 0; i < test_count; i++)
	{
		ret = mtd_erase(fd, -1); if(ret < 0){goto exit;}
		prandom_seed_state(&state1, (i%20) + 1);
		ret = mtd_write(fd, -2, -1); if(ret < 0){goto exit;}
		prandom_seed_state(&state1, (i%20) + 1);
		ret = mtd_read(fd, -2, -1); if(ret < 0){goto exit;}
		debug_info("The %d time's test finished!\n", i );
	}

	ret = mtd_erase(fd, -1); if(ret < 0){goto exit;}
exit:
	close(fd);
	return ret;
}

int main(int argc, char **argv)
{
	char *cmd = NULL;
	int ret  = -EINVAL;
	char *mtd_device = NULL;
	unsigned int temp_count = 0;

	test_count = 5;
	switch(argc)
	{
		case 1:
		{
			mtd_device = default_mtd;
		}
		break;
		case 2:
		{
			mtd_device = strdup(argv[1]);
		}
		break;
		case 3:
		{
			mtd_device = strdup(argv[1]);
			temp_count = strtoul(argv[2], NULL, 0);
			test_count = temp_count ? temp_count : test_count;
		}
		break;
		default:
			debug_info("You should input right parameters!\n");
		break;
	}
	debug_info("the mtd_device is %s, the test_count is %d\n", mtd_device, test_count);

	ret = show_mtd_info(mtd_device);
	if(ret) {
		debug_info("The mtd_device is not corrected, the ret is %d\n", ret);
		goto out;
	}

	ret = mtd_pretest(mtd_device);
	if(ret < 0)
		goto out;

	ret = mtd_rw_test(mtd_device);

out:
	if(bbt)
		free(bbt);
	return ret;
}
