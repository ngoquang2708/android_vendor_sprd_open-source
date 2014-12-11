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
#include "connectivity_rf_parameters.h"
#define REBOOT_DBG

#define	POWER_CTL		"/dev/power_ctl"
#define	DLOADER_PATH		"/dev/download"
#define	UART_DEVICE_NAME	"/dev/ttyS0"

#define DOWNLOAD_IOCTL_BASE	'z'
#define DOWNLOAD_POWER_ON	_IO(DOWNLOAD_IOCTL_BASE, 0x01)
#define DOWNLOAD_POWER_OFF	_IO(DOWNLOAD_IOCTL_BASE, 0x02)
#define DOWNLOAD_POWER_RST	_IO(DOWNLOAD_IOCTL_BASE, 0x03)

#define WCN_SOCKET_NAME       "external_wcn"
#define WCN_MAX_CLIENT_NUM		(10)
#define EXTERNAL_WCN_ALIVE "WCN-EXTERNAL-ALIVE"

typedef struct pmanager {
	pthread_mutex_t client_fds_lock;
	int client_fds[WCN_MAX_CLIENT_NUM];
	int listen_fd;
	bool flag_connect;
}pmanager_t;

pmanager_t pmanager;

typedef enum _DOWNLOAD_STATE {
		DOWNLOAD_INIT,
        DOWNLOAD_START,
        DOWNLOAD_BOOTCOMP,
} DOWNLOAD_STA_E;

struct image_info {
	char *image_path;
	unsigned int image_size;
	unsigned int address;
};

typedef struct  _NV_HEADER {
	unsigned int magic;
	unsigned int len;
	unsigned int checksum;
	unsigned int version;
}nv_header_t;

#define NV_HEAD_MAGIC   0x00004e56

//#define	FDL_PACKET_SIZE 	256
#define	FDL_PACKET_SIZE 	(1*1024)
#define LS_PACKET_SIZE		(256)
#define HS_PACKET_SIZE		(32*1024)

#define FDL_CP_PWRON_DLY	(160*1000)//us
#define FDL_CP_UART_TIMEOUT	(3000)//(200) //ms

#define	DL_FAILURE		(-1)
#define DL_SUCCESS		(0)

#define MS_IN_SEC 1000
#define NS_IN_MS  1000000

static DOWNLOAD_STA_E download_state = DOWNLOAD_INIT;
char test_buffer[HS_PACKET_SIZE+128]={0};
static char *uart_dev = UART_DEVICE_NAME;
static int fdl_cp_poweron_delay = FDL_CP_PWRON_DLY;

int speed_arr[] = {B921600,B115200, B38400, B19200, B9600, B4800, B2400, B1200, B300,
                   B921600,B115200, B38400, B19200, B9600, B4800, B2400, B1200, B300, };
int name_arr[] = {921600,115200,38400,  19200, 9600,  4800,  2400,  1200,  300,
        921600, 115200,38400,  19200,  9600, 4800, 2400, 1200,  300, };


struct wifi_calibration
{
	wifi_rf_t wifi_rf_cali;
	wifi_cali_cp_t wifi_cali_cp;
};
static struct wifi_calibration wifi_data;


static int download_images_count=2;	// download_images_count and download_image_info[] must keep matched!
struct image_info download_image_info[] = {
	{
		//fdl
		"/dev/block/platform/sprd-sdhci.3/by-name/wcnfdl",
		0x2800,
		0x80000000,
	},
	{
		//img
		"/dev/block/platform/sprd-sdhci.3/by-name/wcnmodem",
		0x80000,
		0x100000,
	},
};

static unsigned int delta_miliseconds(struct timespec *begin, struct timespec *end)
{
    long ns;
    unsigned int ms;
    time_t sec;

    if(NULL == begin || NULL == end){
        return 0;
    }

    ns = end->tv_nsec - begin->tv_nsec;
    sec = end->tv_sec - begin->tv_sec;
    ms = sec * MS_IN_SEC + ns / NS_IN_MS;
	if(ms == 0)
		ms = 1;
	return ms;
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
           // Opt.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
            Opt.c_oflag &= ~OPOST;
            Opt.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
            Opt.c_cflag &= ~(CSIZE | PARENB);
            Opt.c_cflag |= CS8;

	    //Opt.c_iflag = ~(ICANON|ECHO|ECHOE|ISIG);
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
	fd = open(uart_dev, O_RDWR|O_NONBLOCK );
    else
	fd = open(uart_dev, O_RDWR);
    if(fd >= 0)
	    set_raw_data_speed(fd,speed);
    return fd;
}

static int try_to_connect_device(int uart_fd)
{
	unsigned long hand_shake = 0x7E7E7E7E;
	char buffer[64]={0};
	char *version_string = (char *)buffer;
	char *data = version_string;
	int i,ret;
	int status = 0;
	int loopcount = 0;
	unsigned long long ms_delta;
	struct timespec tm_begin, tm_end;

	DOWNLOAD_LOGD("try to connect device......uart_fd = %d \n", uart_fd);

	if(-1 == clock_gettime(CLOCK_MONOTONIC, &tm_begin)){
		DOWNLOAD_LOGE("get tm_begin error \n");
		return -1;
	}

	for(;;){
		int try_count=0;
		if(-1 == clock_gettime(CLOCK_MONOTONIC, &tm_end)){
			DOWNLOAD_LOGE("get tm_end error \n");
			return -1;
		}
		ms_delta = delta_miliseconds(&tm_begin, &tm_end);
		if(ms_delta > FDL_CP_UART_TIMEOUT){
			loopcount++;
			if(loopcount == 5) {
				DOWNLOAD_LOGE("need to hard reset \n");
				return -1;
			}
			
			if(-1 == clock_gettime(CLOCK_MONOTONIC, &tm_begin)){
				DOWNLOAD_LOGE("get tm_begin error \n");
				return -1;
			}
		}
		hand_shake = 0x7E7E7E7E;
		ret = write(uart_fd,&hand_shake,3);
		if(ret < 0){
			DOWNLOAD_LOGD("UART Send HandShake(%d) %s %d\n",try_count,strerror(errno),uart_fd);
			fsync(uart_fd);
			close(uart_fd);
			uart_fd = open_uart_device(1,115200);
			continue;
		}
		try_count ++;
		write(uart_fd,&hand_shake,3);
		data = version_string;
		ret = read(uart_fd,version_string,1);
		if(ret == 1){
			DOWNLOAD_LOGD("end %d 0x%x\n",ret,version_string[0]);
			if(version_string[0]==0x7E){
				data++;
				do{
					ret = read(uart_fd,data,1);
					if(ret == 1){
				 		if(*data == 0x7E){
							status = 1;
							DOWNLOAD_LOGD("Version string received:");

							i=0;
							do{
								DOWNLOAD_LOGD("0x%02x",version_string[i]);
								i++;
							}while(data > &version_string[i]);
							DOWNLOAD_LOGD("0x%02x",version_string[i]);
							DOWNLOAD_LOGD("\n");
							break;
						}
						data++;
						if ( (data - version_string) >= sizeof(buffer)) {
							DOWNLOAD_LOGD("invalid version: rubbish data in driver");
							break;
						}
					}  else {
						if(-1 == clock_gettime(CLOCK_MONOTONIC, &tm_end)){
							DOWNLOAD_LOGE("get tm_end error \n");
							return -1;
						}
					}
				}while(delta_miliseconds(&tm_begin, &tm_end) < FDL_CP_UART_TIMEOUT);
			}
		}
		if(status == 1)
			return uart_fd;
	}
}

int download_image(int channel_fd,struct image_info *info)
{
	int packet_size,trans_size=HS_PACKET_SIZE;
	int image_fd;
	int read_len;
	char *buffer;
	char nvbuf[512];
	int i,image_size;
	int count = 0;
	int offset = 0;
	int ret;

        if(info->image_path == NULL)
                return DL_SUCCESS;
        if(info->image_size < HS_PACKET_SIZE)
                trans_size = LS_PACKET_SIZE;

	image_fd = open(info->image_path, O_RDONLY,0);

	if(image_fd < 0){
		DOWNLOAD_LOGE("open file: %s error = %d\n", info->image_path,errno);
		return DL_SUCCESS;
	}
	
	image_size = info->image_size;
	count = (image_size+trans_size-1)/trans_size;
	ret = send_start_message(channel_fd,count*trans_size,info->address,1);

	for(i=0;i<count;i++){
		packet_size = trans_size;
		buffer = (char *)&test_buffer[8];
		do{
			read_len = read(image_fd,buffer,packet_size);
			if(read_len > 0){
				packet_size -= read_len;
				buffer += read_len;
			  }else{
			  	break;
			  }
		}while(packet_size > 0);
		
		if(image_size < trans_size){
			for(i=image_size;i<trans_size;i++)
				test_buffer[i+8] = 0xFF;
			image_size = 0;
		}else{
			image_size -= trans_size;
		}
		
		ret = send_data_message(channel_fd,test_buffer,trans_size,1,trans_size,image_fd);
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
	int image_count = download_images_count - 1;

	info = &download_image_info[1];
	for(i=0;i<image_count;i++){
		ret = download_image(channel_fd,info);
		if(ret != DL_SUCCESS)
			break;
		info++;
	}
	send_exec_message(channel_fd,download_image_info[1].address,1);
	return ret;
}

void * load_fdl2memory(int *length)
{
	int fdl_fd;
	int read_len,size;
	char *buffer = NULL;
	char *ret_val = NULL;
	struct image_info *info;
	char nvbuf[512];
	nv_header_t *nv_head;

	info = &download_image_info[0];
	fdl_fd = open(info->image_path, O_RDONLY,0);
	if(fdl_fd < 0){
		DOWNLOAD_LOGE("open file %s error = %d\n", info->image_path, errno);
		return NULL;
	}

	read_len = read(fdl_fd,nvbuf, 512);
	nv_head = (nv_header_t*) nvbuf;
	if(nv_head->magic != NV_HEAD_MAGIC)
	{
		lseek(fdl_fd,SEEK_SET,0);
	}
	DOWNLOAD_LOGD("nvbuf.magic  0x%x \n",nv_head->magic);

	size = info->image_size;
        buffer = malloc(size+4);
        if(buffer == NULL){
                close(fdl_fd);
                DOWNLOAD_LOGE("no memory\n");
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
	DOWNLOAD_LOGD("fdl image info : address %p size %x\n",buffer,size);
	if(buffer == NULL)
		return DL_FAILURE;
	ret_val = buffer;
	ret = send_start_message(uart_fd,size,download_image_info[0].address,0);
	if(ret == DL_FAILURE){
                free(ret_val);
                return ret;
        }
	while(size){
		ret = send_data_message(uart_fd,buffer,FDL_PACKET_SIZE,0,0,0);
		if(ret == DL_FAILURE){
			free(ret_val);
			return ret;
		}
		buffer += FDL_PACKET_SIZE;
		size -= FDL_PACKET_SIZE;
	}
	DOWNLOAD_LOGD("send_end_message\n");
	ret = send_end_message(uart_fd,0);
	if(ret == DL_FAILURE){
		free(ret_val);
		return ret;
	}
	DOWNLOAD_LOGD("send_exec_message\n");
	ret = send_exec_message(uart_fd,download_image_info[0].address,0);
	free(ret_val);
	return ret;
}

static void download_power_on(bool enable)
{
	int fd;
	fd = open(POWER_CTL, O_RDWR);
	if(enable)
	{
		ioctl(fd,DOWNLOAD_POWER_ON,NULL);
	}
	else
	{
		ioctl(fd,DOWNLOAD_POWER_OFF,NULL);
	}
	close(fd);
}

static void download_hw_rst(void)
{
	int fd;
	fd = open(POWER_CTL, O_RDWR);
	ioctl(fd,DOWNLOAD_POWER_RST,NULL);
	close(fd);
}

static void download_wifi_calibration(int download_fd)
{
	int ret=0;

	DOWNLOAD_LOGD("start download calibration\n");

	ret = write(download_fd,"start_calibration",17);
	DOWNLOAD_LOGD("wifi_rf_t size:%d\n",sizeof(wifi_data.wifi_rf_cali));
	DOWNLOAD_LOGD("wifi_cali_cp_t size:%d\n",sizeof(wifi_data.wifi_cali_cp));

	/* start calibration*/
	get_connectivity_rf_param(&wifi_data.wifi_rf_cali);
	ret = write(download_fd,&wifi_data.wifi_rf_cali,sizeof(wifi_data.wifi_rf_cali));

	if(!wifi_data.wifi_rf_cali.wifi_cali.cali_config.is_calibrated){
		do{
			ret = read(download_fd,&wifi_data.wifi_cali_cp,sizeof(wifi_data.wifi_cali_cp));
			sleep(1);
		}while(ret <=0);

		wlan_save_cali_data_to_file(&wifi_data.wifi_cali_cp);
	}

	ret = write(download_fd,"end_calibration",15);

	DOWNLOAD_LOGD("end download calibration\n");
}

int download_entry(void)
{
	int uart_fd;
	int download_fd = -1;
	int ret=0;

	DOWNLOAD_LOGD("download_entry\n");
	download_power_on(1);
reboot_device:
	download_hw_rst();
    uart_fd = open_uart_device(1,115200);
    if(uart_fd < 0)
    {
		DOWNLOAD_LOGE("open_uart_device fail\n");
		return -1;
    }
	
	ret = try_to_connect_device(uart_fd);
	if(ret < 0) {
        close(uart_fd);
	    goto reboot_device;
    }

	uart_fd = ret;
	ret = send_connect_message(uart_fd,0);

    ret = download_fdl(uart_fd);
    if(ret == DL_FAILURE){
	    close(uart_fd);
	    goto reboot_device;
    }

	fsync(uart_fd);
    close(uart_fd);

    download_fd = open(DLOADER_PATH, O_RDWR);
    if(download_fd < 0){
	DOWNLOAD_LOGD("open dloader device failed retry ......\n");
        for(;;) {
            download_fd = open(DLOADER_PATH, O_RDWR);
            if(download_fd>=0) {
                break;
            } else {
                DOWNLOAD_LOGD("open dloader device failed retry ......\n");
                sleep(1);
            }
        }
    }
    DOWNLOAD_LOGD("open dloader device successfully ... \n");
    ret = download_images(download_fd);
    DOWNLOAD_LOGD("download finished ......\n");

	download_wifi_calibration(download_fd);
    close(download_fd);
	if(ret == DL_FAILURE){
		sleep(1);
		goto reboot_device;
	}
	
    return 0;
}

static void store_client_fd(int client_fds[], int fd)
{
	if(!client_fds) return;

	int i = 0;

	for (i = 0; i < WCN_MAX_CLIENT_NUM; i++)
	{
		if(client_fds[i] == -1) //invalid fd
		{
			client_fds[i] = fd;
			return;
		}
		else if(client_fds[i] == fd)
		{
			DOWNLOAD_LOGD("%s: Somethine error happens. restore the same fd:%d", __FUNCTION__, fd);
			return;
		}
	}

	if(i == WCN_MAX_CLIENT_NUM)
	{
		DOWNLOAD_LOGD("ERRORR::%s: client_fds is FULL", __FUNCTION__);
		client_fds[i-1] = fd;
		return;
	}
}


static void *client_listen_thread(void *arg)
{
	pmanager_t *pmanager = (pmanager_t *)arg;

	if(!pmanager)
	{
		DOWNLOAD_LOGE("%s: unexcept NULL pmanager", __FUNCTION__);
		exit(-1);
	}

	while(1)
	{
		int  i = 0;
		fd_set read_fds;
		int rc = 0;
		int max = -1;

		FD_ZERO(&read_fds);

		max = pmanager->listen_fd;
		FD_SET(pmanager->listen_fd, &read_fds);

		//if need to deal with the cmd sent from client, here add them to read_fds
		pthread_mutex_lock(&pmanager->client_fds_lock);
		for (i = 0; i < WCN_MAX_CLIENT_NUM; i++)
		{
			int fd = pmanager->client_fds[i];
			if(fd != -1) //valid fd
			{
				FD_SET(fd, &read_fds);
				if (fd > max)
					max = fd;
			}
		}
		pthread_mutex_unlock(&pmanager->client_fds_lock);

		//DOWNLOAD_LOGD("listen_fd = %d, max=%d", pmanager->listen_fd, max);
		if ((rc = select(max + 1, &read_fds, NULL, NULL, NULL)) < 0)
		{
			if (errno == EINTR)
				continue;

			//DOWNLOAD_LOGD("select failed (%s) listen_fd = %d, max=%d", strerror(errno), pmanager->listen_fd, max);
			sleep(1);
			continue;
		}
		else if (!rc)
			continue;

		if (FD_ISSET(pmanager->listen_fd, &read_fds))
		{
			struct sockaddr addr;
			socklen_t alen;
			int c;

			//accept the client connection
			do {
				alen = sizeof(addr);
				c = accept(pmanager->listen_fd, &addr, &alen);
				DOWNLOAD_LOGD("%s got %d from accept", WCN_SOCKET_NAME, c);
			} while (c < 0 && errno == EINTR);

			if (c < 0)
			{
				DOWNLOAD_LOGE("accept failed (%s)", strerror(errno));
				sleep(1);
				continue;
			}
			pmanager->flag_connect = 1;

			pthread_mutex_lock(&pmanager->client_fds_lock);
			store_client_fd(pmanager->client_fds, c);
			pthread_mutex_unlock(&pmanager->client_fds_lock);
		}
	}

}


static int socket_init(pmanager_t *pmanager)
{
	pthread_t thread_id;
	int i = 0;

	memset(pmanager, 0, sizeof(struct pmanager));

	for(i=0; i<WCN_MAX_CLIENT_NUM; i++)
		pmanager->client_fds[i] = -1;

	pmanager->listen_fd = socket_local_server(WCN_SOCKET_NAME, ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
	if(pmanager->listen_fd < 0) {
		DOWNLOAD_LOGE("%s: cannot create local socket server", __FUNCTION__);
		return -1;
	}

	if (pthread_create(&thread_id, NULL, client_listen_thread, pmanager))
	{
		DOWNLOAD_LOGE("start_client_listener: pthread_create (%s)", strerror(errno));
		return -1;
	}
	return 0;
}

static int send_notify_to_client(pmanager_t *pmanager, char *info_str)
{
	int i, ret;
	char *buf;
	int len;

	if(!pmanager || !info_str) return -1;

	DOWNLOAD_LOGD("send_notify_to_client");

	pthread_mutex_lock(&pmanager->client_fds_lock);

	/* info socket clients that WCN with str info */
	for(i = 0; i < WCN_MAX_CLIENT_NUM; i++)
	{
		int fd = pmanager->client_fds[i];
		DOWNLOAD_LOGD("client_fds[%d]=%d\n",i, fd);

		if(fd >= 0)
		{
			buf = info_str;
			len = strlen(buf) + 1;

			ret = write(fd, buf, len);
			if(ret < 0)
			{
				DOWNLOAD_LOGE("reset client_fds[%d]=-1",i);
				close(fd);
				pmanager->client_fds[i] = -1;
			}
		}
	}

	pthread_mutex_unlock(&pmanager->client_fds_lock);

	return 0;
}

#ifdef REBOOT_DBG

static void download_signal_handler(int sig)
{
	DOWNLOAD_LOGD("sig:%d\n",sig);
	exit(0);
}

#endif

int main(void)
{ 
	int ret=0;
#ifdef REBOOT_DBG
	/* Register signal handler */
	signal(SIGINT, download_signal_handler);
	signal(SIGKILL, download_signal_handler);
	signal(SIGTERM, download_signal_handler);
#endif
	ret = socket_init(&pmanager);

	download_state = DOWNLOAD_START;
	do{
		if(download_state == DOWNLOAD_START){
			if(download_entry() == 0)
			{
				download_state = DOWNLOAD_BOOTCOMP;
			}
		}

		if(pmanager.flag_connect){
			ret = send_notify_to_client(&pmanager, EXTERNAL_WCN_ALIVE);
			pmanager.flag_connect = 0;
		}
		sleep(1);
	}while(1);

	return 0;
}
