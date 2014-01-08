#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utils/Log.h>
#include <cutils/sockets.h>
#include <time.h>
#include "packet.h"

struct image_info {
	char *image_path;
	char *image_path_bak;
	unsigned int image_size;
	unsigned int address;
};
//#define __TEST_SPI_ONLY__
#define	MODEM_POWER_PATH	"/sys/devices/platform/modem_interface/modempower"
#define	DLOADER_PATH		"/dev/dloader"
#define	UART_DEVICE_NAME	"/dev/ttyS1"

#define FDL_OFFSET		0
#define	FDL_PACKET_SIZE 	256
#define HS_PACKET_SIZE		(32*1024)

#define FDL_CP_PWRON_DLY	(180*1000)//us
#define FDL_CP_UART_TIMEOUT	(200) //ms
#define FDL_CP_UART_REC_DLY	(5*1000) //us

#define	DL_FAILURE		(-1)
#define DL_SUCCESS		(0)
char test_buffer[HS_PACKET_SIZE+128]={0};

static int modem_images_count=7;
static char *uart_dev = UART_DEVICE_NAME;
static int need_shutdown = 0;
static int fdl_cp_poweron_delay = FDL_CP_PWRON_DLY;

struct image_info download_image_info[] = {
	{ //fdl
		"/dev/block/platform/sprd-sdhci.3/by-name/tddsp",
		"/dev/block/platform/sprd-sdhci.3/by-name/tddsp",
		0x3400,
		0x20000000,
	},
	{ //PARM
		"/dev/block/platform/sprd-sdhci.3/by-name/tdmodem",
		"/dev/block/platform/sprd-sdhci.3/by-name/tdmodem",
		0x590c00,
		0x80400000,
	},
	{ //parm fixvn
		"/dev/block/platform/sprd-sdhci.3/by-name/tdfixnv1",
		"/dev/block/platform/sprd-sdhci.3/by-name/tdfixnv1",
		0x28000,
		0x809B0000,
       },
       { //cmdline
		"/proc/cmdline",
		"/proc/cmdline",
		0x400,
		0x80A10000,
	},
	{ //	CA5
		"/dev/block/platform/sprd-sdhci.3/by-name/wcnmodem",
		"/dev/block/platform/sprd-sdhci.3/by-name/wcnmodem",
		0x790000,
		0x81F00000,
	},
	{ //ca5 fixvn
		"/dev/block/platform/sprd-sdhci.3/by-name/wcnfixnv1",
		"/dev/block/platform/sprd-sdhci.3/by-name/wcnfixnv1",
		0x28000,
		0x82690000,
	},
	{ //cmdline
		"/proc/cmdline",
		"/proc/cmdline",
		0x400,
		0x826F0000,
	},
	{
		NULL,
		NULL,
		0,
		0,
	},
};
static int modem_interface_fd = -1;
static int boot_status = 0;
int speed_arr[] = {B921600,B115200, B38400, B19200, B9600, B4800, B2400, B1200, B300,
                   B921600,B115200, B38400, B19200, B9600, B4800, B2400, B1200, B300, };
int name_arr[] = {921600,115200,38400,  19200, 9600,  4800,  2400,  1200,  300,
        921600, 115200,38400,  19200,  9600, 4800, 2400, 1200,  300, };

int get_modem_images_info_bak(void)
{
    FILE *fp;
    int images_count = 0;
    char line[256];
    unsigned long address,length;
    struct image_info *info = download_image_info;
    int max_item = sizeof(download_image_info)/sizeof(download_image_info[0]);

    if(max_item == 0)
        return 0;

    if (!(fp = fopen("/modem_images.info", "r"))) {
        return 0;
    }
    printf("start parse modem images file\n");

    while(fgets(line, sizeof(line), fp)) {
        const char *delim = " \t";
        char *save_ptr;
        char *filename, *address_ptr, *length_ptr;


        line[strlen(line)-1] = '\0';

        if (line[0] == '#' || line[0] == '\0')
            continue;

        if (!(filename = strtok_r(line, delim, &save_ptr))) {
            printf("Error parsing type");
            break;
        }
        if (!(length_ptr = strtok_r(NULL, delim, &save_ptr))) {
            break;
        }
        if (!(address_ptr = strtok_r(NULL, delim, &save_ptr))) {
            printf("Error parsing label");
            break;
        }
        printf("%s:%s:%s\n",filename,&length_ptr[2],&address_ptr[2]);
        info[images_count].image_path = strdup(filename);
        info[images_count].image_size = strtol(&length_ptr[2],&save_ptr,16);
        info[images_count].address = strtol(&address_ptr[2],&save_ptr,16);
        if((info[images_count].address == 0) || (info[images_count].image_size==0)){
                /*get tty device number from modem_images.info*/
                uart_dev = info[images_count].image_path;
                printf("UART Device = %s",uart_dev);
                if(info[images_count].image_size != 0){
                        /*get cp power delay param from modem_images.info*/
                        fdl_cp_poweron_delay = info[images_count].image_size;
                }
        }else {
                images_count++;
        }
        if(images_count >= max_item) break;
    }
    fclose(fp);
    modem_images_count = images_count;
    return images_count;
}


static unsigned short calc_checksum(unsigned char *dat, unsigned long len)
{
	unsigned long checksum = 0;
	unsigned short *pstart, *pend;
	if (0 == (unsigned long)dat % 2)  {
		pstart = (unsigned short *)dat;
		pend = pstart + len / 2;
		while (pstart < pend) {
			checksum += *pstart;
			pstart ++;
		}
		if (len % 2)
			checksum += *(unsigned char *)pstart;
		} else {
		pstart = (unsigned char *)dat;
		while (len > 1) {
			checksum += ((*pstart) | ((*(pstart + 1)) << 8));
			len -= 2;
			pstart += 2;
		}
		if (len)
			checksum += *pstart;
	}
	checksum = (checksum >> 16) + (checksum & 0xffff);
	checksum += (checksum >> 16);
	return (~checksum);
}

/*
	TRUE(1): pass
	FALSE(0): fail
*/
static int _chkEcc(unsigned char* buf, int size)
{
	unsigned short crc,crcOri;
//	crc = __crc_16_l_calc(buf, size-2);
//	crcOri = (uint16)((((uint16)buf[size-2])<<8) | ((uint16)buf[size-1]) );

	crc = calc_checksum(buf,size-4);
	crcOri = (unsigned short)((((unsigned short)buf[size-3])<<8) | ((unsigned short)buf[size-4]) );

	return (crc == crcOri);
}
int _chkImg(char *fileName, int size)
{
	unsigned char* buf;
	int fileHandle = 0;;
	int ret=0;

	buf = malloc(size);
	memset(buf,0xFF,size);
	fileHandle = open(fileName, O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
	if(fileHandle < 0){
		free(buf);
		return 0;
	}
	ret = read(fileHandle, buf, size);
	close(fileHandle);
	// check IO
	if(ret != size){
		free(buf);
		return 0;
	}
	//check crc
	if(_chkEcc(buf, size)){
		free(buf);
		return 1;
	}
	free(buf);
	return 0;
}

int get_modem_images_info(void)
{
	FILE *fp;
	char line[512];
	int cnt;
	unsigned int address,size;
	char originPath[100],bakPath[100];
	 int max_item =( sizeof(download_image_info)/sizeof(download_image_info[0]))-1;

// 1 get image info from config file
	if (!(fp = fopen("/modem_images.info", "r"))) {
		return 0;
	}
	printf("start parase modem images file\n");

	cnt = 0;
	memset(download_image_info,0,sizeof(download_image_info));
	modem_images_count = 0;
	printf("\toriginImage\t\tbackupImage\t\tlength\taddress\n");
	while(fgets(line, sizeof(line), fp)) {
		line[strlen(line)-1] = '\0';
		if (line[0] == '#' || line[0] == '\0'){
			continue;
		}
		if(-1 == sscanf(
				line,"%s %s %x %x",
				originPath,
				bakPath,
				&download_image_info[cnt].image_size,
				&download_image_info[cnt].address
			)
		){
			continue;
		}
		download_image_info[cnt].image_path = strdup(originPath);
		download_image_info[cnt].image_path_bak = strdup(bakPath);

		printf("\t%32s\t%32s\t0x%8x\t0x%8x\n",download_image_info[cnt].image_path,download_image_info[cnt].image_path_bak,download_image_info[cnt].image_size,download_image_info[cnt].address);

		if((0 == download_image_info[cnt].address) || 0 == (download_image_info[cnt].image_size)){
			/*get tty device number from modem_images.info*/
			uart_dev = download_image_info[cnt].image_path;
			printf("UART Device = %s",uart_dev);
			if(download_image_info[cnt].image_size != 0){
			/*get cp power delay param from modem_images.info*/
			fdl_cp_poweron_delay = download_image_info[cnt].image_size;
			}
		}else {
			cnt++;
		}
		if(max_item <= cnt){
			printf("Max support %d item, this config has too many item!!!\n",max_item);
			break;
		}
	}
	fclose(fp);
	download_image_info[cnt].image_path		= 0;
	download_image_info[cnt].image_path_bak	= 0;
	download_image_info[cnt].image_size		= 0;
	download_image_info[cnt].address			= 0;
	printf("end parase %d!\n",cnt);
	modem_images_count = cnt;

// 2 check image file
	for(cnt = 0;  cnt < modem_images_count; cnt++){
		if(!strcmp(download_image_info[cnt].image_path,download_image_info[cnt].image_path_bak)){
			continue;
		}
		if(_chkImg(download_image_info[cnt].image_path, download_image_info[cnt].image_size)){
			continue;
		}
		if(!_chkImg(download_image_info[cnt].image_path_bak, download_image_info[cnt].image_size)){
			continue;
		}
		strcpy(download_image_info[cnt].image_path,download_image_info[cnt].image_path_bak);
	}
// 3 return
	return cnt;
}


void print_modem_image_info(void)
{
        int i;
        struct image_info *info = download_image_info;
        printf("modem_images_count = %d .\n",modem_images_count);

        for(i=0;i<modem_images_count;i++){
                printf("image[%d]: %s  size 0x%x  address 0x%x\n",i,info[i].image_path,info[i].image_size,info[i].address);
        }
}

static void reset_modem(void)
{
	int modem_power_fd;
	//return;
	printf("reset modem ...\n");
	modem_power_fd = open(MODEM_POWER_PATH, O_RDWR);
        if(modem_power_fd < 0)
		return;
        if(need_shutdown == 1){
             write(modem_power_fd,"0",2);
		usleep(500*1000);
	}
	need_shutdown = 1;
        write(modem_power_fd,"1",2);
        close(modem_power_fd);
}
void delay_ms(int ms)
{
	struct timeval delay;
	delay.tv_sec = 0;
	delay.tv_usec = ms * 1000;
	select(0, NULL, NULL, NULL, &delay);
}

static void try_to_connect_modem(int uart_fd)
{
	unsigned long hand_shake = 0x7E7E7E7E;
	char buffer[64]={0};
	char *version_string = (char *)buffer;
	char *data = version_string;
	int modem_connect = 0;
	int i,ret,delay;
	long options;
	struct timespec delay_time;

	delay_time.tv_sec = 0;
	printf("try to connect modem(%d)......\n",boot_status);
	modem_connect = 0;
	for(;;){
	       usleep(FDL_CP_UART_REC_DLY);
		write(uart_fd,&hand_shake,3);
		data = version_string;
		ret = read(uart_fd,version_string,1);
		if(ret == 1){
			printf("end %d 0x%x\n",ret,version_string[0]);
			if(version_string[0]==0x7E){
				modem_connect=0;
				data++;
				do{
					ret = read(uart_fd,data,1);
					if(ret == 1){
				 		if(*data == 0x7E){
							modem_connect = 1;
							printf("Version string received:");
							i=0;
							do{
								printf("0x%02x",version_string[i]);
								i++;
							}while(data > &version_string[i]);
							printf("0x%02x",version_string[i]);
							printf("\n");
							break;
						}
						data++;
						if ( (data - version_string) >= sizeof(buffer)) {
							printf("invalid version: rubbish data in driver");
							break;
						}
					}  else {
						modem_connect += 2;
					}
				}while(modem_connect < FDL_CP_UART_TIMEOUT);
			} else {
				if(version_string[0] == 0x55){
					write(uart_fd,&hand_shake,3);
					modem_connect += 40;
				}
				modem_connect += 2;
			}
		}
		if(modem_connect == 1)
			break;
		modem_connect += 2;
	}
}

int download_image(int channel_fd,struct image_info *info)
{
	int packet_size;
	int image_fd;
	int read_len;
	char *buffer;
	int i,image_size;
	int count = 0;
	int ret;

        if(info->image_path == NULL)
                return DL_SUCCESS;

	image_fd = open(info->image_path, O_RDONLY,0);

	if(image_fd < 0){
		printf("open file: %s error = %d\n", info->image_path,errno);
		return DL_SUCCESS;
	}

	printf("Start download image %s image_size 0x%x address 0x%x\n",info->image_path,info->image_size,info->address);
	image_size = info->image_size;
	count = (image_size+HS_PACKET_SIZE-1)/HS_PACKET_SIZE;
	ret = send_start_message(channel_fd,count*HS_PACKET_SIZE,info->address,1);
	if(ret != DL_SUCCESS){
		close(image_fd);
		return DL_FAILURE;
	}
	for(i=0;i<count;i++){
		packet_size = HS_PACKET_SIZE;
		buffer = (char *)&test_buffer[8];
		do{
			read_len = read(image_fd,buffer,packet_size);
			if(read_len > 0){
				packet_size -= read_len;
				buffer += read_len;
			  } else break;
		}while(packet_size > 0);
		if(image_size < HS_PACKET_SIZE){
			for(i=image_size;i<HS_PACKET_SIZE;i++)
				test_buffer[i+8] = 0xFF;
			image_size = 0;
		}else { image_size -= HS_PACKET_SIZE;}
		ret = send_data_message(channel_fd,test_buffer,HS_PACKET_SIZE,1);
		continue;
		if(ret != DL_SUCCESS){
			close(image_fd);
			return DL_FAILURE;
		}
	}

	ret = send_end_message(channel_fd,1);
	close(image_fd);
	return ret;
}

int download_images(int channel_fd)
{
	struct image_info *info;
	int i ,ret;
	int image_count = modem_images_count - 1;

	info = &download_image_info[1];
	for(i=0;i<image_count;i++){
		ret = download_image(channel_fd,info);
		if(ret != DL_SUCCESS)
			break;
		info++;
	}
	send_exec_message(channel_fd,0x80400000,1); //parm
	return ret;
}

void * load_fdl2memory(int *length)
{
	int fdl_fd;
	int read_len,size;
	char *buffer = NULL;
	char *ret_val = NULL;
	struct image_info *info;

	info = &download_image_info[0];
	fdl_fd = open(info->image_path, O_RDONLY,0);
	if(fdl_fd < 0){
		printf("open file %s error = %d\n", info->image_path, errno);
		return NULL;
	}
	size = info->image_size;
        buffer = malloc(size+4);
        if(buffer == NULL){
                close(fdl_fd);
                printf("no memory\n");
                return NULL;
        }
        ret_val = buffer;
	do{
		read_len = read(fdl_fd,buffer,size);
		if(read_len > 0)
		{
			size -= read_len;
			buffer += read_len;
		}
	}while(size > 0);
	close(fdl_fd);
	if(length)
		*length = info->image_size;
	return ret_val;
}
static int download_fdl(int uart_fd)
{
	int size=0,ret;
	int data_size=0;
	int offset=0;
	int translated_size=0;
	int ack_size = 0;
	char *buffer,data = 0;
	char *ret_val = NULL;
	char test_buffer1[256]={0};

	buffer = load_fdl2memory(&size);
	printf("fdl image info : address %p size %x\n",buffer,size);
	if(buffer == NULL)
		return DL_FAILURE;
	ret_val = buffer;
	ret = send_start_message(uart_fd,size,download_image_info[0].address,0);
	if(ret == DL_FAILURE){
                free(ret_val);
                return ret;
        }
	while(size){
		ret = send_data_message(uart_fd,buffer,FDL_PACKET_SIZE,0);
		if(ret == DL_FAILURE){
			free(ret_val);
			return ret;
		}
		buffer += FDL_PACKET_SIZE;
		size -= FDL_PACKET_SIZE;
	}
	ret = send_end_message(uart_fd,0);
	if(ret == DL_FAILURE){
		free(ret_val);
		return ret;
	}
	ret = send_exec_message(uart_fd,download_image_info[0].address,0);
	free(ret_val);
	return ret;
}
static void print_log_data(char *buf, int cnt)
{
}
void set_raw_data_speed(int fd, int speed)
{
    unsigned long   	i;
    int   		status;
    struct termios   	Opt;

    tcflush(fd,TCIOFLUSH);
    tcgetattr(fd, &Opt);
    for ( i= 0;  i  < sizeof(speed_arr) / sizeof(int);  i++){
        if  (speed == name_arr[i])
        {
	    //set raw data mode
            Opt.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
            Opt.c_oflag &= ~OPOST;
            Opt.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
            Opt.c_cflag &= ~(CSIZE | PARENB);
            Opt.c_cflag |= CS8;

	    Opt.c_iflag = ~(ICANON|ECHO|ECHOE|ISIG);
	    Opt.c_oflag = ~OPOST;
	    cfmakeraw(&Opt);
            //set baudrate
            cfsetispeed(&Opt, speed_arr[i]);
            cfsetospeed(&Opt, speed_arr[i]);
            status = tcsetattr(fd, TCSANOW, &Opt);
            if  (status != 0)
                perror("tcsetattr fd1");
            return;
        }
    }
 }


int open_uart_device(int polling_mode,int speed)
{
    int fd;
    if(polling_mode == 1)
	fd = open( uart_dev, O_RDWR|O_NONBLOCK );         //| O_NOCTTY | O_NDELAY
    else
	fd = open( uart_dev, O_RDWR);
    if(fd >= 0)
	set_raw_data_speed(fd,speed);
    return fd;
}
int modem_boot(void)
{
    int uart_fd;
    int ret=0;
    unsigned int i;
    int nread;
    char buff[512];
    unsigned long offset = 0,step = 4*1024;

reboot_modem:
#ifndef __TEST_SPI_ONLY__
    uart_fd = open_uart_device(1,115200);
    if(uart_fd < 0)
	return -1;

    do{
	boot_status = 0;
	try_to_connect_modem(uart_fd);
	boot_status = 1;
	ret = send_connect_message(uart_fd,0);
    }while(ret < 0);

    ret = download_fdl(uart_fd);
    if(ret == DL_FAILURE){
	close(uart_fd);
	goto reboot_modem;
    }
    try_to_connect_modem(uart_fd);
    close(uart_fd);
    uart_fd = open_uart_device(0,115200);
    if(uart_fd< 0)
	return -1;
    uart_send_change_spi_mode_message(uart_fd);
#endif

    modem_interface_fd = open(DLOADER_PATH, O_RDWR);
    if(modem_interface_fd < 0){
	printf("open dloader device failed ......\n");
        for(;;)
        {
		modem_interface_fd = open(DLOADER_PATH, O_RDWR);
		if(modem_interface_fd>=0)
                        break;
		sleep(1);
        }
    }
    printf("open dloader device successfully ... \n");
    ret = download_images(modem_interface_fd);
    close(uart_fd);
    close(modem_interface_fd);
    if(ret == DL_FAILURE){
	sleep(2);
	goto reboot_modem;
    }
    printf("MODEM boot finished ......\n");
    return 0;
}
