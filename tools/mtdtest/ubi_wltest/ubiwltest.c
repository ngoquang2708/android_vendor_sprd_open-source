//
#include<sys/types.h>
#include<unistd.h>
#include<stdio.h>
#include<stdlib.h>
#include<fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <mtd/mtd-user.h>
#include "ubi-user.h"

#define UBI_VOL_NUM_AUTO (-1)
#define UBI_DEV_NUM_AUTO (-1)

#define MAX_MTD_PARTITIONS 16

/* Maximum volume name length */
#define UBI_MAX_VOLUME_NAME 127


#define be32_to_cpu(x) ((__u32)(				\
	(((__u32)(x) & (__u32)0x000000ffUL) << 24) |		\
	(((__u32)(x) & (__u32)0x0000ff00UL) <<  8) |		\
	(((__u32)(x) & (__u32)0x00ff0000UL) >>  8) |		\
	(((__u32)(x) & (__u32)0xff000000UL) >> 24)))

#define be64_to_cpu(x) ((__u64)(				\
	(((__u64)(x) & (__u64)0x00000000000000ffULL) << 56) |	\
	(((__u64)(x) & (__u64)0x000000000000ff00ULL) << 40) |	\
	(((__u64)(x) & (__u64)0x0000000000ff0000ULL) << 24) |	\
	(((__u64)(x) & (__u64)0x00000000ff000000ULL) <<  8) |	\
	(((__u64)(x) & (__u64)0x000000ff00000000ULL) >>  8) |	\
	(((__u64)(x) & (__u64)0x0000ff0000000000ULL) >> 24) |	\
	(((__u64)(x) & (__u64)0x00ff000000000000ULL) >> 40) |	\
	(((__u64)(x) & (__u64)0xff00000000000000ULL) >> 56)))


struct ubi_leb2peb_req {
    __s32 lnum;
    __s32 pnum;
    __s32 ec;   
    __s32 ubi_num;
    __s32 vol_id;
} __packed;

struct ubi_ec_vid_req {
    __s32 ec[128];
    __s32 vid[128];
    __s32 ubi_num;
    __s32 vol_id;
} __packed;


struct ubi_deviation_mean {
    __s32 min;
    __s32 max;
    __s32 mean;
    float deviation;
} __packed;

struct ubi_ec_hdr {
    __be32  magic;
    __u8    version;
    __u8    padding1[3];
    __be64  ec; /* Warning: the current limit is 31-bit anyway! */
    __be32  vid_hdr_offset;
    __be32  data_offset;
    __be32  image_seq;
    __u8    padding2[32];
    __be32  hdr_crc;
} __packed;

struct ubi_vid_hdr {
    __be32  magic;
    __u8    version;
    __u8    vol_type;
    __u8    copy_flag;
    __u8    compat;
    __be32  vol_id;
    __be32  lnum;
    __u8    padding1[4];
    __be32  data_size;
    __be32  used_ebs;
    __be32  data_pad;
    __be32  data_crc;
    __u8    padding2[4];
    __be64  sqnum;
    __u8    padding3[12];
    __be32  hdr_crc;
} __packed;


#define UBI_CTRL_DEV "/dev/ubi_ctrl"
#define UBI_SYS_PATH "/sys/class/ubi"
#define MTD_SYS_PATH    "/sys/class/mtd"
#define UBI_DBG_PRINT(s) write(fd_mtd, s, strlen((char *)s))

enum {
    UBI_VID_DYNAMIC = 1,
    UBI_VID_STATIC  = 2
};

static struct {
    char name[16];
    int number;
} mtd_part_map[MAX_MTD_PARTITIONS];

struct ubi_inf {
    int     ubi_num;
    int     mtd_num;
    char    mtd_name[32];
};

static int mtdcdev_fd = -3;
static int fd_mtd;
static int s_leb_size;
static unsigned int ec_vid[0x1000] = {0};
static struct ubi_inf s_ubi_infs[8];

static struct ubi_ec_vid_req first_buf;
static struct ubi_ec_vid_req last_buf;

static int get_peb_ec(int pnum, int *ec, int *vid);
int get_wl_deviation_mean(int pebnums, struct ubi_deviation_mean *pdevi);

static double time_so_far(void)
{
    struct timeval tp;

    if(gettimeofday(&tp, (struct timezone *) NULL) == -1)
        perror("gettimeofday");
    return ((double) (tp.tv_sec)) + (((double) tp.tv_usec) * 0.000001 );
}

int do_getpeb_ec_vid(int ubi_num, int vol_id, struct ubi_ec_vid_req *req);
/*
 * gettime() - returns the time in seconds of the system's monotonic clock or
 * zero on error.
 */
static time_t gettime(void)
{
    struct timespec ts;
    int ret;

    ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (ret < 0) {
        printf("clock_gettime(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        return 0;
    }

    return ts.tv_sec;
}

static int wait_for_file(const char *filename, int timeout)
{
    struct stat info;
    time_t timeout_time = gettime() + timeout;
    int ret = -1;

    while (gettime() < timeout_time && ((ret = stat(filename, &info)) < 0))
        usleep(10000);

    return ret;
}

static int mtd_part_count = 0;
static void find_mtd_partitions(void)
{
    int fd;
    char buf[1024];
    char *pmtdbufp;
    ssize_t pmtdsize;
    int r;

    fd = open("/proc/mtd", O_RDONLY);
    if (fd < 0)
        return;

    buf[sizeof(buf) - 1] = '\0';
    pmtdsize = read(fd, buf, sizeof(buf) - 1);
    pmtdbufp = buf;
    while (pmtdsize > 0) {
        int mtdnum, mtdsize, mtderasesize;
        char mtdname[16];
        mtdname[0] = '\0';
        mtdnum = -1;
        r = sscanf(pmtdbufp, "mtd%d: %x %x %15s",
                   &mtdnum, &mtdsize, &mtderasesize, mtdname);
        if ((r == 4) && (mtdname[0] == '"')) {
            char *x = strchr(mtdname + 1, '"');
            if (x) {
                *x = 0;
            }
            printf("mtd partition %d, %s\n", mtdnum, mtdname + 1);
            if (mtd_part_count < MAX_MTD_PARTITIONS) {
                strcpy(mtd_part_map[mtd_part_count].name, mtdname + 1);
                mtd_part_map[mtd_part_count].number = mtdnum;
                mtd_part_count++;
            } else {
                printf("too many mtd partitions\n");
            }
        }
        while (pmtdsize > 0 && *pmtdbufp != '\n') {
            pmtdbufp++;
            pmtdsize--;
        }
        if (pmtdsize > 0) {
            pmtdbufp++;
            pmtdsize--;
        }
    }
    close(fd);
}

int mtd_name_to_number(const char *name)
{
    int n;
    if (mtd_part_count < 0) {
        mtd_part_count = 0;
        find_mtd_partitions();
    }
    for (n = 0; n < mtd_part_count; n++) {
        if (!strcmp(name, mtd_part_map[n].name)) {
            return mtd_part_map[n].number;
        }
    }
    return -1;
}

static int mtd_dev_read_int(int dev, const char *file, int def)
{
    int fd, val = def;
    char path[128], buf[64];

    sprintf(path, MTD_SYS_PATH "/mtd%d/%s", dev, file);
    wait_for_file(path, 5);
    fd = open(path, O_RDONLY);
    if(fd == -1){
        return val;
    }
    if(read(fd, buf, 64) > 0){
        val = atoi(buf);
    }
    close(fd);
    return val;
}

static int mtd_readbuf(char *dev_name, loff_t addr, int readbytes, int *preadbytes, char *buf)
{
    int rb;

    /* Open and size the device */
    if(mtdcdev_fd == -3)
    {
        if ((mtdcdev_fd = open(dev_name, O_RDWR, 0)) < 0){
            printf("%s  open error\n" , __FUNCTION__);
            return 1;
        }
        printf("%s(wz): mtdcdev_fd is %d\n", __FUNCTION__, mtdcdev_fd);
    }

    lseek(mtdcdev_fd, addr, SEEK_SET);
    rb = read(mtdcdev_fd, buf, readbytes);
    if(rb < 0)
    {
        printf("read error!\n");
        close(mtdcdev_fd);
        return 1;
    }
    if(preadbytes)
    {
        *preadbytes = rb;
    }

    return 0;
}

int store_ubi_infs(int ubi_num, int mtd_num, char *mtd_name)
{
    int i;
    for(i = 0; i < (int)(sizeof(s_ubi_infs)/sizeof(s_ubi_infs[0])); i++)
    {
        if(s_ubi_infs[i].mtd_name[0] == 0)
        {
            s_ubi_infs[i].ubi_num = ubi_num;
            s_ubi_infs[i].mtd_num = mtd_num;
            strcpy(s_ubi_infs[i].mtd_name, mtd_name);
            return 0;
        }
    }
    return 1;
}

int ubinum_to_mtd_name(int ubi_num, char *mtd_name)
{
    int i;

    for(i = 0; i < (int)(sizeof(s_ubi_infs)/sizeof(s_ubi_infs[0])); i++)
    {
        if((s_ubi_infs[i].ubi_num == ubi_num) && (s_ubi_infs[i].mtd_name[0] != 0))
        {
            strcpy(mtd_name, s_ubi_infs[i].mtd_name);
            return 0;
        }
    }
    return 1;
}

int show_ubinum_mtdname(void)
{
    int i;

    for(i = 0; i < (int)(sizeof(s_ubi_infs)/sizeof(s_ubi_infs[0])); i++)
    {
        if(s_ubi_infs[i].mtd_name[0] != 0)
        {
            printf("%s, ubi_num=%d, mtd_name=%s\n", __FUNCTION__, s_ubi_infs[i].ubi_num, s_ubi_infs[i].mtd_name);
        }
        else
        {
            break;
        }
    }
    return 0;
}

static int ubi_dev_read_int(int dev, const char *file, int def)
{
    int fd, val = def;
    char path[128], buf[64];

    sprintf(path, UBI_SYS_PATH "/ubi%d/%s", dev, file);
    /*
    printf("(wz):ubi_dev_read_int, path=%s\n", path);
    */
    wait_for_file(path, 5);
    fd = open(path, O_RDONLY);
    if(fd == -1){
        return -1;
    }
    if(read(fd, buf, 64) > 0){
        val = atoi(buf);
    }
    close(fd);
    return val;
}

int do_ubiAttach(int nargs, char **args)
{
    struct ubi_attach_req req;
    int ubi_num;
    int fd;
    int ret;
    char buf[256]={0};

    printf("(wz)args = %x\n", (int)args);
    memset(&req, 0, sizeof(struct ubi_attach_req));
    req.ubi_num =(typeof(req.ubi_num))atoi(args[1]);
    if(-1 == req.ubi_num){
        req.ubi_num = UBI_DEV_NUM_AUTO;
    }
    req.mtd_num = (typeof(req.mtd_num))mtd_name_to_number( args[2]);
    if(req.mtd_num == -1)
    {
        printf("no such mtd partition!\n");
        return -1;
    }
    printf("mtd partition is %d\n", req.mtd_num);

    fd = open(UBI_CTRL_DEV, O_RDONLY);
    if(-1 == fd){
        printf("(wz)attach failure\n");
        return -1;
    }
    ret = ioctl(fd, UBI_IOCATT, &req);
    if(-1 == fd){
        return -1;
    }
    ubi_num = req.ubi_num;

    close(fd);

    printf("%s ubi_num=%d mtd_num=%d, mtd_name=%s\n", __FUNCTION__, ubi_num, req.mtd_num, args[2]);
    if(store_ubi_infs(ubi_num, req.mtd_num, args[2]))
    {
        printf("mtd partition store full!\n");
    }

    s_leb_size = ubi_dev_read_int(ubi_num, "eraseblock_size", 0);
    sprintf(buf, "attach finish ubi_num = %x, s_leb_size=%x\n", ubi_num, s_leb_size);
    UBI_DBG_PRINT(buf);

    return 0;
}

int do_ubiDetach(int ubi_num)
{
    struct ubi_attach_req req;
    int fd;
    int ret;

    fd = open(UBI_CTRL_DEV, O_RDONLY);
    if(-1 == fd){
        return -1;
    }
    ret = ioctl(fd, UBI_IOCDET, &ubi_num);
    close(fd);
    if(-1 == ret){
        return -1;
    }
    return 0;
}
/* 测试分区需要创建一个VOL */
int do_mkvol(int ubi_num)
{
    char name[16] = {0};
    int ret;
    int mtd_num;
    int ubi_dev;
    int vols, avail_lebs, leb_size;
    char path[128];
    char buf[256] = {0};
    struct ubi_mkvol_req mkvol_req = {0}; 
    
    strcpy(name, "test1");
    vols = ubi_dev_read_int(ubi_num, "volumes_count", -1);
    if(vols == -1)
    {
        printf("%s: no such ubi_num=%d\n", __FUNCTION__, ubi_num);
    }
    printf("(wz):do_mkvol, vols=%d\n", vols);

    if(vols == 0){
        sprintf(path, "/dev/ubi%d", ubi_num);
        ubi_dev = open(path, O_RDONLY);
        if(ubi_dev == -1){
            return -1;
        }
        avail_lebs = ubi_dev_read_int(ubi_num, "avail_eraseblocks", 0);
        leb_size = ubi_dev_read_int(ubi_num, "eraseblock_size", 0);
        memset(buf, 0, 256);
        sprintf(buf, "do_mkvol, avail_lebs=%d, leb_size=%d\n", avail_lebs, leb_size);
        UBI_DBG_PRINT(buf);
        memset(&mkvol_req, 0, sizeof(struct ubi_mkvol_req));
        mkvol_req.vol_id = UBI_VOL_NUM_AUTO;
        mkvol_req.alignment = 1;
        mkvol_req.bytes = (long long)avail_lebs * leb_size;
        mkvol_req.vol_type = UBI_DYNAMIC_VOLUME;
        ret = snprintf(mkvol_req.name, UBI_MAX_VOLUME_NAME + 1, "%s", name);
        mkvol_req.name_len = ret;
        memset(buf, 0, 256);
        sprintf(buf, "do_mkvol, mkvol_req.name=%s\n", mkvol_req.name);
        UBI_DBG_PRINT(buf);
        ioctl(ubi_dev, UBI_IOCMKVOL, &mkvol_req);
        memset(buf, 0, 256);
        sprintf(buf, "do_mkvol, vol_id=%d\n", mkvol_req.vol_id);
        UBI_DBG_PRINT(buf);
        close(ubi_dev);
    }
    return 0;
}

int do_rmvol(int ubi_num, int vol_id)
{
    char path[128];
    int ubi_dev;
    char buf[256] = {0};

    sprintf(path, "/dev/ubi%d", ubi_num);
    ubi_dev = open(path, O_RDONLY);
    if(ubi_dev == -1)
    {
        printf("%s, no such ubi_num=%d\n", __FUNCTION__, ubi_num);
        return 1;
    }
    ioctl(ubi_dev, UBI_IOCRMVOL, &vol_id);
    close(ubi_dev);
    
    sprintf(buf, "do_rmvol, ubi_num=%d, vol_id=%d\n", ubi_num, vol_id);
    UBI_DBG_PRINT(buf);

    return 0;
}

int do_eraseubi_leb(int lnum)
{
    int fd;
    int i = 0;
    int ret;
    struct ubi_map_req map_req = {0};

    fd = open("/dev/ubi1_test1", O_RDWR);
    if(fd == -1)
    {
        printf("open /dev/ubi1 fail\n");
        return 1;
    }
    else
    {
        printf("open fd=%d\n", fd);
    }

    map_req.lnum = lnum;
    for(i = 0; i < 256; i++)
    {
        ret = ioctl(fd, UBI_IOCEBMAP, &map_req);
        ret = ioctl(fd, UBI_IOCEBUNMAP, &map_req.lnum);
    }
    close(fd);

    return 0;
}

static int ubi_leb2peb(int ubi_num, int vol_id, int lnum, int *pnum, int *ec)
{
    struct ubi_ec_hdr   ec_hdr = {0};
    struct ubi_vid_hdr  vid_hdr = {0};
    int mtd_num, mtd_blks, mtd_erasesize;
    int r_vol_id, r_lnum;
    char   dev_name[64] = {0};
    int     i;
    loff_t  addr;

    /* workround at present, need maintenance later */
    mtd_num = 3;
    memset(dev_name, 0, 64);
    sprintf(dev_name, "/dev/mtd/mtd%d", mtd_num);
    mtd_erasesize = mtd_dev_read_int(mtd_num, "erasesize", 0);
    mtd_blks      = mtd_dev_read_int(mtd_num, "size", 0) / mtd_erasesize;

    for(i = 0; i < mtd_blks; i++)
    {
        addr = (loff_t)(i * mtd_erasesize + 4096);
        mtd_readbuf(dev_name, addr, sizeof(struct ubi_vid_hdr), NULL, (char *)&vid_hdr);
        r_vol_id = be32_to_cpu(vid_hdr.vol_id);
        r_lnum = be32_to_cpu(vid_hdr.lnum);
        if((r_lnum == lnum) && (r_vol_id == vol_id))
        {
            addr = (loff_t)(i * mtd_erasesize);
            mtd_readbuf(dev_name, addr, sizeof(struct ubi_vid_hdr), NULL, (char *)&ec_hdr);         
            *ec = (int)be64_to_cpu(ec_hdr.ec);       
            break;
        }
    }
    if(i == mtd_blks)
    {
        printf("no such lnum =%d, vol_id=%d\n", lnum, vol_id);
    }
    return 0;
}

int start_test_frame(int ubi_num, int vol_id, int lnum)
{
    int i;
    int fd;
    int ec;
    int vid;
    int pnum;
    int ret;
    char  buf[32] = {0};
    struct ubi_leb_change_req req = {0};

    s_leb_size = ubi_dev_read_int(ubi_num, "eraseblock_size", 0);
    for(i = 0; i < 256; i++)
    {
        fd = open("/dev/ubi1_test1", O_RDWR);
        if(fd == -1)
        {
            UBI_DBG_PRINT("open /dev/ubi1 UBI_IOCEBCH fail\n");
            return 1;
        }
        req.lnum = lnum;
        memset(buf, 0, 32);
        sprintf(buf, "Hello word %d.\n", i);
        req.bytes = strlen((char *)buf);
        ret = ioctl(fd, UBI_IOCEBCH, &req);
        ret = write(fd, buf, req.bytes);
        close(fd);

        fd = open("/dev/ubi1_test1", O_RDWR);
        if(fd == -1)
        {
            UBI_DBG_PRINT("open /dev/ubi1 read fail\n");
            return 1;
        }
        memset(buf, 0, 32);
        req.lnum = lnum;
        lseek(fd, req.lnum * s_leb_size, SEEK_SET);
        ret = read(fd, buf, req.bytes);
        close(fd);

        ret = ubi_leb2peb(ubi_num, vol_id, lnum, &pnum, &ec);
        if(ret == -1)
        {
            UBI_DBG_PRINT("open ubi_ctrl_ex failure\n");
            return 1;
        }

        fd = open("/dev/ubi1_test1", O_RDWR);
        if(fd == -1)
        {
            UBI_DBG_PRINT("open /dev/ubi1 UBI_IOCEBER fail\n");
            return 1;
        }
        /* erase the lnum */
        req.lnum = lnum;
        ret = ioctl(fd, UBI_IOCEBER, &req.lnum);
        close(fd);
    }

    return 0;
}

static int gouttime = 0;

int do_write_ec_void(int fd, struct ubi_ec_vid_req *req)
{
    int i, err;
    char buf[128];
    
    memset(buf, 0, 128);
    sprintf(buf, "========== %d\n", gouttime);
    err = write(fd, buf, strlen((char *)buf));
    for(i = 0; i < 32; i++)
    {
        memset(buf, 0, 128);
        sprintf(buf, "{0x%8x, %4d},{0x%8x, %4d},{0x%8x, %4d},{0x%8x, %4d},\n", req->vid[4*i], req->ec[4*i], req->vid[4*i+1], req->ec[4*i+1],req->vid[4*i+2], req->ec[4*i+2],req->vid[4*i+3], req->ec[4*i+3]);
        printf("%s", buf);
        err = write(fd, buf, strlen((char *)buf));
    }
    return 0;
}

int do_write_ec_void_diff(int fd, struct ubi_ec_vid_req *req1, struct ubi_ec_vid_req *req2)
{
    int i, err, diff = 0;
    char buf[128];

    memset(buf, 0, 128);
    sprintf(buf, "\n========== %d\n", gouttime);
    err = write(fd, buf, strlen((char *)buf));
    for(i = 0; i < 128; i++)
    {
        if(req1->ec[i] != req2->ec[i])
        {
            memset(buf, 0, 128);
            sprintf(buf, "{%4d, 0x%8x, %4d},", i, req1->vid[i], (int)(req2->ec[i] - req1->ec[i]));
            diff++;
            if((diff % 4) == 0)
            {
                strcat(buf, "\n");
            }
            err = write(fd, buf, strlen((char *)buf));
        }
    }
    return 0;
}



int do_test_wl(int ubi_num, int vol_id, int lnum, int num_of_times)
{
    int fd1;
    int fd2;
    int i;
    char buf[256] = {0};
    struct ubi_ec_vid_req req1;
    struct ubi_ec_vid_req req2;
    struct ubi_deviation_mean dev_mean;
    double start, stop;

    UBI_DBG_PRINT("do_test_wl start\n");

    remove("/data/test_wl.txt");
    remove("/data/test_wl_diff.txt");
    fd1 = open("/data/test_wl.txt", O_RDWR|O_CREAT, S_IRWXU);
    if(fd1 == -1)
    {
        UBI_DBG_PRINT("open /data/test_wl.txt fail\n");
        return -1;
    }
    fd2 = open("/data/test_wl_diff.txt", O_RDWR|O_CREAT, S_IRWXU);
    if(fd2 == -1)
    {
        UBI_DBG_PRINT("open /data/test_wl_diff.txt fail\n");
        return -1;
    }

    /*
    0x000000100000-0x000002100000 : "testubi"
    */
    start = time_so_far();
    gouttime = 0;
    memset(&req1, 0, sizeof(struct ubi_ec_vid_req));
    if(do_getpeb_ec_vid(ubi_num, vol_id, &req1) != 0)
    {
        printf("%s do_getpeb_ec_vid failure\n", __FUNCTION__);
        return 1;
    }
    memcpy(&first_buf, &req1, sizeof(struct ubi_ec_vid_req));
    do_write_ec_void(fd1, &req1);
    for(i = 0; i < num_of_times; i++)
    {
        start_test_frame(ubi_num, vol_id, lnum);
        gouttime++;
        if((gouttime % 4) == 0)
        {
            memset(&req2, 0, sizeof(struct ubi_ec_vid_req));
            do_getpeb_ec_vid(ubi_num, vol_id, &req2);
            do_write_ec_void(fd1, &req2);
            do_write_ec_void_diff(fd2, &req1, &req2);
            memcpy(&req1, &req2, sizeof(struct ubi_ec_vid_req));
        }
    }

    stop = time_so_far();
    close(mtdcdev_fd);
    printf("%s, num_of_times is %d, time is %f\n", __FUNCTION__, num_of_times, (stop - start));
    memcpy(&last_buf, &req1, sizeof(struct ubi_ec_vid_req));
    get_wl_deviation_mean(128, &dev_mean);

    close(fd1);
    close(fd2);

    memset(buf, 0, 256);
    sprintf(buf, "do_test_wl finish! num_of_times=%d.\n", num_of_times);
    UBI_DBG_PRINT(buf);

    memset(buf, 0, 256);
    sprintf(buf, "pebnums = %d, mean = %d, deviation=%f.\n", 128, dev_mean.mean, dev_mean.deviation);
    UBI_DBG_PRINT(buf);

    memset(buf, 0, 256);
    sprintf(buf, "min_ec = %d, max_ec = %d\n", dev_mean.min, dev_mean.max);
    UBI_DBG_PRINT(buf);

    return 0;
}


int get_wl_deviation_mean(int pebnums, struct ubi_deviation_mean *pdevi)
{
    int     i;
    float   f1, f2, f3;

    memset(pdevi, 0, sizeof(struct ubi_deviation_mean));
    pdevi->min = pdevi->max = last_buf.ec[0] - first_buf.ec[0];
    for(i = 0; i < pebnums; i++)
    {
        if((last_buf.ec[i] - first_buf.ec[i]) > pdevi->max)
        {
            pdevi->max = last_buf.ec[i] - first_buf.ec[i];
        }
        if((last_buf.ec[i] - first_buf.ec[i]) < pdevi->min)
        {
            pdevi->min = last_buf.ec[i] - first_buf.ec[i];
        }
        pdevi->mean += last_buf.ec[i] - first_buf.ec[i];
    }
    
    pdevi->mean = pdevi->mean / pebnums;
    f3 = (float)pdevi->mean;
    for(i = 0; i < pebnums; i++)
    {
        f1 = first_buf.ec[i];
        f2 = last_buf.ec[i];
        f2 = (f2 - f1 - f3)/f3;
        pdevi->deviation += f2*f2;
    }
    pdevi->deviation /= pebnums;

    return 0;
}

static int ubi_getpeb_ec_vid(int ubi_num, int vol_id, int *ec, int *vid)
{
    struct ubi_ec_hdr   ec_hdr = {0};
    struct ubi_vid_hdr  vid_hdr = {0};
    long long ec_raw;
    int    temp;
    int    mtd_num, mtd_blks, mtd_erasesize;
    loff_t addr;
    char   dev_name[64] = {0};
    int    i;

    /* workround at present, need maintenance later */
    mtd_num = 3;
    memset(dev_name, 0, 64);
    sprintf(dev_name, "/dev/mtd/mtd%d", mtd_num);
    mtd_erasesize = mtd_dev_read_int(mtd_num, "erasesize", 0);
    mtd_blks      = mtd_dev_read_int(mtd_num, "size", 0) / mtd_erasesize;

    for(i = 0; i < mtd_blks; i++)
    {
        addr = (loff_t)(i * mtd_erasesize);
        mtd_readbuf(dev_name, addr, sizeof(struct ubi_ec_hdr), NULL, (char *)&ec_hdr);
        ec[i] = (int)be64_to_cpu(ec_hdr.ec);
        addr = (loff_t)(i * mtd_erasesize + 4096);
        mtd_readbuf(dev_name, addr, sizeof(struct ubi_vid_hdr), NULL, (char *)&vid_hdr);
        vid[i] = (int)be32_to_cpu(vid_hdr.vol_id);
    }

    return 0;
}

int do_getpeb_ec_vid(int ubi_num, int vol_id, struct ubi_ec_vid_req *req)
{
    int fd;
    int ret;
    int i;
    char buf[256] = {0};

    return ubi_getpeb_ec_vid(ubi_num, vol_id, req->ec, req->vid);
}

int file_ops(int filenum)
{
    int     i, j, err;
    int     fd;
    char    filebuf[64];
    char    databuf[64];
    char    buf[256] = {0};

    for(i = 0; i < filenum; i++)
    {
        memset(filebuf, 0, 64);
        sprintf(filebuf, "/data/ubitest/powercut_%d.txt", i);
        fd = open(filebuf, O_RDWR|O_CREAT, S_IRWXU);
        if(fd == -1)
        {
            memset(buf, 0, 256);
            sprintf(buf, "create file %s failure errno =%d\n", filebuf, errno);
            UBI_DBG_PRINT(buf);
            break;
        }

        memset(databuf, 0, 64);
        for(j = 0; j < 8; j++)
        {
            sprintf(databuf, "hello ubifs powercut test%d!\n", j);
            write(fd, databuf, strlen((char *)databuf));
        }
        close(fd);
    }

    for(i = 0; i < filenum; i++)
    {
        memset(filebuf, 0, 64);
        sprintf(filebuf, "/data/ubitest/powercut_%d.txt", i);
        err = remove(filebuf);
        if(err)
        {
            memset(buf, 0, 256);
            sprintf(buf, "remove file %s failure errno =%d\n", filebuf, errno);
            UBI_DBG_PRINT(buf);            
        }
    }
    return 0;
}

/* 
   创建文件目录，mount到文件目录，产生给定文件数目，进行读/写/删除文件操作，最后unmount文件目录，并删除文件目录，然后
   反复多次操作，在此过程中随机断电，并重启，然后再自动执行类似程序
*/
int do_test_powercut(int filenum, int num_of_times)
{
    int     i, ret;
    char    buf[256] = {0};
    double start, stop;

    start = time_so_far();
    UBI_DBG_PRINT("do_test_powercut start\n");
    for(i = 0; i < num_of_times; i++)
    {
        /* create the output dir, for mount */
        ret = mkdir("/data/ubitest", 0x777);
        if(ret == -1)
        {
            if(errno != EEXIST)
            {
                memset(buf, 0, 256);                
                sprintf(buf, "mkdir errno =%d\n", errno);
                UBI_DBG_PRINT(buf);
                break;
            }
        }
        else
        {
            if((i % 64) == 0)
            {
                memset(buf, 0, 256);                
                sprintf(buf, "mkdir success, times=%d\n", i);
                UBI_DBG_PRINT(buf);
            }
        }

        /* mount the volume character device, */
        ret = mount("/dev/ubi1_test1", "/data/ubitest", "ubifs", 0, 0);
        if(ret == -1)
        {
            memset(buf, 0, 256);
            sprintf(buf, "mount errno =%d\n", errno);
            UBI_DBG_PRINT(buf);
            break;
        }
        else
        {
            if((i % 64) == 0)
            {
                memset(buf, 0, 256);                
                sprintf(buf, "mount success, times=%d\n", i);
                UBI_DBG_PRINT(buf);
            }
        }

        file_ops(filenum);
        
        ret = umount("/data/ubitest");
        if(ret == -1)
        {
            memset(buf, 0, 256);
            sprintf(buf, "unmount errno =%d\n", errno);
            UBI_DBG_PRINT(buf);
        }
        else
        {
            if((i % 64) == 0)
            {
                memset(buf, 0, 256);                
                sprintf(buf, "unmount success, times=%d\n", i);
                UBI_DBG_PRINT(buf);
            }
        }
        ret = rmdir("/data/ubitest");
        if(ret == -1)
        {
            memset(buf, 0, 256);
            sprintf(buf, "rmdir errno =%d\n", errno);
            UBI_DBG_PRINT(buf);
        }
        else
        {
            if((i % 64) == 0)
            {
                memset(buf, 0, 256);                
                sprintf(buf, "rmdir success, times=%d\n", i);
                UBI_DBG_PRINT(buf);
            }
        }
    }

    memset(buf, 0, 256);
    sprintf(buf, "do_test_powercut finish, filenum=%d, num_of_times=%d\n", filenum, num_of_times);
    UBI_DBG_PRINT(buf);
    
    stop = time_so_far();
    printf("%s, num_of_times is %d, time is %f\n", __FUNCTION__, num_of_times, (stop - start));

    memset(buf, 0, 256);
    sprintf(buf, "do_test_powercut finish, testtime=%f\n", (stop - start));
    UBI_DBG_PRINT(buf);

    return 0;
}

/* 测试分区需要创建n个VOL */
int do_mk_n_vols(int ubi_num, int nvols)
{
    char name[16] = {0};
    int i, ret;
    int mtd_num;
    int ubi_dev;
    int vols, avail_lebs, leb_size;
    char path[128];
    char buf[256] = {0};
    struct ubi_mkvol_req mkvol_req = {0};

    strcpy(name, "test");
    vols = ubi_dev_read_int(ubi_num, "volumes_count", -1);
    if(vols == -1)
    {
        printf("%s: no such ubi_num=%d\n", __FUNCTION__, ubi_num);
    }    
    printf("(wz):do_mk_n_vols, vols=%d\n", vols);

    if(vols == 0){
        sprintf(path, "/dev/ubi%d", ubi_num);
        ubi_dev = open(path, O_RDONLY);
        if(ubi_dev == -1){
            return -1;
        }
        avail_lebs = ubi_dev_read_int(ubi_num, "avail_eraseblocks", 0);
        leb_size = ubi_dev_read_int(ubi_num, "eraseblock_size", 0);
        memset(buf, 0, 256);
        sprintf(buf, "do_mk_n_vols, avail_lebs=%d, leb_size=%d\n", avail_lebs, leb_size);
        UBI_DBG_PRINT(buf);
        for(i = 0; i < nvols; i++)
        {
            memset(&mkvol_req, 0, sizeof(struct ubi_mkvol_req));
            mkvol_req.vol_id = UBI_VOL_NUM_AUTO;
            mkvol_req.alignment = 1;
            /*
            mkvol_req.bytes = (long long)avail_lebs * leb_size;
            */
            mkvol_req.bytes = (long long)15 * leb_size;
            mkvol_req.vol_type = UBI_DYNAMIC_VOLUME;
            ret = snprintf(mkvol_req.name, UBI_MAX_VOLUME_NAME + 1, "%s%d", name, i);
            mkvol_req.name_len = ret;
            memset(buf, 0, 256);
            sprintf(buf, "do_mk_n_vols, mkvol_req.name=%s\n", mkvol_req.name);
            UBI_DBG_PRINT(buf);
            ioctl(ubi_dev, UBI_IOCMKVOL, &mkvol_req);
            memset(buf, 0, 256);
            sprintf(buf, "do_mk_n_vols, vol_id=%d\n", mkvol_req.vol_id);
            UBI_DBG_PRINT(buf);
        }

        close(ubi_dev);
    }
    return 0;
}

int do_rm_nvol(int ubi_num, int nvols)
{
    char path[128];
    int i;
    int ubi_dev;
    char buf[256] = {0};

    sprintf(path, "/dev/ubi%d", ubi_num);
    ubi_dev = open(path, O_RDONLY);
    for(i = 0; i < nvols; i++)
    {
        ioctl(ubi_dev, UBI_IOCRMVOL, &i);
    }
    close(ubi_dev);
    
    sprintf(buf, "do_rm_nvol, ubi_num=%d, nvols=%d\n", ubi_num, nvols);
    UBI_DBG_PRINT(buf);

    return 0;
}

static int do_erase_testubi(void)
{
    int     fd = 0;
    int     i, ret;
    int     mtd_num, mtd_erasesize;
    char    dev_name[64] = {0};
    struct  erase_info_user argp = {0};

    mtd_num = mtd_name_to_number("testubi");
    mtd_erasesize = mtd_dev_read_int(mtd_num, "erasesize", 0);
    sprintf(dev_name, "/dev/mtd/mtd%d", mtd_num);
    
    printf("dev_name is %s, mtd_erasesize is %d\n", dev_name, mtd_erasesize);
    /* Open and size the device */
    if ((fd = open(dev_name, O_RDWR)) < 0){
        printf("%s  open error, errno=%d\n" , __FUNCTION__, errno);
        return 1;
    }

    argp.start = 0;
    argp.length = mtd_erasesize * 128;
    
    ret = ioctl(fd,MEMERASE,&argp);
    if(ret != 0)
    {
        printf("erase errorno is %d\n", ret);
        close(fd);
        return 1;
    }
    close(fd);
    
    return 0;
}

int main(int argc, char **argv)
{
    int  i;
    int  ops;
    char user_str[4][16];

    if(argc == 1)
    {
        printf("para nums is wrong!\n");
        printf("1 attach ubi\n");
        printf("2 detach ubi\n");
        printf("3 erase leb\n");
        printf("4 make volume\n");
        printf("5 rm volume\n");
        printf("6 test wl\n");
        printf("7 test power cut\n");
        printf("14 make volumes\n");
        printf("15 rm volumes\n");

        return 1;
    }
    printf("argc = %d, argv[0]=%s, argv[1]=%s\n", argc, argv[0], argv[1]);
    ops = atoi(argv[1]);

    fd_mtd = open("/data/mtd_test.txt", O_RDWR|O_APPEND);
    if(fd_mtd == -1)
    {
        if(errno == ENOENT)
        {
            fd_mtd = open("/data/mtd_test.txt", O_RDWR|O_CREAT, S_IRWXU);
        }
        else
        {
            printf("can't open file mtd_test.txt\n");
        }
    }
    
    switch(ops)
    {
        case 1:
            {
                char **pp;

                find_mtd_partitions();
                pp = (char **)malloc( 4 * sizeof(char *));
                memset(pp, 0, 4*sizeof(char *));
                for(i = 0; i < 4; i++)
                {
                    memset(user_str[i], 0, 16);
                }
                strcpy(user_str[1], "1");
                strcpy(user_str[2], "testubi");
                
                pp[1] = user_str[1];
                pp[2] = user_str[2];

                do_ubiAttach(0, pp);

                free(pp);
            }
            break;
        case 2:
            {
                int  ubi_num;
                ubi_num = 1;
                do_ubiDetach(ubi_num);
            }
            break;
        case 3:
            {
                do_eraseubi_leb(2);
            }
            break;
        case 4:
            {
                do_mkvol(1);
            }
            break;
        case 5:
            {
                do_rmvol(1, 0);
            }
            break;
        case 6:
            {
                int num_of_times;
                if(argc != 3)
                {
                    UBI_DBG_PRINT("parameter error!\n");
                    UBI_DBG_PRINT("./testubi 6 num_of_times\n");
                    return 1;
                }
                num_of_times = atoi(argv[2]);
                if(num_of_times < 1)
                {
                    num_of_times = 1;
                }
                do_test_wl(1, 0, 2, num_of_times);
            }
            break;
        case 7:
            {
                /* test power cut */
                int num_of_times, filenum;
                if(argc != 4)
                {
                    UBI_DBG_PRINT("parameter error!\n");
                    UBI_DBG_PRINT("./testubi 7 num_of_times filenum\n");                    
                    return 1;
                }
                num_of_times = atoi(argv[2]);
                filenum      = atoi(argv[3]);
                if(num_of_times == -1)
                {
                    num_of_times = 0x7fffffff;
                }
                if(num_of_times < 1)
                {
                    num_of_times = 1;
                }
                do_test_powercut(filenum, num_of_times);
            }
            break;
        case 8:
            {
                close(fd_mtd);
                remove("/data/mtd_test.txt");
                fd_mtd = -5;
            }
            break;
        case 14:
            do_mk_n_vols(1, 5);
            break;
        case 15:
            do_rm_nvol(1, 5);
            break;
        case 100:
            {
                find_mtd_partitions();
                do_erase_testubi();
            }
            break;
        default:
            break;
    }

    if(fd_mtd != -5)
    {
        close(fd_mtd);
    }
    return 0;
}
