#ifndef __WCND_H__
#define __WCND_H__

#define WCND_DEBUG
#define WCND_WIFI_CONFIG_FILE_PATH "/data/misc/wifi/wifimac.txt"
#define WCND_WIFI_FACTORY_CONFIG_FILE_PATH "/productinfo/wifimac.txt"
#define WCND_BT_CONFIG_FILE_PATH "/data/misc/bluedroid/btmac.txt"
#define WCND_BT_FACTORY_CONFIG_FILE_PATH "/productinfo/btmac.txt"
#define MAC_LEN 6

#ifdef WCND_DEBUG
#define WCND_LOGD(x...) ALOGD( x )
#define WCND_LOGE(x...) ALOGE( x )
#else
#define WCND_LOGD(x...) do {} while(0)
#define WCND_LOGE(x...) do {} while(0)
#endif

//the CP2(WIFI/BT/FM) device node
#define WCN_DEV				"/dev/cpwcn"

//when what to read is that has been to write, then it indicate the CP2(WIFI/BT/FM) is OK
#define WCN_LOOP_IFACE		"/dev/spipe_wcn0"

//when assert happens, related information will write to this interface
#define WCN_ASSERT_IFACE		"/dev/spipe_wcn2"

//The interface to communicate with slog
#define WCN_SLOG_IFACE		"/dev/slog_wcn"

//write '1' to this interface, the CP2(WIFI/BT/FM) will stop
#define WCN_STOP_IFACE		"/proc/cpwcn/stop"

//write '1' to this interface, the CP2(WIFI/BT/FM) will start to boot up
#define WCN_START_IFACE		"/proc/cpwcn/start"

//write image file to this interface, then the image will be download to CP2(WIFI/BT/FM)
#define WCN_DOWNLOAD_IFACE	"/proc/cpwcn/modem"

//read from this interface, the memory infor will be read from CP2(WIFI/BT/FM)
#define WCN_DUMP_IFACE		"/proc/cpwcn/mem"

//when an exception of watdog happens, related infor will write to this interface
#define WCN_WATCHDOG_IFACE	"/proc/cpwcn/wdtirq"

//can read the status infor from this interface
#define WCN_STATUS_IFACE		"/proc/cpwcn/status"


//send at cmd to this interface
#define WCN_ATCMD_IFACE		"/dev/spipe_wcn5"



//the key used by property_get() to get partition path
#define PARTITION_PATH_PROP_KEY	"ro.product.partitionpath"

#define WCND_MODEM_ENABLE_PROP_KEY "ro.modem.wcn.enable"

#define WCND_RESET_PROP_KEY "persist.sys.sprd.wcnreset"
#define WCND_SLOG_ENABLE_PROP_KEY "persist.sys.sprd.wcnlog"
#define WCND_SLOG_RESULT_PROP_KEY "persist.sys.sprd.wcnlog.result"
#define WCND_ENGCTRL_PROP_KEY "persist.sys.engpc.disable"


//the image file
#define WCN_IMAGE_NAME	"wcnmodem"

#define WCN_IMAGE_SIZE	(10*1024*1024)

#define WCND_SOCKET_NAME	"wcnd"


#define WCND_CP2_EXCEPTION_STRING "WCN-CP2-EXCEPTION"
#define WCND_CP2_RESET_START_STRING "WCN-CP2-RESET-START"
#define WCND_CP2_RESET_END_STRING "WCN-CP2-RESET-END"
#define WCND_CP2_ALIVE_STRING "WCN-CP2-ALIVE"

typedef int (*cmd_handler)(int client_fd, int argc, char* argv[]);

typedef struct structWcnCmdExecuter{
	char* name;
	int (*runcommand)(int client_fd, int argc, char* argv[]);
}WcnCmdExecuter;

#define WCND_MAX_CLIENT_NUM	(10)

#define WCND_MAX_IFACE_NAME_SIZE		(32)

#define WCND_MAX_CMD_EXECUTER_NUM		(10)

typedef struct structWcndManager
{
	//identify if the struct is initialized or not.
	int inited;

	pthread_mutex_t client_fds_lock;

	//to store the sockets that connect from the clients
	int client_fds[WCND_MAX_CLIENT_NUM];

	//the server socket to listen for client to connect
	int listen_fd;

	char wcn_assert_iface_name[WCND_MAX_IFACE_NAME_SIZE];
	char wcn_loop_iface_name[WCND_MAX_IFACE_NAME_SIZE];
	char wcn_watchdog_iface_name[WCND_MAX_IFACE_NAME_SIZE];

	char wcn_stop_iface_name[WCND_MAX_IFACE_NAME_SIZE];
	char wcn_start_iface_name[WCND_MAX_IFACE_NAME_SIZE];
	char wcn_download_iface_name[WCND_MAX_IFACE_NAME_SIZE];
	char wcn_image_file_name[WCND_MAX_IFACE_NAME_SIZE];
	char wcn_atcmd_iface_name[WCND_MAX_IFACE_NAME_SIZE];

	int wcn_image_file_size;

	/*
	* count the notify information send, at the same time use as
	* a notify_id of the notify information send every time.
	* In order to identy the reponse of the notify information from
	* client, that means that the client must send back the notity_id in
	* the reponse of the notify information.
	*/
	int notify_count;

	pthread_mutex_t cmdexecuter_list_lock;

	/**
	* Cmd executer to execute the cmd send from clients
	*/
	const WcnCmdExecuter* cmdexecuter_list[WCND_MAX_CMD_EXECUTER_NUM];

	//to identify if a reset process is going on
	int doing_reset;

}WcndManager;

//export API
WcndManager* wcnd_get_default_manager(void);
int wcnd_register_cmdexecuter(WcndManager *pWcndManger, const WcnCmdExecuter *pCmdExecuter);
int wcnd_send_back_cmd_result(int client_fd, char *str, int isOK);



#endif
