#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include<time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "eng_sqlite.h"
#include "engat.h"
#include <utils/Log.h>
#include "eng_attok.h"
#include "engapi.h"
#include "engopt.h"
#include "cutils/properties.h"
#include <string.h>
#include <errno.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG 			"BTWIFIMAC"

#define MAC_ERROR_EX			":::::"
#define MAC_ERROR			"FF:FF:FF:FF:FF:FF"
#define WIFI_MAC_FILE		"/data/wifimac.txt"
#define BT_MAC_FILE			"/data/btmac.txt"
#define MAC_RAND_FILE		"/data/rand_mac.txt"
#define KMSG_LOG			"/data/rand_kmsg.txt"
#define GET_BTMAC_ATCMD		"AT+SNVM=0,401"
#define GET_WIFIMAC_ATCMD	"AT+SNVM=0,409"

typedef enum {
	BT_MAC_ADDR=0,
	WIFI_MAC_ADDR
}MAC_ADDR;

#define MAC_FROM_ANDROID
static int counter=0;

static int read_btwifimac_from_database(char *btmac, char *wifimac)
{
	int bt_flag=0;
	int wifi_flag=0;
	char* bt_ptr, *wifi_ptr;

	ALOGD("%s",__FUNCTION__);

	//read btaddr
	bt_ptr=eng_sql_string2string_get("btaddr");
	if(strcmp(bt_ptr, ENG_SQLSTR2STR_ERR)!=0) {
		strcpy(btmac, bt_ptr);
		ALOGD("eng_setbtwifiaddr: bluetooth %s",btmac);
		bt_flag=1;
	}

	//read wifiaddr
	wifi_ptr=eng_sql_string2string_get("wifiaddr");
	if(strcmp(wifi_ptr, ENG_SQLSTR2STR_ERR)!=0) {
		strcpy(wifimac, wifi_ptr);
		ALOGD("eng_setbtwifiaddr: wifi %s",wifimac);
		wifi_flag=1;
	}

	return bt_flag & wifi_flag;
}

static int get_macaddress(char *mac_adr, char *data, MAC_ADDR type)
{
	int int1,i,j;
	char *ptr = data;
	char mac[6][3];

	char string1[128]={0};
	char *pstring1 = string1;

	memset(string1, 0, sizeof(string1));

	at_tok_start(&ptr);
	at_tok_nextint(&ptr, &int1);
	at_tok_nextstr(&ptr, &pstring1);

	memset(mac, 0 , sizeof(mac));

	for(i=0; i<6; i++) {
		strncpy(mac[i], pstring1+i*2, 2);
	}

	if(type == BT_MAC_ADDR)
		sprintf(mac_adr, "%s:%s:%s:%s:%s:%s",mac[5],mac[4],mac[3],mac[2],mac[1],mac[0]);
	else
		sprintf(mac_adr, "%s:%s:%s:%s:%s:%s",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

	ALOGD("%s: mac address is %s",__FUNCTION__,mac_adr);

	return 0;

}

static void write_to_randmacfile(char *btmac, char *wifimac)
{
	int fd;
	char buf[80];

	fd = open(MAC_RAND_FILE, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
	if( fd >= 0) {
		memset(buf, 0, sizeof(buf));
		sprintf(buf, "%s;%s",btmac, wifimac);
		write(fd, buf, sizeof(buf));
		close(fd);
	} else {
	    ALOGD("%s: errno=%d, errstr=%s",__FUNCTION__, errno, strerror(errno));
	}
	ALOGD("%s: %s fd=%d, data=%s",__FUNCTION__, MAC_RAND_FILE, fd,buf);
}

static int send_to_modem(int fd, char *buf, int buflen)
{
	ALOGD("%s",__FUNCTION__);
	return eng_write(fd, buf, buflen);
}

static int recv_from_modem(int fd, char *buf, int buflen)
{
	int n, counter;
	int readlen=-1;
	fd_set readfds;
	struct timeval timeout;

	timeout.tv_sec=20;
	timeout.tv_usec=0;

	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);

	ALOGD("%s: Waiting modem response ... fd=%d",__FUNCTION__, fd);

	counter = 0;
	while((n=select(fd+1, &readfds, NULL, NULL, &timeout))<=0) {
		counter++;
		ALOGD("%s: select n=%d, retry %d",__FUNCTION__, n, counter);
		if(counter > 3)
			break;
		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);
	}

	ALOGD("%s: Receive %d Modem Response",__FUNCTION__, n);

	if(n > 0)
		readlen=eng_read(fd, buf, buflen);

	return readlen;
}

static int read_btwifimac_from_modem(char *btmac, char *wifimac)
{
	int fd, timeout;
	int bt_ok=1;
	int wifi_ok=1;
	char cmdbuf[128];
	counter = 0;
	ALOGD("%s",__FUNCTION__);

	fd = engapi_open(0);
	ALOGD("%s modem_fd=%d",__FUNCTION__, fd);

	while (fd < 0) {
		usleep(100*1000);
		fd = engapi_open(0);
		ALOGD("%s modem_fd=%d",__FUNCTION__, fd);
	}


	ALOGD("===========BT MAC===========");
	//get bt mac address
	do {
		memset(cmdbuf, 0, sizeof(cmdbuf));
		sprintf(cmdbuf , "%d,%d,%s",ENG_AT_NOHANDLE_CMD, 1, GET_BTMAC_ATCMD);
		ALOGD("%s: send %s",__FUNCTION__, cmdbuf);
		send_to_modem(fd, cmdbuf, strlen(cmdbuf));
		memset(cmdbuf, 0, sizeof(cmdbuf));
		timeout = recv_from_modem(fd, cmdbuf, sizeof(cmdbuf));
		ALOGD("%s: BT timeout=%d, response=%s\n", __FUNCTION__,timeout,cmdbuf);
		usleep(100*1000);
		counter++;
	}while((timeout>0)&&(strstr(cmdbuf, "OK") == NULL));

	if(timeout<=0) {
		ALOGD("%s: Get BT MAC from modem fail",__FUNCTION__);
		engapi_close(fd);
		return 0;
	}

	get_macaddress(btmac, cmdbuf, BT_MAC_ADDR);
	if((strstr(btmac, MAC_ERROR)!=NULL)||(strstr(btmac, MAC_ERROR_EX)!= NULL))
		bt_ok = 0;

	ALOGD("===========WIFI MAC===========");
	//get wifi mac address
	do {
		memset(cmdbuf, 0, sizeof(cmdbuf));
		sprintf(cmdbuf , "%d,%d,%s",ENG_AT_NOHANDLE_CMD, 1, GET_WIFIMAC_ATCMD);
		ALOGD("%s: send %s",__FUNCTION__, cmdbuf);
		send_to_modem(fd, cmdbuf, strlen(cmdbuf));
		memset(cmdbuf, 0, sizeof(cmdbuf));
		timeout = recv_from_modem(fd, cmdbuf, sizeof(cmdbuf));
		ALOGD("%s: WIFI timeout=%d, response=%s\n", __FUNCTION__,timeout,cmdbuf);
	}while((timeout>0)&&(strstr(cmdbuf, "OK") == NULL));

	if(timeout<=0) {
		ALOGD("%s: Get WIFI MAC from modem fail",__FUNCTION__);
		engapi_close(fd);
		return 0;
	}

	get_macaddress(wifimac, cmdbuf, WIFI_MAC_ADDR);
	if((strstr(wifimac, MAC_ERROR)!=NULL)||(strstr(wifimac, MAC_ERROR_EX)!= NULL))
		wifi_ok = 0;

	engapi_close(fd);
	ALOGD("bt_ok:%d wifi_ok:%d \n",bt_ok,wifi_ok);
	return bt_ok & wifi_ok;
}

// realtek_add_start
static int get_urandom(unsigned int *buf, size_t len){
	int fd;
	size_t rc;

	ALOGD("+%s+",__FUNCTION__);
	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0){
		ALOGD("%s: Open urandom fail", __FUNCTION__);
		return -1;
	}
	rc = read(fd, buf, len);
	close(fd);
	ALOGD("-%s: rc: %d-",__FUNCTION__, rc);
	return rc;
}
// realtek_add_end

static void mac_rand(char *btmac, char *wifimac)
{
	int fd,i, j, k;
	off_t pos;
	char buf[80];
	char *ptr;
	unsigned int randseed;
	// realtek_add_start
	int rc;
	struct timeval tt;
	// realtek_add_end

	memset(buf, 0, sizeof(buf));

	// realtek_add_start
	ALOGD("+%s+",__FUNCTION__);
	// realtek_add_end
	if(access(MAC_RAND_FILE, F_OK) == 0) {
		ALOGD("%s: %s exists",__FUNCTION__, MAC_RAND_FILE);
		fd = open(MAC_RAND_FILE, O_RDWR);
		if(fd>=0) {
			read(fd, buf, sizeof(buf));
			ALOGD("%s: read %s %s",__FUNCTION__, MAC_RAND_FILE, buf);
			ptr = strchr(buf, ';');
			if(ptr != NULL) {

				if((strstr(wifimac, MAC_ERROR)!=NULL)||(strstr(wifimac, MAC_ERROR_EX)!=NULL)||(strlen(wifimac)==0))
					strcpy(wifimac, ptr+1);

				*ptr = '\0';

				if((strstr(btmac, MAC_ERROR)!=NULL)||(strstr(btmac, MAC_ERROR_EX)!=NULL)||(strlen(btmac)==0))
					strcpy(btmac, buf);

				ALOGD("%s: read btmac=%s, wifimac=%s",__FUNCTION__, btmac, wifimac);
				close(fd);
				return;
			}
			// realtek_add_start
			close(fd);
			// realtek_add_end
		}
	}

	// realtek_add_start
#if 0
	usleep(counter*1000);
	memset(buf, 0, sizeof(buf));
	sprintf(buf, "dmesg>%s",KMSG_LOG);
	system(buf);
	fd = open(KMSG_LOG, O_RDONLY);
	ALOGD("%s: counter=%d, fd=%d",__FUNCTION__, counter, fd);
	if (fd > 0) {
		pos = lseek(fd, -(counter*10), SEEK_END);
		memset(buf, 0, sizeof(buf));
		read(fd, buf, counter);
		ALOGD("%s: read %s: %s",__FUNCTION__, KMSG_LOG, buf);
		close(fd);
	}

	k=0;
	for(i=0; i<counter; i++)
		k += buf[i];

	//rand seed
	randseed = (unsigned int) time(NULL) + k*fd*counter + buf[counter-2];
	ALOGD("%s: randseed=%d",__FUNCTION__, randseed);
#endif

	rc = get_urandom(&randseed, sizeof(randseed));
	if (rc > 0) {
		ALOGD("urandom:%u", randseed);
	} else {
		if (gettimeofday(&tt, (struct timezone *)0) > 0)
			randseed = (unsigned int) tt.tv_usec;
		else
			randseed = (unsigned int) time(NULL);

		ALOGD("urandom fail, using system time for randseed");
	}
	// realtek_add_end
	ALOGD("%s: randseed=%u",__FUNCTION__, randseed);
	srand(randseed);

	//FOR BT
	i=rand(); j=rand();
	ALOGD("%s:  rand i=0x%x, j=0x%x",__FUNCTION__, i,j);
	sprintf(btmac, "00:%02x:%02x:%02x:%02x:%02x", \
									   (unsigned char)((i>>8)&0xFF), \
									   (unsigned char)((i>>16)&0xFF), \
									   (unsigned char)((j)&0xFF), \
									   (unsigned char)((j>>8)&0xFF), \
									   (unsigned char)((j>>16)&0xFF));

	//FOR WIFI
	i=rand(); j=rand();
	ALOGD("%s:  rand i=0x%x, j=0x%x",__FUNCTION__, i,j);
	sprintf(wifimac, "00:%02x:%02x:%02x:%02x:%02x", \
									   (unsigned char)((i>>8)&0xFF), \
									   (unsigned char)((i>>16)&0xFF), \
									   (unsigned char)((j)&0xFF), \
									   (unsigned char)((j>>8)&0xFF), \
									   (unsigned char)((j>>16)&0xFF));

	ALOGD("%s: bt mac=%s, wifi mac=%s",__FUNCTION__, btmac, wifimac);

	//create rand file
	write_to_randmacfile(btmac, wifimac);
}

static void write_mac2file(char *wifimac, char *btmac)
{
	int fd;

	//wifi mac
	fd = open(WIFI_MAC_FILE, O_CREAT|O_RDWR|O_TRUNC, S_IRUSR|S_IWUSR);
	ALOGD("%s: mac=%s, fd[%s]=%d",__FUNCTION__, wifimac, WIFI_MAC_FILE, fd);
	if(fd >= 0) {
		chmod(WIFI_MAC_FILE,0666);
		write(fd, wifimac, strlen(wifimac));
		close(fd);
	}

	//bt mac
	fd = open(BT_MAC_FILE, O_CREAT|O_RDWR|O_TRUNC, S_IRUSR|S_IWUSR);
	ALOGD("%s: mac=%s, fd[%s]=%d",__FUNCTION__, btmac, BT_MAC_FILE, fd);
	if(fd >= 0) {
		chmod(BT_MAC_FILE,0666);
		write(fd, btmac, strlen(btmac));
		close(fd);
	}
}

int main(void)
{
	int mac_ok=0;
	char bt_mac[32], *bt_ptr;
	char wifi_mac[32], *wifi_ptr;
	char mac_buf[80];

	ALOGD("set BT/WIFI mac");

	memset(bt_mac, 0, sizeof(bt_mac));
	memset(wifi_mac, 0, sizeof(wifi_mac));

	sleep(6);//wait for modem up
#ifdef MAC_FROM_ANDROID
	mac_ok = read_btwifimac_from_database(bt_mac, wifi_mac);
#else
	mac_ok = read_btwifimac_from_modem(bt_mac, wifi_mac);
#endif

	if(mac_ok==0)
		mac_rand(bt_mac, wifi_mac);
	else
		write_to_randmacfile(bt_mac, wifi_mac);

	ALOGD("property ro.mac.wifi=%s, ro.mac.bluetooth=%s",wifi_mac,bt_mac);
	write_mac2file(wifi_mac,bt_mac);

	property_set("sys.mac.wifi" ,wifi_mac);
	property_set("sys.mac.bluetooth",bt_mac);
	property_set("sys.bt.bdaddr_path",BT_MAC_FILE);
	property_set("ctl.start", "set_mac");

	return 0;
}
