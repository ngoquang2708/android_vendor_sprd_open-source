#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include "stdlib.h"
#include <errno.h>
#include <sys/statfs.h>
#include <pthread.h>

#include "engservice.h"
#include "engopt.h"
#include "fdevent.h"

#include "cutils/sockets.h"
#include "cutils/properties.h"

#define ENG_MAX_RW_SIZE            (2048-20)

#define ENG_MAX_CONNECT_NUM       60
#define ENG_MAX_CLIENT_NAME_LEN   20
struct eng_client_info{
	char name[ENG_MAX_CLIENT_NAME_LEN];
	int type;
	fdevent* fe;
	//struct list_head	list;		/* */
};

struct eng_client_info client_info[ENG_MAX_CONNECT_NUM];
static pthread_mutex_t gEngRegMutex = PTHREAD_MUTEX_INITIALIZER;
static int monitor_svc_fd = -1;
static char eng_read_buf[ENG_MAX_RW_SIZE] = {0};
static char eng_write_buf[ENG_MAX_RW_SIZE] = {0};

static void pcclient_event_func(int _fd, unsigned ev, void *_l);
static void modemclient_event_func(int _fd, unsigned ev, void *_l);
static fd_func getclientfunc(int fd, char* regname);
#if 0
/*copy from socket_loopback_server*/
/* open listen() port on loopback interface */
int eng_server(int port, int type)
{
	struct sockaddr_in addr;
	size_t alen;
	int s, n;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	s = socket(AF_INET, type, 0);
	if(s < 0) return -1;
	
	n = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));

	if(bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
	    close(s);
	    return -1;
	}
	
	if (type == SOCK_STREAM) {
	    int ret;

	    ret = listen(s, ENG_MAX_CONNECT_NUM);

	    if (ret < 0) {
	        close(s);
	        return -1; 
	    }
	}
	
	return s;
}
#endif

int eng_server(int port, int type)
{
    int ret;
    ret = socket_local_server ("engineeringmodel", 
                0, SOCK_STREAM);

    if (ret < 0) {
         ALOGD("eng_server Unable to bind socket errno:%d", errno);
         exit (-1);
    }
    return ret;
}
static void client_info_init(void)
{
	memset(client_info,0,sizeof(struct eng_client_info)*ENG_MAX_CONNECT_NUM);
}

static int clinet_info_getnametype(char *input, char *name)
{
	char *ptr;
	int type;

	ENG_LOG("%s: input=%s\n",__func__, input);
	
	ptr = strchr(input, ':');
	if(ptr == NULL){ //No type parameter
		strcpy(name, input);
		type = 0;
	} else {
		strncpy(name, input, strlen(input)-2);
		type = atoi(ptr+1);
		
	}

	ENG_LOG("%s: name=%s; type=%d\n",__func__, name, type);

	return type;
}
static void client_info_update(fdevent* fe,char* name, int type)
{
	int i;
	for (i=0; i<ENG_MAX_CONNECT_NUM; i++) {
		if (NULL == client_info[i].fe){
			client_info[i].fe = fe;
			client_info[i].type = type;
			memcpy(client_info[i].name, name, strlen(name));
			ENG_LOG("%s: name[%d] %s,\n",__func__, i, client_info[i].name);
			break;
		}
	}
}
static int client_info_gettype(int fd)
{
	int i;
	for (i=0; i<ENG_MAX_CONNECT_NUM; i++) {
		ENG_LOG("%s:[%d] fd=%d;\n",__func__, i, fd);
		if (client_info[i].fe != NULL){
			if (fd == client_info[i].fe->fd) {
				ENG_LOG("%s: [%d].name=%s; .type=%d\n",__func__, i, client_info[i].name, client_info[i].type);
				return client_info[i].type ;	
			}
		}
	}
	return 0;
}

static int client_info_get_socketid(char* name)
{
	int i;
	for (i=0; i<ENG_MAX_CONNECT_NUM; i++) {
		if (0 == memcmp(client_info[i].name, name, strlen(name))){
			ENG_LOG("%s: [%d].name=%s; .fd=%d\n",__func__, i, client_info[i].name, client_info[i].fe->fd);
			return client_info[i].fe->fd ;			
		}
	}
	return -1;
}

static fdevent* client_info_get_sockefd(int fd)
{
	int i;
	for (i=0; i<ENG_MAX_CONNECT_NUM; i++) {
		if (client_info[i].fe->fd == fd){
			return client_info[i].fe ;
		}
	}
	return NULL;
}

static int eng_event_can_reg(void)
{
    int i = 0, ret = 0;

    ENG_LOG("%s enter",__func__);
    for (i = 0; i < ENG_MAX_CONNECT_NUM; i ++) {
        if (NULL == client_info[i].fe) {
            ret = 1;
            break;
        }
    }
    ENG_LOG("%s ret = %d", __func__, ret);
    return ret;
}

static int eng_event_unreg(int fd)
{
	int i;

	ENG_LOG("%s: fd=%d",__func__, fd);
        pthread_mutex_lock(&gEngRegMutex);
	for (i=0; i<ENG_MAX_CONNECT_NUM; i++) {

		if(client_info[i].fe != NULL) {
			ENG_LOG("%s: client_info[%d].fe->fd=%d",__func__, i, client_info[i].fe->fd);
		}else{
			ENG_LOG("%s: [%d].fe=NULL",__func__, i);
		}
			
		
		if( client_info[i].fe != NULL &&  client_info[i].fe->fd == fd)
		{
			ENG_LOG("engserver eng_event_unreg delete client %s\n", client_info[i].name);
			fdevent_del(client_info[i].fe,FDE_READ);
			//fdevent_del(client_info[i].fe,FDE_WRITE);
 			fdevent_destroy(client_info[i].fe);
			free(client_info[i].fe);
			memset(client_info[i].name, 0, ENG_MAX_CLIENT_NAME_LEN);
			client_info[i].type=0;
			client_info[i].fe = NULL;
			
			break;
		}
	}
	close(fd);
        pthread_mutex_unlock(&gEngRegMutex);
	if(i>=ENG_MAX_CONNECT_NUM)
		return -1;
	
	return 0;
}

static int eng_event_reg(int fd)
{
	int type;
        int ret = 0;
	char regname[ENG_MAX_CLIENT_NAME_LEN];
        char engname[ENG_MAX_CONNECT_NUM];
	fdevent* fe = NULL;
	fd_func func = pcclient_event_func;
	
	ENG_LOG("%s: fd=%d",__func__, fd);
        pthread_mutex_lock(&gEngRegMutex);
    if (0 == eng_event_can_reg()) {
        ENG_LOG("engserver eng_event_reg reached max num\n");
        pthread_mutex_unlock(&gEngRegMutex);
        return -1;
    }
         memset(engname, 0, ENG_MAX_CONNECT_NUM);
         ret = eng_read(fd, engname, ENG_MAX_CONNECT_NUM);
         if(-1 == ret)
         {
		ENG_LOG("engserver eng_event_reg read error\n");
		eng_close(fd);
                pthread_mutex_unlock(&gEngRegMutex);
		return -1;
	 }
         if(0 == ret)
         {
                ENG_LOG("engserver eng_event_reg read no data\n");
                eng_close(fd);
                pthread_mutex_unlock(&gEngRegMutex);
                return -1;
         }


	memset(regname, 0, ENG_MAX_CLIENT_NAME_LEN);
        type = clinet_info_getnametype(engname, regname);

	if (monitor_svc_fd < 0
			&& ( strncmp((const char*)regname,ENG_MONITOR,strlen(ENG_MONITOR)) != 0)){
		eng_write(fd,ENG_DESTERROR,strlen(ENG_DESTERROR));
		close(fd);
                pthread_mutex_unlock(&gEngRegMutex);
		return -1;
	}

	func = getclientfunc(fd, regname);
	if (func==NULL){
		ALOGD("engserver eng_event_reg get client function error\n");
		eng_close(fd);
                pthread_mutex_unlock(&gEngRegMutex);
		return -1;
	}

	ENG_LOG("%s: fdevent_create\n",__func__);
	if (NULL != (fe = fdevent_create(fd,func,NULL)))
	{
		ENG_LOG("%s: fe.fd=%d",__func__, fe->fd);
		fdevent_add(fe,FDE_READ);
		//event_add(fe,FDE_WRITE);
		client_info_update(fe,(char*)regname, type);
		ENG_LOG("%s: send %s\n",__func__, ENG_WELCOME);
		eng_write(fd,ENG_WELCOME,strlen(ENG_WELCOME));
	}
	else
	{
		eng_write(fd,ENG_ERROR,strlen(ENG_ERROR));
                pthread_mutex_unlock(&gEngRegMutex);
		return -1;
	}
        pthread_mutex_unlock(&gEngRegMutex);
	return 0;
}

static void setup_send_data(int fd, int type, char *inbuf, char *outbuf)
{
	/*fd max=43892 /proc/sys/fs/file-max */
	sprintf(outbuf, "%5d;%5d;%d;%s",6+6+2+strlen(inbuf), fd, type, inbuf);
	ENG_LOG("%s:fd=%d; type=%d; inbuf=%s; outbuf=%s", __func__, fd, type, inbuf, outbuf);
}

static int get_send_fd(char *inbuf, char *outbuf)
{
        int fd, length;
        char fdstring[8];
        char *ptr = NULL;
        ENG_LOG("%s: inbuf=%s",__func__, inbuf);

        ptr = strchr(inbuf, ';');
        if (NULL == ptr) {
            ENG_LOG("%s: failed! return 0",__func__);
            return 0;
        }
        sprintf(outbuf, "%s",ptr+1);

        memset(fdstring, 0, sizeof(fdstring));
        length = ptr-inbuf;
        *ptr='\0';
        sprintf(fdstring, "%s", inbuf);
        fd = atoi(fdstring);

	ENG_LOG("%s: fdstring=%s; outbuf=%s; fd=%d, length=%d",__func__, fdstring, outbuf, fd, length);
	return fd;
}

static void monitorclient_event_func(int _fd, unsigned ev, void *_l)
{
	int ret = 0;
	unsigned char mbuf[ENG_MAX_RW_SIZE] = {0};
	if (ev & FDE_READ) {
		ret =eng_read(_fd, mbuf, ENG_MAX_RW_SIZE);	
		if(ret==0) {
 			eng_event_unreg(_fd);
		}
	}
	else if (ev& FDE_WRITE){
	}
	else	{
		ALOGD("monitorclient_event_func _fd=0x%x error\n",_fd);
	}
}
//type 0:sms 1:mms
static int df(char *s,int type) {
    char buf[100];
    struct statfs st;
    long long total_len = 0;
    long long avail_len = 0;
    long long left_len = 0;
    long long len = 0;

    if (statfs(s, &st) < 0) {
        fprintf(stderr, "%s: %s\n", s, strerror(errno));
	return -1;
    } else {
        if (st.f_blocks == 0)
            return -1;

	if(0==strncmp(s,"/data",sizeof("/data"))){
	       total_len = ((long long)st.f_blocks * (long long)st.f_bsize) / 1024;
               avail_len = ((long long)st.f_bfree * (long long)st.f_bsize) / 1024;

		left_len = type?(total_len/10+400):(total_len/20);
	       	len = avail_len - left_len;
		ENG_LOG("Avail = %lld,left %lld,fill len %lld",avail_len,left_len,len);
		if ( len > 0 ) {
			memset(buf,0,sizeof(buf));
			sprintf(buf,"dd if=/dev/zero of=/data/data/stuffing bs=1024 count=%lld",len);
			system(buf);
			return 0;
		}
	}
    }

	return -1;
}

static void df_main(int type) {
	int ret=-1;
        char s[2000];
    	struct statfs st;

        FILE *f = fopen("/proc/mounts", "r");

        while (fgets(s, 2000, f)) {
            char *c, *e = s;

            for (c = s; *c; c++) {
                if (*c == ' ') {
                    e = c + 1;
                    break;
                }
            }

            for (c = e; *c; c++) {
                if (*c == ' ') {
                    *c = '\0';
                    break;
                }
            }

            ret = df(e,type);
	    if ( 0 == ret ) {
		    break;
	    }
        }

        fclose(f);
}


static void eng_test_function(char* buf)
{
	//do wifi test
	if ( 0 == strncmp (buf,"WIFI",strlen("WIFI"))){
	/*	system("killall -s 9 synergy_service");
		system("rmmod unifi_sdio");
		sleep(2);
		system("insmod /system/lib/modules/unifi_sdio.ko");
		sleep(2);
		system("synergy_service -p ptest &");*/
	}else if( 0 == strncmp (buf,"STOP",strlen("STOP")) ){
	/*	ENG_LOG("=============CMD:STOP");
		system("killall -s 9 synergy_service");
		system("synergy_service &");
		system("rmmod unifi_sdio");
		sleep(2);*/
	}else if( 0 == strncmp (buf,"SMS MEM START",strlen("SMS MEM START")) ){
		ENG_LOG("SMS DF BEGIN");
		df_main(0);
		system("sync");
		ENG_LOG("SMS DF END");
	}else if( 0 == strncmp (buf,"MMS MEM START",strlen("MMS MEM START")) ){
		ENG_LOG("MMS DF BEGIN");
		df_main(1);
		system("sync");
		ENG_LOG("MMS DF END");
	}else if( 0 == strncmp (buf,"MEM STOP",strlen("MEM STOP")) ){
		ENG_LOG("MEM RM");
		system("rm -f /data/data/stuffing");
		system("sync");
	}else if( 0 == strncmp (buf,"CMCC START",strlen("CMCC START")) ){
		ENG_LOG("CMCC START");
		//LOGV("run anritsu_init.sh");
		//system("anritsu_init.sh >> 1.txt");
		
	}else if( 0 == strncmp (buf,"NONANRITSU START",strlen("NONANRITSU START")) ){
		ENG_LOG("CMCC  NONANRITSU START");
		//LOGV("run non_anritsu_init.sh");
		//system("non_anritsu_init.sh >> 2.txt");
	}else if( 0 == strncmp (buf,"CMCC STOP",strlen("CMCC STOP")) ){
		ENG_LOG("CMCC STOP");
		//ENG_LOG("rmmod unifi_sdio");
		//system("rmmod unifi_sdio");
		//sleep(2);
	}else if( 0 == strncmp (buf,"CMCC TEST",strlen("CMCC TEST")) ){
		ENG_LOG("CMCC TEST");
		ALOGD("wl roam_off 1");
		system("wl roam_off 1");
		ALOGD("wl PM 0");
		system("wl PM 0");
		//system("iwconfig wlan0 power off");
	}else if( 0 == strncmp (buf,"SET MAX POWER",strlen("SET MAX POWER")) ){
		ENG_LOG("SET MAX POWER");
		ALOGD("iwnpi wlan0 set_tx_power 1127");
		system("iwnpi wlan0 set_tx_power 1127");
		ALOGD("iwnpi wlan0 set_tx_power 127");
		system("iwnpi wlan0 set_tx_power 127");
	}else if( 0 == strncmp (buf,"UNSET MAX POWER",strlen("UNSET MAX POWER")) ){
		ENG_LOG("UNSET MAX POWER");
		ALOGD("iwnpi wlan0 set_tx_power 1088");
		system("iwnpi wlan0 set_tx_power 1088");
		ALOGD("iwnpi wlan0 set_tx_power 72");
		system("iwnpi wlan0 set_tx_power 72");
	}
}
static void  appclient_event_func(int _fd, unsigned ev, void *_l)
{
	int ret;
	int type = client_info_gettype(_fd);
	int dest_fd = client_info_get_socketid(ENG_MODEM);
	ENG_LOG("%s: type=%d; dest_fd=%d\n",__func__, type, dest_fd);
	if (ev & FDE_READ) {
		memset(eng_read_buf, 0, ENG_MAX_RW_SIZE);
		ret=eng_read(_fd, eng_read_buf, ENG_MAX_RW_SIZE);
		ENG_LOG("%s: _fd=0x%x read write to dest_id =0x%x, read length = %d\n", \
			__func__, _fd,dest_fd,ret);

		if(ret==0) {
			//client disconnect
			eng_event_unreg(_fd);
		} else if(dest_fd>=0) {
			//data parse
			memset(eng_write_buf, 0, ENG_MAX_RW_SIZE);
			if (0 == strncmp(eng_read_buf,"CMD:",strlen("CMD:"))){
				eng_test_function(eng_read_buf+strlen("CMD:"));
			}
			else {
				setup_send_data(_fd, type, eng_read_buf, eng_write_buf);
				eng_write(dest_fd, eng_write_buf, strlen(eng_write_buf));
			}
		} else {
			//tell sender that the desitnation socket closed
			memset(eng_write_buf, 0, ENG_MAX_RW_SIZE);
			strcpy(eng_write_buf, ENG_DESTERROR);
			eng_write(_fd, eng_write_buf, strlen(eng_write_buf));
		}
	}
	else if (ev& FDE_WRITE){
		ENG_LOG("%s: FDE_WRITE\n",__func__);
		//eng_write(dest_id, eng_write_buf, ENG_MAX_RW_SIZE);
		//g_write(_fd,ENG_WELCOME,strlen(ENG_WELCOME));
		//event_del(client_info_get_sockefd(_fd),FDE_WRITE);
	}
	else	{
		ALOGD("appclient_event_func _fd=0x%x error\n",_fd);
	}	
}

static void modemclient_event_func(int _fd, unsigned ev, void *_l)
{
	int ret, length;
	int dest_fd; 
 	if (ev & FDE_READ) {
		memset(eng_read_buf, 0, ENG_MAX_RW_SIZE);
		ret = eng_read(_fd, eng_read_buf, ENG_MAX_RW_SIZE);		
		ENG_LOG("%s: _fd=0x%x read write to read length=%d\n",__func__, _fd,ret);
		
		memset(eng_write_buf, 0, ENG_MAX_RW_SIZE);
		dest_fd = get_send_fd(eng_read_buf, eng_write_buf);
	
		if(ret==0) {
			//client disconnect
 			eng_event_unreg(_fd);
 		} else if(dest_fd>=0){
			//data parse
			length = strlen(eng_write_buf);
			ENG_LOG("%s: write %s to %d, lenght=%d",__func__, eng_write_buf, dest_fd, length);
			eng_write(dest_fd, eng_write_buf, length);
 		} else {
 			ALOGD("modemclient_event_func dest socket fd error!\n");
			//tell sender that the desitnation socket closed
			memset(eng_write_buf, 0, ENG_MAX_RW_SIZE);
			strcpy(eng_write_buf, ENG_DESTERROR);
			eng_write(_fd, eng_write_buf, strlen(eng_write_buf));
 		}
	}
	else if (ev& FDE_WRITE){
		//eng_write(dest_id, eng_write_buf, ENG_MAX_RW_SIZE);
		//g_write(_fd,ENG_WELCOME,strlen(ENG_WELCOME));	
		//event_del(client_info_get_sockefd(_fd),FDE_WRITE);
	}
	else	{
		ALOGD("modemclient_event_func _fd=0x%x error\n",_fd);
	}
 }

static void pcclient_event_func(int _fd, unsigned ev, void *_l)
{
	if (ev & FDE_READ) {
		eng_read(_fd, eng_read_buf, ENG_MAX_RW_SIZE);		
		ENG_LOG("pcclient_event_func _fd=0x%x read\n",_fd);
	}
	else if (ev& FDE_WRITE){
		//eng_write(dest_id, eng_write_buf, ENG_MAX_RW_SIZE);
		ENG_LOG("pcclient_event_func _fd=0x%x write\n",_fd);		
	}
	else	{
		ENG_LOG("pcclient_event_func _fd=0x%x\n",_fd);
	}
}

static void adrvclient_event_func(int _fd, unsigned ev, void *_l)
{
	if (ev & FDE_READ) {
		eng_read(_fd, eng_read_buf, ENG_MAX_RW_SIZE);		
		ENG_LOG("adrvclient_event_func _fd=0x%x read\n",_fd);
	}
	else if (ev& FDE_WRITE){
		//eng_write(dest_id, eng_write_buf, ENG_MAX_RW_SIZE);
		ENG_LOG("adrvclient_event_func _fd=0x%x write\n",_fd);		
	}
	else	{
		ENG_LOG("adrvclient_event_func _fd=0x%x\n",_fd);
	}
}

static void logclient_event_func(int _fd, unsigned ev, void *_l)
{
	if (ev & FDE_READ) {
		eng_read(_fd, eng_read_buf, ENG_MAX_RW_SIZE);		
		ENG_LOG("logclient_event_func _fd=0x%x read\n",_fd);
	}
	else if (ev& FDE_WRITE){
		//eng_write(dest_id, eng_write_buf, ENG_MAX_RW_SIZE);
		ENG_LOG("logclient_event_func _fd=0x%x write\n",_fd);		
	}
	else	{
		ENG_LOG("logclient_event_func _fd=0x%x\n",_fd);
	}
}

static fd_func getclientfunc(int fd, char *regname)
{
	fd_func func = NULL;
	ENG_LOG("%s: %s\n",__func__, regname);
	if ( strncmp((const char*)regname,ENG_PCCLIENT,strlen(ENG_PCCLIENT)) == 0){
		func = pcclient_event_func;
	}
	else if ( strncmp((const char*)regname,ENG_MODEM,strlen(ENG_MODEM)) == 0){
		func = modemclient_event_func;
	}
	else if ( strncmp((const char*)regname,ENG_APPCLIENT,strlen(ENG_APPCLIENT)) == 0)	{
		func = appclient_event_func;
	}
	else if ( strncmp((const char*)regname,ENG_ADRV,strlen(ENG_ADRV)) == 0){
		func = adrvclient_event_func;
	}		
	else if ( strncmp((const char*)regname,ENG_LOGECLIENT,strlen(ENG_LOGECLIENT)) == 0){
		func = logclient_event_func;
	}
	else if ( strncmp((const char*)regname,ENG_MONITOR,strlen(ENG_MONITOR)) == 0){
		func = monitorclient_event_func;
		monitor_svc_fd = fd;
	}	
	else	{
		ALOGD("getclientfunc error!!!");
	}
	return func;
}

void *eng_svc_thread(void *x)
{
	fdevent_loop();
	return 0;
}
	
int main (int argc, char** argv)
{
	int s,n,ret;
	struct sockaddr addr;
	socklen_t alen;
	pid_t pid;
	eng_thread_t t;
	int has_thread = 0;
	int type;
	int opt;
	char name[10];
#if 0
	umask(0);

	/*
	* Become a session leader to lose controlling TTY.
	*/
	if ((pid = fork()) < 0)
		ALOGD("engservice can't fork");
	else if (pid != 0) /* parent */
		exit(0);
	setsid();

	if (chdir("/") < 0)
		ALOGD("can't change directory to /");
#endif

	while ( -1 != (opt = getopt(argc, argv, "t:"))) {
		switch (opt) {
			case 't':
				memset(name,0,10);
				type = atoi(optarg);
				if (type){
					strcpy(name,"engtd");
				} else {
					strcpy(name,"engw");
				}
				break;
			default:
				exit(EXIT_FAILURE);
		}
	}
	ENG_LOG("%s servername=%s",__func__,name);
	s = android_get_control_socket(name);
	//s = eng_server(ENG_SOCKET_PORT,SOCK_STREAM);
	if (s == -1)
	{
		ENG_LOG("Failed to get control socket %s",strerror(errno));
		exit(-1);
	}

	ret = listen(s, 4);
	if (ret < 0) {
		ENG_LOG("Failed to listen on control socket '%d': %s",s, strerror(errno));
		exit(-1);
	}

	ENG_LOG("engserver %s start listen\n",name);

	client_info_init();

	alen = sizeof(addr);
	for (; ;)
	{
		if ( (n=accept(s,&addr,&alen)) == -1)
		{			
			ALOGD("engserver accept error\n");
			continue;
		}

		if ( eng_event_reg(n) < 0 )
			continue;

		if (0 == has_thread){
			if (0 != eng_thread_create( &t, eng_svc_thread, (void *)n)){
				ALOGD("engserver thread create error\n");
			}
		}
		else{
			eng_write(monitor_svc_fd,"client connect",strlen("client connect"));
		}		

		has_thread = 1;
	}
	
	return 0;
}
