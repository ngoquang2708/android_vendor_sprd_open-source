#define LOG_TAG 	"WCND"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cutils/sockets.h>
#include <ctype.h>
#include <pthread.h>
#include <errno.h>
#include <cutils/properties.h>
#include <utils/Log.h>
#include <signal.h>

#include "wcnd.h"
#include "wcnd_sm.h"

#define CP2_RESET_READY

//Macro to control if polling cp2 assert/watdog interface
#define CP2_WATCHER_ENABLE

//for unit test
//#define FOR_UNIT_TEST

//Macro to control if polling cp2 loop interface every 5 seconds
#define LOOP_CHECK

//Macro to enable the Wifi Engineer Mode
#define WIFI_ENGINEER_ENABLE

bool is_zero_ether_addr(const unsigned char *mac)
{
	return !(mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]);
}

long get_seed()
{
	struct timeval t;
	unsigned long seed = 0;
	gettimeofday(&t, NULL);
	seed = 1000000 * t.tv_sec + t.tv_usec;
	WCND_LOGD("generate seed: %lu", seed);
	return seed;
}

/* This function is for internal test only */
void get_random_mac(unsigned char *mac)
{
	int i;

	WCND_LOGD("generate random mac");
	memset(mac, 0, MAC_LEN);

	srand(get_seed()); /* machine run time in us */
	for(i=0; i<MAC_LEN; i++) {
		mac[i] = rand() & 0xFF;
	}

	mac[0] &= 0xFE; /* clear multicast bit */
	mac[0] |= 0x02; /* set local assignment bit (IEEE802) */
}

void read_mac_from_file(const char *file_path, unsigned char *mac)
{
	FILE *f;
	unsigned char mac_src[MAC_LEN];
	char buf[20];

	f = fopen(file_path, "r");
	if (f == NULL) return;

	if (fscanf(f, "%02x:%02x:%02x:%02x:%02x:%02x", &mac_src[0], &mac_src[1], &mac_src[2], &mac_src[3], &mac_src[4], &mac_src[5]) == 6) {
		memcpy(mac, mac_src, MAC_LEN);
		sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x", mac_src[0], mac_src[1], mac_src[2], mac_src[3], mac_src[4], mac_src[5]);
		WCND_LOGD("mac from configuration file: %s", buf);
	} else {
		memset(mac, 0, MAC_LEN);
	}

	fclose(f);
}

void write_mac_to_file(const char *file_path, const unsigned char *mac)
{
	FILE *f;
	unsigned char mac_src[MAC_LEN];
	char buf[100];

	f = fopen(file_path, "w");
	if (f == NULL) return;

	sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	fputs(buf, f);
	WCND_LOGD("write mac to configuration file: %s", buf);

	fclose(f);

	sprintf(buf, "chmod 666 %s", file_path);
	system(buf);
}

bool is_file_exists(const char *file_path)
{
	return access(file_path, 0) == 0;
}

void force_replace_file(const char *dst_file_path, const char *src_file_path)
{
	FILE *f_src, *f_dst;
	char buf[100];

	f_src = fopen(src_file_path, "r");
	if (f_src == NULL) return;
	fgets(buf, sizeof(buf), f_src);
	fclose(f_src);
	
	f_dst = fopen(dst_file_path, "w");
	if (f_dst == NULL) return;
	fputs(buf, f_dst);
	fclose(f_dst);

	sprintf(buf, "chmod 666 %s", dst_file_path);
	system(buf);
	WCND_LOGD("force_replace_config_file: %s", buf);
}

void generate_wifi_mac()
{
	unsigned char mac[MAC_LEN];
	// force replace configuration file if vaild mac is in factory configuration file
	if(is_file_exists(WCND_WIFI_FACTORY_CONFIG_FILE_PATH)) {
		WCND_LOGD("wifi factory configuration file exists");
		read_mac_from_file(WCND_WIFI_FACTORY_CONFIG_FILE_PATH, mac);
		if(!is_zero_ether_addr(mac)) {
			force_replace_file(WCND_WIFI_CONFIG_FILE_PATH, WCND_WIFI_FACTORY_CONFIG_FILE_PATH);
			return;
		}
	}
	// if vaild mac is in configuration file, use it
	if(is_file_exists(WCND_WIFI_CONFIG_FILE_PATH)) {
		WCND_LOGD("wifi configuration file exists");
		read_mac_from_file(WCND_WIFI_CONFIG_FILE_PATH, mac);
		if(!is_zero_ether_addr(mac)) return;
	}
	// generate random mac and write to configuration file
	get_random_mac(mac);
	write_mac_to_file(WCND_WIFI_CONFIG_FILE_PATH, mac);
}

void generate_bt_mac()
{
	unsigned char mac[MAC_LEN];
	// force replace configuration file if vaild mac is in factory configuration file
	if(is_file_exists(WCND_BT_FACTORY_CONFIG_FILE_PATH)) {
		WCND_LOGD("bt factory configuration file exists");
		read_mac_from_file(WCND_BT_FACTORY_CONFIG_FILE_PATH, mac);
		if(!is_zero_ether_addr(mac)) {
			force_replace_file(WCND_BT_CONFIG_FILE_PATH, WCND_BT_FACTORY_CONFIG_FILE_PATH);
			return;
		}
	}
	// if vaild mac is in configuration file, use it
	if(is_file_exists(WCND_BT_CONFIG_FILE_PATH)) {
		WCND_LOGD("bt configuration file exists");
		read_mac_from_file(WCND_BT_CONFIG_FILE_PATH, mac);
		if(!is_zero_ether_addr(mac)) return;
	}
	// generate random mac and write to configuration file
	get_random_mac(mac);
	mac[0] = 0;
	write_mac_to_file(WCND_BT_CONFIG_FILE_PATH, mac);
}




//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

/**
* pre-define static API.
*/
static int send_back_cmd_result(int client_fd, char *str, int isOK);

/**
* static variables
*/
static WcndManager default_wcn_manager;

/*
* wcn cmd executer to executer cmd that relate to CP2 assert
* such as:
* (1) reset  //to reset the CP2
*/
static const WcnCmdExecuter wcn_cmdexecuter = {
	.name = "wcn",
	.runcommand = wcnd_runcommand,
};

/**
* export API
*/

WcndManager* wcnd_get_default_manager(void)
{
	return &default_wcn_manager;
}

int wcnd_register_cmdexecuter(WcndManager *pWcndManger, const WcnCmdExecuter *pCmdExecuter)
{
	if(!pWcndManger || !pCmdExecuter) return -1;

	if(!pWcndManger->inited)
	{
		WCND_LOGE("WcndManager IS NOT INITIALIZED ! ");
		return -1;
	}

	int i = 0;
	int ret = 0;
	pthread_mutex_lock(&pWcndManger->cmdexecuter_list_lock);

	for (i = 0; i < WCND_MAX_CMD_EXECUTER_NUM; i++)
	{
		if(!pWcndManger->cmdexecuter_list[i]) //empty
		{
			pWcndManger->cmdexecuter_list[i] = pCmdExecuter;
			break;
		}
		else if(pWcndManger->cmdexecuter_list[i] == pCmdExecuter)
		{
			WCND_LOGD("cmd executer:%p has been register before!", pCmdExecuter);
			break;
		}
	}

	pthread_mutex_unlock(&pWcndManger->cmdexecuter_list_lock);

	if( WCND_MAX_CMD_EXECUTER_NUM == i)
	{
		WCND_LOGE("ERRORR::%s: cmdexecuter_list is FULL", __FUNCTION__);
		ret = -1;
	}

	return ret;
}

int wcnd_send_back_cmd_result(int client_fd, char *str, int isOK)
{
	return send_back_cmd_result(client_fd, str, isOK);
}

int wcnd_send_selfcmd(WcndManager *pWcndManger, char *cmd)
{
        return TEMP_FAILURE_RETRY(write(pWcndManger->selfcmd_sockets[0], cmd, strlen(cmd)));
}


/**
* static API
*/
#define OK_STR "OK"
#define FAIL_STR "FAIL"

static int send_back_cmd_result(int client_fd, char *str, int isOK)
{
	char buffer[255];

	if(client_fd < 0) return -1;

	memset(buffer, 0, sizeof(buffer));

	if(!str)
	{
		snprintf(buffer, 255, "%s",  (isOK?OK_STR:FAIL_STR));
	}
	else
	{
		snprintf(buffer, 255, "%s %s", (isOK?OK_STR:FAIL_STR), str);
	}

	int ret = write(client_fd, buffer, strlen(buffer)+1);
	if(ret < 0)
	{
		WCND_LOGE("write %s to client_fd:%d fail (error:%s)", buffer, client_fd, strerror(errno));
		return -1;
	}

	return 0;
}

/**
* cmd format:
* module_name [submodule_name cmd | cmd]  arg1 arg2 ...
* eg:
* wcn reset
* eng iwnapi getmaxpower
* Note: data must end with null character.
*/
static void dispatch_command2(WcndManager *pWcndManger, int client_fd, char *data)
{
	if(!pWcndManger || !data)
	{
		send_back_cmd_result(client_fd, "Null pointer!!", 0);
		return;
	}

	int i, j = 0;
	int argc = 0;

#define WCND_CMD_ARGS_MAX (16)

	char *argv[WCND_CMD_ARGS_MAX];
	char tmp[255];
	char *p = data;
	char *q = tmp;
	char *qlimit = tmp + sizeof(tmp) - 1;
	int esc = 0;
	int quote = 0;
	int k;
	int haveCmdNum = 0;

	memset(argv, 0, sizeof(argv));
	memset(tmp, 0, sizeof(tmp));

	while(*p)
	{
		if (*p == '\\')
		{
			if (esc) {
				if (q >= qlimit)
					goto overflow;
				*q++ = '\\';
				esc = 0;
			} else

			esc = 1;
			p++;
			continue;
		}
		else if (esc)
		{
			if (*p == '"')
			{
				if (q >= qlimit)
					goto overflow;
				*q++ = '"';
			}
			else if (*p == '\\')
			{
				 if (q >= qlimit)
					goto overflow;
				*q++ = '\\';
			 }
			else
			{
				send_back_cmd_result(client_fd, "Unsupported escape sequence", 0);
				goto out;
			}
			p++;
			esc = 0;
			 continue;
		}

		if (*p == '"')
		{
			if (quote)
				quote = 0;
			else
				quote = 1;
			p++;
			continue;
		}

		if (q >= qlimit)
			goto overflow;
		*q = *p++;
		if (!quote && *q == ' ')
		{
			*q = '\0';
			if (haveCmdNum)
			{
				 char *endptr;
				int cmdNum = (int)strtol(tmp, &endptr, 0);
				if (endptr == NULL || *endptr != '\0')
				{
					send_back_cmd_result(client_fd, "Invalid sequence number", 0);
					goto out;
				}
				//TDO Save Cmd Num
				haveCmdNum = 0;
			 }
			else
			{
				if (argc >= WCND_CMD_ARGS_MAX)
					goto overflow;
				argv[argc++] = strdup(tmp);
			}
			memset(tmp, 0, sizeof(tmp));
			q = tmp;
			continue;
		}
		q++;
	}

	*q = '\0';
	if (argc >= WCND_CMD_ARGS_MAX)
		goto overflow;

	argv[argc++] = strdup(tmp);
#if 1
	for (k = 0; k < argc; k++)
	{
		WCND_LOGD("%s: arg[%d] = '%s'", __FUNCTION__ , k, argv[k]);
	}
#endif

	if (quote)
	{
		send_back_cmd_result(client_fd, "Unclosed quotes error", 0);
		goto out;
	}

	cmd_handler handler = NULL;

	pthread_mutex_lock(&pWcndManger->cmdexecuter_list_lock);
	for (i = 0; i <WCND_MAX_CMD_EXECUTER_NUM; ++i)
	{
		if (pWcndManger->cmdexecuter_list[i] && pWcndManger->cmdexecuter_list[i]->name &&
			(!strcmp(argv[0], pWcndManger->cmdexecuter_list[i]->name)))
		{
			handler = pWcndManger->cmdexecuter_list[i]->runcommand;
			break;
		}
	}
	pthread_mutex_unlock(&pWcndManger->cmdexecuter_list_lock);

	if(!handler)
	{
		send_back_cmd_result(client_fd, "Command not recognized", 0);
		goto out;

	}

	handler(client_fd, argc-1, &argv[1]);

out:
	for (j = 0; j < argc; j++)
		free(argv[j]);
	return;

overflow:
	send_back_cmd_result(client_fd, "Command too long", 0);
	goto out;
}

/**
* the data string may contain multi cmds, each cmds end with null character.
*/
static void dispatch_command(WcndManager *pWcndManger, int client_fd, char *data, int data_len)
{
	char buffer[255];
	int count = 0, start = 0, end = data_len;

	if(!data || data_len >= 255) return;
	memset(buffer, 0, sizeof(buffer));
	memcpy(buffer, data, data_len);
	while(buffer[count] == ' ')count++; //remove blankspace

	start = count ;

	while(count < end)
	{
		if(buffer[count] == '\0')
		{
			if(start < count)
			{
				dispatch_command2(pWcndManger, client_fd, buffer+start) ;
			}
			start = count + 1;
		}

		count++;
	}

	if(buffer[end] != '\0')
	{
		WCND_LOGE("%s: cmd should end with null character!", __FUNCTION__);
	}
	else if(start < count)
	{
		dispatch_command2(pWcndManger, client_fd, buffer+start) ;
	}

	return;
}


/**
* may do some clear action that will used during reset
* such as wcnlog.result property.
*/
static void pre_send_cp2_exception_notify(void)
{
    /* Erase any previous setting of the slogresult property */
    property_set(WCND_SLOG_RESULT_PROP_KEY, "0");
}


/**
* check "persist.sys.sprd.wcnlog" and "persist.sys.sprd.wcnlog.result"
* 1. if wcnlog == 0, just return.
* 2. if wcnlog == 1, wait until wcnlog.result == 1.
*/
#define WAIT_ONE_TIME (200)   /* wait for 200ms at a time */
                                  /* when polling for property values */
static void wait_for_dump_logs(void)
{
	const char *desired_status = "1";
	char value[PROPERTY_VALUE_MAX] = {'\0'};

	property_get(WCND_SLOG_ENABLE_PROP_KEY, value, "0");
	int slog_enable = atoi(value);
	if(!slog_enable)
		return;

	int maxwait = 300; // wait max 300 s for slog dump cp2 log
	int maxnaps = (maxwait * 1000) / WAIT_ONE_TIME;

	if (maxnaps < 1)
	{
		maxnaps = 1;
	}

	memset(value, 0, sizeof(value));

	while (maxnaps-- > 0)
	{
		usleep(WAIT_ONE_TIME * 1000);
		if (property_get(WCND_SLOG_RESULT_PROP_KEY, value, NULL))
		{
			if (strcmp(value, desired_status) == 0)
			{
				return;
			}
		}
	}
}

/**
* check "persist.sys.sprd.wcnreset" property to see if reset cp2 or not.
* return non-zero for true.
*/
static int check_if_reset_enable(void)
{
	char value[PROPERTY_VALUE_MAX] = {'\0'};

	property_get(WCND_RESET_PROP_KEY, value, "0");
	int is_reset = atoi(value);

	return is_reset;
}

/**
* check "ro.modem.wcn.enable" property to see if cp2 enabled or not.
* default is enabled
* return non-zero for true.
*/
static int check_if_wcnmodem_enable(void)
{
	char value[PROPERTY_VALUE_MAX] = {'\0'};

	property_get(WCND_MODEM_ENABLE_PROP_KEY, value, "1");
	int is_enabled = atoi(value);

	return is_enabled;
}

/*
 * To block the pipe signal to avoid the process exit.
 */
static void blockSigpipe(void)
{
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGPIPE);

	if (sigprocmask(SIG_BLOCK, &mask, NULL) != 0)
		WCND_LOGD("WARNING: SIGPIPE not blocked\n");
}

/**
* store the fd in the client_fds array.
* when the array is full, then something will go wrong, so please NOTE!!!
*/
static void store_client_fd(int client_fds[], int fd)
{
	if(!client_fds) return;

	int i = 0;

	for (i = 0; i < WCND_MAX_CLIENT_NUM; i++)
	{
		if(client_fds[i] == -1) //invalid fd
		{
			client_fds[i] = fd;
			return;
		}
		else if(client_fds[i] == fd)
		{
			WCND_LOGD("%s: Somethine error happens. restore the same fd:%d", __FUNCTION__, fd);
			return;
		}
	}

	//if full, ignore the last one, and save the new one
	if(i == WCND_MAX_CLIENT_NUM)
	{
		WCND_LOGD("ERRORR::%s: client_fds is FULL", __FUNCTION__);
		client_fds[i-1] = fd;
		return;
	}
}

#define RDWR_FD_FAIL (-2)
#define GENERIC_FAIL (-1)

/**
* process the active client fd ( the client close the remote socket or send something)
*
* return < 0 for fail;
* TODO: can handle the cmd sent from the client here
*/
static int process_active_client_fd(WcndManager *pWcndManger, int fd)
{
	char buffer[255];
	int len;

	memset(buffer, 0, sizeof(buffer));
	len = TEMP_FAILURE_RETRY(read(fd, buffer, sizeof(buffer)-1));//reserve last byte for null character.
	if (len < 0)
	{
		WCND_LOGD("read() failed (%s)", strerror(errno));
		return RDWR_FD_FAIL;
	}
	else if (!len)
	{
		WCND_LOGD("read() failed, the peer of fd(%d) is shutdown", fd);
		return RDWR_FD_FAIL;
	}

	dispatch_command(pWcndManger, fd, buffer, len+1);//len+1 make sure string end with null character.

	return 0;
}

/**
* listen on the server socket for accept connection from client.
* and then also set the client socket to select read fds for:
* 1. to detect the exception from client, then to close the client socket.
* 2. to process the cmd from client in feature.
*/
static void *client_listen_thread(void *arg)
{
	WcndManager *pWcndManger = (WcndManager *)arg;

	if(!pWcndManger)
	{
		WCND_LOGD("%s: UNEXCPET NULL WcndManager", __FUNCTION__);
		exit(-1);
	}

	int pending_fds[WCND_MAX_CLIENT_NUM];
	memset(pending_fds, -1, sizeof(pending_fds));

	while(1)
	{
		int  i = 0;
		fd_set read_fds;
		int rc = 0;
		int max = -1;

		FD_ZERO(&read_fds);

		max = pWcndManger->listen_fd;
		FD_SET(pWcndManger->listen_fd, &read_fds);

		FD_SET(pWcndManger->selfcmd_sockets[1], &read_fds);
		if (pWcndManger->selfcmd_sockets[1] > max)
			max = pWcndManger->selfcmd_sockets[1];

		//if need to deal with the cmd sent from client, here add them to read_fds
		pthread_mutex_lock(&pWcndManger->clients_lock);
		for (i = 0; i < WCND_MAX_CLIENT_NUM; i++)
		{
			int fd = pWcndManger->clients[i].sockfd;
			if(fd != -1) //valid fd
			{
				FD_SET(fd, &read_fds);
				if (fd > max)
					max = fd;
			}
		}
		pthread_mutex_unlock(&pWcndManger->clients_lock);

		WCND_LOGD("listen_fd = %d, max=%d", pWcndManger->listen_fd, max);
		if ((rc = select(max + 1, &read_fds, NULL, NULL, NULL)) < 0)
		{
			if (errno == EINTR)
				continue;

			WCND_LOGD("select failed (%s) listen_fd = %d, max=%d", strerror(errno), pWcndManger->listen_fd, max);
			sleep(1);
			continue;
		}
		else if (!rc)
			continue;

		if (FD_ISSET(pWcndManger->listen_fd, &read_fds))
		{
			struct sockaddr addr;
			socklen_t alen;
			int c;

			//accept the client connection
			do {
				alen = sizeof(addr);
				c = accept(pWcndManger->listen_fd, &addr, &alen);
				WCND_LOGD("%s got %d from accept", WCND_SOCKET_NAME, c);
			} while (c < 0 && errno == EINTR);

			if (c < 0)
			{
				WCND_LOGE("accept failed (%s)", strerror(errno));
				sleep(1);
				continue;
			}

			//save client
			pthread_mutex_lock(&pWcndManger->clients_lock);
			for (i = 0; i < WCND_MAX_CLIENT_NUM; i++)
			{
				if(pWcndManger->clients[i].sockfd == -1) //invalid fd
				{
					pWcndManger->clients[i].sockfd = c;
					pWcndManger->clients[i].type = WCND_CLIENT_TYPE_NOTIFY;
					break;

				}
				else if(pWcndManger->clients[i].sockfd == c)
				{
					WCND_LOGD("%s: Somethine error happens. restore the same fd:%d", __FUNCTION__, c);
					break;
				}
			}

			//if full, ignore the last one, and save the new one
			if(i == WCND_MAX_CLIENT_NUM)
			{
				WCND_LOGD("ERRORR::%s: clients is FULL", __FUNCTION__);
				close(c);
			}
			pthread_mutex_unlock(&pWcndManger->clients_lock);
		}

		/* TODO:   */

		/* Add all active clients to the pending list first */
		memset(pending_fds, -1, sizeof(pending_fds));
		pthread_mutex_lock(&pWcndManger->clients_lock);
		for (i = 0; i<WCND_MAX_CLIENT_NUM; i++)
		{
			int fd = pWcndManger->clients[i].sockfd;
			if ((fd!= -1) && FD_ISSET(fd, &read_fds))
			{
				store_client_fd(pending_fds, fd);
			}
		}
		pthread_mutex_unlock(&pWcndManger->clients_lock);


		if (FD_ISSET(pWcndManger->selfcmd_sockets[1], &read_fds))
			store_client_fd(pending_fds, pWcndManger->selfcmd_sockets[1]);


		/* Process the pending list, since it is owned by the thread, there is no need to lock it */
		for (i = 0; i<WCND_MAX_CLIENT_NUM; i++)
		{
			int fd = pending_fds[i];

			/* remove from the pending list */
			pending_fds[i] = -1;

			/* Process it, if fail is returned and our sockets are connection-based, remove and destroy it */
			if( (fd != -1) && (process_active_client_fd(pWcndManger, fd)  == RDWR_FD_FAIL))
			{
				int j = 0;

				/* Remove the client from our array */
				WCND_LOGD("going to zap %d for %s", fd, WCND_SOCKET_NAME);
				pthread_mutex_lock(&pWcndManger->clients_lock);
				for (j = 0; j<WCND_MAX_CLIENT_NUM; j++)
				{
					if (pWcndManger->clients[j].sockfd == fd) {
						close(fd); //close the socket
						pWcndManger->clients[j].sockfd = -1;
						pWcndManger->clients[j].type = WCND_CLIENT_TYPE_NOTIFY;
						break;
					}
				}
				pthread_mutex_unlock(&pWcndManger->clients_lock);
			}
		}

	}

}


/**
* Note: message send from wcnd must end with null character
*             use null character to identify a completed message.
*/
static int send_msg(WcndManager *pWcndManger, int client_fd, char *msg_str)
{
	if(!pWcndManger || !msg_str) return GENERIC_FAIL;

	char *buf;

	pWcndManger->notify_count++;
	buf = msg_str;

	/* Send the zero-terminated message */

	//Note: message send from wcnd must end with null character
	//use null character to identify a completed message.
	int len = strlen(buf) + 1; //including null character

	WCND_LOGD("send %s to client_fd:%d", buf, client_fd);

	int ret = TEMP_FAILURE_RETRY(write(client_fd, buf, len));
	if(ret < 0)
	{
		WCND_LOGE("write %d bytes to client_fd:%d fail (error:%s)", len, client_fd, strerror(errno));
		ret = RDWR_FD_FAIL;
	}

	return ret;
}

/**
* send notify information to all the connected clients.
* return -1 for fail
* Note: message send from wcnd must end with null character
*             use null character to identify a completed message.
*/
int wcnd_send_notify_to_client(WcndManager *pWcndManger, char *info_str, int notify_type)
{
	int i, ret;

	if(!pWcndManger || !info_str) return -1;

	WCND_LOGD("send_notify_to_client (type:%d)", notify_type);

	if(notify_type == WCND_CLIENT_TYPE_NOTIFY && !pWcndManger->notify_enabled)
	{
		WCND_LOGD("do not need to send_notify_to_client, just return!!");
		return 0;
	}

	pthread_mutex_lock(&pWcndManger->clients_lock);

	/* info socket clients that WCN with str info */
	for(i = 0; i < WCND_MAX_CLIENT_NUM; i++)
	{
		int fd = pWcndManger->clients[i].sockfd;
		int type = pWcndManger->clients[i].type;
		WCND_LOGD("clients[%d].sockfd = %d, type = %d\n",i, fd, type);

		if(fd >= 0 && type == notify_type)
		{
			ret = send_msg(pWcndManger, fd, info_str);
			if(RDWR_FD_FAIL == ret)
			{
				WCND_LOGD("reset clients[%d].sockfd = -1",i);
				close(fd);
				pWcndManger->clients[i].sockfd = -1;
				pWcndManger->clients[i].type = WCND_CLIENT_TYPE_NOTIFY;
			}
			else if(notify_type == WCND_CLIENT_TYPE_CMD || notify_type == WCND_CLIENT_TYPE_CMD_PENDING)
			{
				pWcndManger->clients[i].type = WCND_CLIENT_TYPE_SLEEP;
			}

		}
	}

	pthread_mutex_unlock(&pWcndManger->clients_lock);

	return 0;
}


#define min(x,y) (((x) < (y)) ? (x) : (y))

/**
* write image file pointed by the src_fd to the destination pointed by the dst_fd
* return -1 for fail
*/
static int write_image(int  src_fd, int src_offset, int dst_fd, int dst_offset, int size)
{
	if(src_fd < 0 || dst_fd < 0) return -1;

	int  bufsize, rsize, rrsize, wsize;
	char buf[8192];
	int buf_size = sizeof(buf);

	if (lseek(src_fd, src_offset, SEEK_SET) != src_offset)
	{
		WCND_LOGE("failed to lseek %d in fd: %d", src_offset, src_fd);
		return -1;
	}

	if (lseek(dst_fd, dst_offset, SEEK_SET) != dst_offset)
	{
		WCND_LOGE("failed to lseek %d in fd:%d", dst_offset, dst_fd);
		return -1;
	}

	int totalsize = 0;
	while(size > 0)
	{
		rsize = min(size, buf_size);
		rrsize = read(src_fd, buf, rsize);
		totalsize += rrsize;

		if(rrsize == 0)
		{
			WCND_LOGE("At the end of the file (totalsize: %d)", totalsize);
			break;
		}

		if (rrsize < 0  || rrsize > rsize)
		{
			WCND_LOGE("failed to read fd: %d (ret = %d)", src_fd, rrsize);
			return -1;
		}

		wsize = write(dst_fd, buf, rrsize);
		if (wsize != rrsize)
		{
			WCND_LOGE("failed to write fd: %d [wsize = %d  rrsize = %d  remain = %d]",
					dst_fd, wsize, rrsize, size);
			return -1;
		}
		size -= rrsize;
	}

	return 0;
}

#define LOOP_TEST_CHAR "hi"
#define WATI_FOR_CP2_READY_TIME_MSECS (100)
#define MAX_LOOP_TEST_COUNT (50)
#define LOOP_TEST_INTERVAL_MSECS (100)
#define RESET_FAIL_RETRY_COUNT (3)
#define RESET_RETRY_INTERVAL_MSECS (2000)

/**
* reset the cp2:
* 1. stop cp2: echo "1"  > /proc/cpwcn/stop
* 2. download image: cat /dev/block/platform/sprd-sdhci.3/by-name/wcnmodem > /proc/cpwcn/modem
* 3. start cp2: echo "1" > /proc/cpwcn/start
* 4. polling /dev/spipe_wcn0, do write and read testing.
*/

//stop cp2: echo "1"  > /proc/cpwcn/stop
static inline int stop_cp2(WcndManager *pWcndManger)
{
	int stop_fd = -1;
	int len = 0;

	stop_fd = open(pWcndManger->wcn_stop_iface_name, O_RDWR);
	WCND_LOGD("%s: open stop interface: %s, fd = %d", __func__, pWcndManger->wcn_stop_iface_name, stop_fd);
	if (stop_fd < 0)
	{
		WCND_LOGE("open %s failed, error: %s", pWcndManger->wcn_stop_iface_name, strerror(errno));
		return -1;
	}

	//Stop cp2, write '1' to wcn_stop_iface
	len = write(stop_fd, "1", 1);
	if (len != 1)
	{
		WCND_LOGE("write 1 to %s to stop CP2 failed!!", pWcndManger->wcn_stop_iface_name);

		close(stop_fd);
		return -1;
	}
	close(stop_fd);

	WCND_LOGD("%s:%s is OK", __func__, pWcndManger->wcn_stop_iface_name);

	return 0;
}

//start cp2: echo "1" > /proc/cpwcn/start
static inline int start_cp2(WcndManager *pWcndManger)
{
	int start_fd = -1;
	int len = 0;

	//Start cp2
	start_fd = open( pWcndManger->wcn_start_iface_name, O_RDWR);
	WCND_LOGD("%s: open start interface: %s, fd = %d", __func__, pWcndManger->wcn_start_iface_name, start_fd);
	if (start_fd < 0)
	{
		WCND_LOGE("open %s failed, error: %s", pWcndManger->wcn_start_iface_name, strerror(errno));
		return -1;
	}

	len = write(start_fd, "1", 1);
	if (len != 1)
	{
		WCND_LOGE("write 1 to %s to stop CP2 failed!!", pWcndManger->wcn_stop_iface_name);

		close(start_fd);
		return -1;
	}
	close(start_fd);

	WCND_LOGD("%s:%s is OK", __func__, pWcndManger->wcn_start_iface_name);

	return 0;
}

//download image: cat /dev/block/platform/sprd-sdhci.3/by-name/wcnmodem > /proc/cpwcn/modem
static inline int download_image_to_cp2(WcndManager *pWcndManger)
{
	int download_fd = -1;
	int image_fd = -1;

	download_fd = open( pWcndManger->wcn_download_iface_name, O_RDWR);
	WCND_LOGD("%s: open download interface: %s, fd = %d", __func__, pWcndManger->wcn_download_iface_name, download_fd);
	if (download_fd < 0)
	{
		WCND_LOGE("open %s failed, error: %s", pWcndManger->wcn_download_iface_name, strerror(errno));
		return -1;
	}


	char image_file[256];

	memset(image_file, 0, sizeof(image_file));
	if ( -1 == property_get(PARTITION_PATH_PROP_KEY, image_file, "") )
	{
		WCND_LOGE("%s: get partitionpath fail",__func__);
		close(download_fd);
		return -1;
	}

	strcat(image_file, pWcndManger->wcn_image_file_name);

	image_fd = open( image_file, O_RDONLY);
	WCND_LOGD("%s: image file: %s, fd = %d", __func__, image_file, image_fd);
	if (image_fd < 0)
	{
		WCND_LOGE("open %s failed, error: %s", image_file, strerror(errno));

		close(download_fd);
		return -1;
	}


	WCND_LOGD("Loading %s in bank %s:%d %d", image_file, pWcndManger->wcn_download_iface_name,
		0, pWcndManger->wcn_image_file_size);

	//start downloading  image
	if(write_image(image_fd, 0, download_fd, 0, pWcndManger->wcn_image_file_size) < 0)
	{
		WCND_LOGE("Download IMAGE TO CP2  failed");
		close(image_fd);
		close(download_fd);
		return -1;
	}

	close(image_fd);
	close(download_fd);

	WCND_LOGD("%s:%s is OK", __func__, pWcndManger->wcn_download_iface_name);

	return 0;
}

/*
* polling /dev/spipe_wcn0, do write and read testing.
* is_loopcheck: if true use select.
*     if false use NONBLOCK mode, because after downloading image and starting CP2, the cp2 may not receive the string
*     that wrote to the /dev/spipe_wcn0, for it is not inited completely.
* Note: return 0 for OK; return -1 for fail
*/
static int is_cp2_alive_ok(WcndManager *pWcndManger, int is_loopcheck)
{
	int len = 0;
	int loop_fd = -1;

	//block mode for using select
	if(is_loopcheck)
	    loop_fd = open( pWcndManger->wcn_loop_iface_name, O_RDWR/*|O_NONBLOCK*/);
	else
            loop_fd = open( pWcndManger->wcn_loop_iface_name, O_RDWR|O_NONBLOCK);

	WCND_LOGD("%s: open polling interface: %s, fd = %d", __func__, pWcndManger->wcn_loop_iface_name, loop_fd);
	if (loop_fd < 0)
	{
		WCND_LOGE("open %s failed, error: %s", pWcndManger->wcn_loop_iface_name, strerror(errno));
		return -1;
	}

	len = write(loop_fd, LOOP_TEST_CHAR, strlen(LOOP_TEST_CHAR));
	if(len < 0)
	{
		WCND_LOGE("%s: write %s failed, error:%s", __func__, pWcndManger->wcn_loop_iface_name, strerror(errno));
		close(loop_fd);
		return -1;
	}

       //if it is not loop check, use select instead
	if(!is_loopcheck)
	{
		//wait
		usleep(100*1000);

		char buffer[32];
		memset(buffer, 0, sizeof(buffer));
		do {
			len = read(loop_fd, buffer, sizeof(buffer));
		} while(len < 0 && errno == EINTR);

		if ((len <= 0) || !strstr(buffer,LOOP_TEST_CHAR))
		{
			WCND_LOGE("%s: read %d return %d, buffer:%s,  errno = %s", __func__, loop_fd , len, buffer, strerror(errno));
			close(loop_fd);
			return -1;
		}
	}
	else
	{
		fd_set read_fds;
		int rc = 0;
		int max = -1;
		struct timeval timeout;

		FD_ZERO(&read_fds);

		max = loop_fd;
		FD_SET(loop_fd, &read_fds);

		//time out 2.5 seconds
		timeout.tv_sec = 2;
		timeout.tv_usec = 500000;

	select_retry:
		if ((rc = select(max + 1, &read_fds, NULL, NULL, &timeout)) < 0)
		{
			if (errno == EINTR)
				goto select_retry;

			WCND_LOGD("select loop_fd(%d) failed: %s", loop_fd, strerror(errno));
			close(loop_fd);
			return -1;
		}
		else if (!rc)
		{
			WCND_LOGD("select loop_fd(%d) TimeOut", loop_fd);
			close(loop_fd);
			return -1;
		}

		if (!(FD_ISSET(loop_fd, &read_fds)))
		{
			WCND_LOGD("select loop_fd(%d) return > 0, but loop_fd is not set!", loop_fd);
			close(loop_fd);
			return -1;
		}

		char buffer[32];
		memset(buffer, 0, sizeof(buffer));
		do {
			len = read(loop_fd, buffer, sizeof(buffer));
		} while(len < 0 && errno == EINTR);

		if ((len <= 0) || !strstr(buffer,LOOP_TEST_CHAR))
		{
			WCND_LOGE("%s: read %d return %d, buffer:%s,  errno = %s", __func__, loop_fd , len, buffer, strerror(errno));
			close(loop_fd);
			return -1;
		}
	}

	WCND_LOGD("%s: loop: %s is OK", __func__, pWcndManger->wcn_loop_iface_name);

	close(loop_fd);

	return 0;
}


static int reset_cp2(WcndManager *pWcndManger)
{
	if(!pWcndManger)
	{
		WCND_LOGD("%s: UNEXCPET NULL WcndManager", __FUNCTION__);
		return -1;
	}

	WCND_LOGD("reset_cp2");

	if(stop_cp2(pWcndManger) < 0)
	{
		WCND_LOGE("Stop CP2 failed!!");
		return -1;
	}

	//usleep(100*1000);

	if(download_image_to_cp2(pWcndManger) < 0)
	{
		WCND_LOGE("Download image TO CP2 failed!!");
		return -1;
	}

	//usleep(100*1000);

	if(start_cp2(pWcndManger) < 0)
	{
		WCND_LOGE("Start CP2 failed!!");
		return -1;
	}

	//wait for a moment
	//usleep(WATI_FOR_CP2_READY_TIME_MSECS*1000);

	int loop_test_count = 0;

polling_test:
	//polling loop interface to check if CP2 is boot up complete.
	if(loop_test_count++ > MAX_LOOP_TEST_COUNT)
	{
		WCND_LOGE("%s: write test for %d counts, still failed return!!!!!", __func__, MAX_LOOP_TEST_COUNT);
		return -1;
	}

	if(is_cp2_alive_ok(pWcndManger, 0)<0)
	{
		//wait a moment, and go on.
		//usleep(LOOP_TEST_INTERVAL_MSECS*1000);
		goto polling_test;

	}
	return 0;
}

/**
* do cp2 reset processtrue:
* 1. notify connected clients "cp2 reset start"
* 2. reset cp2
* 3. notify connected clients "cp2 reset end"
*/
int wcnd_do_cp2_reset_process(WcndManager *pWcndManger)
{
	WcndMessage message;

	if(!pWcndManger)
	{
		WCND_LOGE("%s: UNEXCPET NULL WcndManager", __FUNCTION__);
		return -1;
	}

	int is_alive_ok = 1;

	if(!check_if_reset_enable())
	{
		WCND_LOGD("%s: wcn reset disabled, do not do reset!", __FUNCTION__);
		return 0;
	}

	//set doing_reset flag
	pWcndManger->doing_reset = 1;

	WCND_LOGD("wait_for_dump_logs");

	//wait slog may dump log.
	wait_for_dump_logs();

	WCND_LOGD("wait_for_dump_logs end");

	int reset_count = 0;

do_cp2reset:

	if(reset_count++ > RESET_FAIL_RETRY_COUNT)
	{
		WCND_LOGE("%s Reset CP2 for %d times, still failed return!!!!!", __func__, RESET_FAIL_RETRY_COUNT);
		//clear doing_reset flag
		pWcndManger->doing_reset = 0;
		//return  0;
		is_alive_ok = 0;
		goto out;
	}

	//Notify client reset start
	wcnd_send_notify_to_client(pWcndManger, WCND_CP2_RESET_START_STRING, WCND_CLIENT_TYPE_NOTIFY);

#ifdef CP2_RESET_READY
	//begin reseting CP2
	if(reset_cp2(pWcndManger) < 0)
	{
		WCND_LOGD("%s: reset Fail !", __FUNCTION__);
		is_alive_ok = 0;
	}
#endif

	//Notify client reset completed.
	wcnd_send_notify_to_client(pWcndManger, WCND_CP2_RESET_END_STRING, WCND_CLIENT_TYPE_NOTIFY);

	//Notify CP2 alive again
	if(is_alive_ok)
		wcnd_send_notify_to_client(pWcndManger, WCND_CP2_ALIVE_STRING, WCND_CLIENT_TYPE_NOTIFY);
	else
	{
		usleep(RESET_RETRY_INTERVAL_MSECS*1000);
		is_alive_ok = 1;
		goto do_cp2reset;
	}

	//clear doing_reset flag
	pWcndManger->doing_reset = 0;

out:
	//clear the cp2 error flag.
	if(is_alive_ok)
	{
		message.event = WCND_EVENT_CP2_OK;
		message.replyto_fd = -1;
		pWcndManger->is_cp2_error = 0;
	}
	else
	{
		message.event = WCND_EVENT_CP2_DOWN;
		message.replyto_fd = -1;
	}

	wcnd_sm_step(pWcndManger, &message);

	return 0;
}


/**
* open cp2 processure:
* 1. reset cp2
*/
int wcnd_open_cp2(WcndManager *pWcndManger)
{
	WcndMessage message;

	if(!pWcndManger)
	{
		WCND_LOGE("%s: UNEXCPET NULL WcndManager", __FUNCTION__);
		return -1;
	}

	int is_alive_ok = 1;

	//set doing_reset flag
	pWcndManger->doing_reset = 1;

	int reset_count = 0;

do_cp2reset:

	if(reset_count++ > RESET_FAIL_RETRY_COUNT)
	{
		WCND_LOGE("%s Reset CP2 for %d times, still failed return!!!!!", __func__, RESET_FAIL_RETRY_COUNT);
		//clear doing_reset flag
		pWcndManger->doing_reset = 0;
		is_alive_ok = 0;
		goto out;
	}


#ifdef CP2_RESET_READY
	//begin reseting CP2
	if(reset_cp2(pWcndManger) < 0)
	{
		WCND_LOGD("%s: reset Fail !", __FUNCTION__);
		is_alive_ok = 0;
	}
#endif


	//Notify CP2 alive again
	if(!is_alive_ok)
	{
		usleep(RESET_RETRY_INTERVAL_MSECS*1000);
		is_alive_ok = 1;
		goto do_cp2reset;
	}

	//clear doing_reset flag
	pWcndManger->doing_reset = 0;

out:
	//clear the cp2 error flag.
	if(is_alive_ok)
	{
		message.event = WCND_EVENT_CP2_OK;
		message.replyto_fd = -1;
		pWcndManger->is_cp2_error = 0;
	}
	else
	{
		message.event = WCND_EVENT_CP2_DOWN;
		message.replyto_fd = -1;
	}

	wcnd_sm_step(pWcndManger, &message);

	return 0;
}

/**
* close cp2 processure:
*/
int wcnd_close_cp2(WcndManager *pWcndManger)
{
	WcndMessage message;

	if(!pWcndManger)
	{
		WCND_LOGE("%s: UNEXCPET NULL WcndManager", __FUNCTION__);
		return -1;
	}

	//Tell CP2 to Sleep
	if(wcnd_process_atcmd(-1, WCND_ATCMD_CP2_SLEEP, pWcndManger) < 0)
	{
		WCND_LOGE("%s: CP2 SLEEP failed!! Before stop it!!", __FUNCTION__);
		goto out;
	}

	if(stop_cp2(pWcndManger) < 0)
	{
		WCND_LOGE("%s: Stop CP2 failed!!", __FUNCTION__);
		//return -1;
	}

out:
	message.event = WCND_EVENT_CP2_DOWN;
	message.replyto_fd = -1;
	wcnd_sm_step(pWcndManger, &message);

	return 0;
}


/**
* handle the cp2 assert
* 1. send notify to client the cp2 assert
* 2. reset cp2
* 3. send notify to client the cp2 reset completed
*/
static int handle_cp2_assert(WcndManager *pWcndManger, int assert_fd )
{
	if(!pWcndManger)
	{
		WCND_LOGE("%s: UNEXCPET NULL WcndManager", __FUNCTION__);
		return -1;
	}

	WCND_LOGD("handle_cp2_assert\n");

	//set the cp2 error flag, it is cleared when reset successfully
	pWcndManger->is_cp2_error = 1;

	char rdbuffer[200];
	char buffer[255];
	int len;

	memset(buffer, 0, sizeof(buffer));
	memset(rdbuffer, 0, sizeof(rdbuffer));

	len = read(assert_fd, rdbuffer, sizeof(rdbuffer));
	if (len <= 0)
	{
		WCND_LOGE("read %d return %d, errno = %s", assert_fd , len, strerror(errno));
		//return -1;
	}

	//reseting is going on just return
	if(pWcndManger->doing_reset)
		return 0;

	pre_send_cp2_exception_notify();

	wcnd_send_selfcmd(pWcndManger, "wcn "WCND_SELF_EVENT_CP2_ASSERT);

	//notify exception
	snprintf(buffer, 255, "%s %s", WCND_CP2_EXCEPTION_STRING, rdbuffer);
	wcnd_send_notify_to_client(pWcndManger, buffer, WCND_CLIENT_TYPE_NOTIFY);

	//currently the reset is done when receive reset cmd from WcnManagerService.java

	WCND_LOGD("handle_cp2_assert end!\n");

	return 0;
}


/**
* handle cp2 watchdog execption
* 1. send notify to client the cp2 cp2 watchdog execption
* 2. reset cp2
* 3. send notify to client the cp2 reset completed
*/
static int handle_cp2_watchdog_exception(WcndManager *pWcndManger, int watchdog_fd )
{
	if(!pWcndManger)
	{
		WCND_LOGE("%s: UNEXCPET NULL WcndManager", __FUNCTION__);
		return -1;
	}

	WCND_LOGD("handle_cp2_watchdog_exception\n");

	//set the cp2 error flag, it is cleared when reset successfully
	pWcndManger->is_cp2_error = 1;

	char rdbuffer[200];
	char buffer[255];
	int len;

	memset(buffer, 0, sizeof(buffer));
	memset(rdbuffer, 0, sizeof(rdbuffer));

	len = read(watchdog_fd, rdbuffer, sizeof(rdbuffer));
	if (len <= 0)
	{
		WCND_LOGE("read %d return %d, errno = %s", watchdog_fd , len, strerror(errno));
		//return -1;
	}

	//reseting is going on just return
	if(pWcndManger->doing_reset)
		return 0;

	pre_send_cp2_exception_notify();

	wcnd_send_selfcmd(pWcndManger, "wcn "WCND_SELF_EVENT_CP2_ASSERT);

	//notify exception
	snprintf(buffer, 255, "%s %s", WCND_CP2_EXCEPTION_STRING, rdbuffer);
	wcnd_send_notify_to_client(pWcndManger, buffer, WCND_CLIENT_TYPE_NOTIFY);

	//currently the reset is done when receive reset cmd from WcnManagerService.java

	WCND_LOGD("handle_cp2_watchdog_exception end\n");

	return 0;
}


static void *cp2_listen_thread(void *arg)
{
	WcndManager *pWcndManger = (WcndManager *)arg;

	if(!pWcndManger)
	{
		WCND_LOGD("%s: UNEXCPET NULL WcndManager", __FUNCTION__);
		exit(-1);
	}

	int assert_fd = -1;
	int watchdog_fd = -1;

get_assertfd:
	assert_fd = open( pWcndManger->wcn_assert_iface_name, O_RDWR);
	WCND_LOGD("%s: open assert dev: %s, fd = %d", __func__, pWcndManger->wcn_assert_iface_name, assert_fd);
	if (assert_fd < 0)
	{
		WCND_LOGD("open %s failed, error: %s", pWcndManger->wcn_assert_iface_name, strerror(errno));
		sleep(2);
		goto get_assertfd;
		//return NULL;
	}

get_watchdogfd:
	watchdog_fd = open( pWcndManger->wcn_watchdog_iface_name, O_RDWR);
	WCND_LOGD("%s: open watchdog dev: %s, fd = %d", __func__, pWcndManger->wcn_watchdog_iface_name, watchdog_fd);
	if (watchdog_fd < 0)
	{
		WCND_LOGD("open %s failed, error: %s", pWcndManger->wcn_watchdog_iface_name, strerror(errno));
		//close(assert_fd);
		//return NULL;
		sleep(2);
		goto get_watchdogfd;
	}

	while(1)
	{
		int  i = 0;
		fd_set read_fds;
		int rc = 0;
		int max = -1;

		FD_ZERO(&read_fds);

		max = assert_fd;
		FD_SET(assert_fd, &read_fds);

		FD_SET(watchdog_fd, &read_fds);
		if (watchdog_fd > max)
			max = watchdog_fd;

		WCND_LOGD("assert_fd = %d, watchdog_fd = %d, max=%d", assert_fd, watchdog_fd, max);
		if ((rc = select(max + 1, &read_fds, NULL, NULL, NULL)) < 0)
		{
			if (errno == EINTR)
				continue;

			WCND_LOGD("select failed assert_fd = %d, watchdog_fd = %d, max=%d", assert_fd, watchdog_fd, max);
			sleep(1);
			continue;
		}
		else if (!rc)
			continue;

		if (FD_ISSET(assert_fd, &read_fds))
		{
			//there is exception from assert.
			handle_cp2_assert(pWcndManger, assert_fd);
		}

		if (FD_ISSET(watchdog_fd, &read_fds))
		{
			//there is exception from watchdog.
			handle_cp2_watchdog_exception(pWcndManger, watchdog_fd);
		}

		//sleep for a while before going to next polling
		sleep(1);

		/* TODO:   */

	}
}

/**
* Start thread to listen for connection from clients.
* return -1 fail;
*/
static int start_client_listener(WcndManager *pWcndManger)
{
	if(!pWcndManger) return -1;

	pthread_t thread_id;

	if (pthread_create(&thread_id, NULL, client_listen_thread, pWcndManger))
	{
		WCND_LOGE("start_client_listener: pthread_create (%s)", strerror(errno));
		return -1;
	}

	return 0;

}

/**
* Start thread to listen on the CP2 assert/watchdog interface to detect CP2 exception
* return -1 fail;
*/
static int start_cp2_listener(WcndManager *pWcndManger)
{
	if(!pWcndManger) return -1;

	if(!check_if_wcnmodem_enable()) return 0;

	pthread_t thread_id;

	if (pthread_create(&thread_id, NULL, cp2_listen_thread, pWcndManger))
	{
		WCND_LOGE("start_cp2_listener: pthread_create (%s)", strerror(errno));
		return -1;
	}

	return 0;
}

/**
* Initial the wcnd manager struct.
* return -1 for fail;
*/
static int init(WcndManager *pWcndManger)
{
	if(!pWcndManger) return -1;

	memset(pWcndManger, 0, sizeof(WcndManager));

	int i = 0;

	pthread_mutex_init(&pWcndManger->clients_lock, NULL);
	pthread_mutex_init(&pWcndManger->cmdexecuter_list_lock, NULL);

	for(i=0; i<WCND_MAX_CLIENT_NUM; i++)
		pWcndManger->clients[i].sockfd = -1;

	pWcndManger->listen_fd = socket_local_server(WCND_SOCKET_NAME, ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
	if (pWcndManger->listen_fd < 0) {
		WCND_LOGE("%s: cannot create local socket server", __FUNCTION__);
		return -1;
	}

	snprintf(pWcndManger->wcn_assert_iface_name, WCND_MAX_IFACE_NAME_SIZE, "%s", WCN_ASSERT_IFACE);
	snprintf(pWcndManger->wcn_watchdog_iface_name, WCND_MAX_IFACE_NAME_SIZE, "%s", WCN_WATCHDOG_IFACE);
	snprintf(pWcndManger->wcn_loop_iface_name, WCND_MAX_IFACE_NAME_SIZE, "%s", WCN_LOOP_IFACE);
	snprintf(pWcndManger->wcn_start_iface_name, WCND_MAX_IFACE_NAME_SIZE, "%s", WCN_START_IFACE);
	snprintf(pWcndManger->wcn_stop_iface_name, WCND_MAX_IFACE_NAME_SIZE, "%s", WCN_STOP_IFACE);
	snprintf(pWcndManger->wcn_download_iface_name, WCND_MAX_IFACE_NAME_SIZE, "%s", WCN_DOWNLOAD_IFACE);
	snprintf(pWcndManger->wcn_image_file_name, WCND_MAX_IFACE_NAME_SIZE, "%s", WCN_IMAGE_NAME);
	snprintf(pWcndManger->wcn_atcmd_iface_name, WCND_MAX_IFACE_NAME_SIZE, "%s", WCN_ATCMD_IFACE);


	pWcndManger->wcn_image_file_size = WCN_IMAGE_SIZE;


	WCND_LOGD(" WCN ASSERT Interface: %s \n WCN WATCHDOG Interface: %s \n WCN LOOP Interface: %s \n WCN START Interface: %s \n"
		"WCN STOP Interface: %s \n WCN DOWNLAOD Interface: %s \n WCN IMAGE File: %s \n WCN ATCMD Interface: %s",  pWcndManger->wcn_assert_iface_name,
		pWcndManger->wcn_watchdog_iface_name, pWcndManger->wcn_loop_iface_name, pWcndManger->wcn_start_iface_name,
		pWcndManger->wcn_stop_iface_name, pWcndManger->wcn_download_iface_name, pWcndManger->wcn_image_file_name,
		pWcndManger->wcn_atcmd_iface_name);

	//To check if "persist.sys.sprd.wcnreset" is set or not. if not set it to be default "1"
	char value[PROPERTY_VALUE_MAX] = {'\0'};
	if(property_get(WCND_RESET_PROP_KEY, value, NULL) <= 0)
	{
		property_set(WCND_RESET_PROP_KEY, "1");
	}

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, pWcndManger->selfcmd_sockets) == -1) {

		WCND_LOGE("%s: cannot create socketpair for self cmd socket", __FUNCTION__);
		return -1;
	}

	wcnd_sm_init(pWcndManger);

	memcpy(pWcndManger->cp2_version_info, WCND_CP2_DEFAULT_CP2_VERSION_INFO, sizeof(WCND_CP2_DEFAULT_CP2_VERSION_INFO));

	pWcndManger->inited = 1;

	return 0;
}

#ifdef LOOP_CHECK
/**
* Reset CP2 if loop check fail
*/
static void handle_cp2_loop_check_fail(WcndManager *pWcndManger)
{
	if(!pWcndManger)
	{
		WCND_LOGE("%s: UNEXCPET NULL WcndManager", __FUNCTION__);
		return;
	}

	WCND_LOGD("handle_cp2_loop_check_fail\n");

	//set the cp2 error flag, it is cleared when reset successfully
	pWcndManger->is_cp2_error = 1;

	char* rdbuffer = "CP2 LOOP CHECK FAIL";
	char buffer[255];
	int len;

	memset(buffer, 0, sizeof(buffer));

	//reseting is going on just return
	if(pWcndManger->doing_reset)
		return;

	pre_send_cp2_exception_notify();

	wcnd_send_selfcmd(pWcndManger, "wcn "WCND_SELF_EVENT_CP2_ASSERT);

	//notify exception
	snprintf(buffer, 255, "%s %s", WCND_CP2_EXCEPTION_STRING, rdbuffer);
	wcnd_send_notify_to_client(pWcndManger, buffer, WCND_CLIENT_TYPE_NOTIFY);

	//currently the reset is done when receive reset cmd from WcnManagerService.java

	WCND_LOGD("handle_cp2_loop_check_fail end!\n");

	return;

}

/**
*check cp2 loop dev interface in a interval of 5 seconds
* if fail, reset the cp2
*/
#define LOOP_CHECK_INTERVAL_MSECS (5000)
static void *cp2_loop_check_thread(void *arg)
{
	WcndManager *pWcndManger = (WcndManager *)arg;

	int count = 0;
	int is_loopcheck_fail = 0;

	if(!pWcndManger)
	{
		WCND_LOGD("%s: UNEXCPET NULL WcndManager", __FUNCTION__);
		exit(-1);
	}

	while(1)
	{
		usleep(LOOP_CHECK_INTERVAL_MSECS*1000);

		if(!pWcndManger->notify_enabled || (pWcndManger->state != WCND_STATE_CP2_STARTED))
			continue;

		//cp2 exception happens just continue for next poll
		if(pWcndManger->is_cp2_error)
		{
			WCND_LOGD("%s: CP2 exception happened and not reset success!!", __FUNCTION__);

			//wait for another 20 seconds for cp2 to be reset success
			//sleep(20);

			continue;
		}

		count = 2;
		while(count-- > 0)
		{
			if(is_cp2_alive_ok(pWcndManger, 1) < 0)
			{
				if(pWcndManger->is_cp2_error ||
					(pWcndManger->state != WCND_STATE_CP2_STARTED))//during loop checking, cp2 exception happens just continue
				{
					is_loopcheck_fail = 0;
					break;
				}

				is_loopcheck_fail = 1;
			}
			else
			{
				is_loopcheck_fail = 0;
				break;
			}
		}

		if(is_loopcheck_fail)
		{
			WCND_LOGD("%s: loop check fail, going to reset cp2!!", __FUNCTION__);
			handle_cp2_loop_check_fail(pWcndManger);
			//wait 20 seconds for reset
			sleep(20);
		}

	}
}

/**
* Start thread to loop check if CP2 is alive.
* return -1 fail;
*/
static int start_cp2_loop_check(WcndManager *pWcndManger)
{
	if(!pWcndManger) return -1;

	//if wcn modem is not enabled, just return
	if(!check_if_wcnmodem_enable()) return 0;

	pthread_t thread_id;

	if (pthread_create(&thread_id, NULL, cp2_loop_check_thread, pWcndManger))
	{
		WCND_LOGE("start_cp2_loop_check: pthread_create (%s)", strerror(errno));
		return -1;
	}

	return 0;

}
#endif


/**
* Start engineer service , such as for get CP2 log from PC.
* return -1 fail;
*/
static int start_engineer_service(void)
{
	char prop[PROPERTY_VALUE_MAX] = {'\0'};;
	WCND_LOGD("start engservice!");
	property_get(WCND_ENGCTRL_PROP_KEY,prop, "0");
	if(!strcmp(prop, "1")) {
		WCND_LOGD("persist.engpc.disable is true return  ");
		return 0;
	}

	property_set("ctl.start", "engservicewcn");//not used just now
	property_set("ctl.start", "engmodemclientwcn");//not used just now
	property_set("ctl.start", "engpcclientwcn");

	return 0;
}

//To get the build type
#define BUILD_TYPE_PROP_KEY "ro.build.type"
#define USER_DEBUG_VERSION_STR "userdebug"
#define DISABLE_CP2_LOG_CMD "AT+ARMLOG=0\r"
/**
* Disable the CP2 log, if it is a user version
*/
static int check_disable_cp2_log(WcndManager *pWcndManger)
{
	char value[PROPERTY_VALUE_MAX] = {'\0'};

	property_get(BUILD_TYPE_PROP_KEY, value, USER_DEBUG_VERSION_STR);

	if(strstr(value, USER_DEBUG_VERSION_STR))
	{
		if(pWcndManger)	pWcndManger->is_in_userdebug = 1;
		WCND_LOGD("userdebug version: %s, do not need to disable cp2 log!!!", value);
		return 0;
	}

	WCND_LOGD("in user version: %s, need to disable cp2 log!!!", value);

	return wcnd_process_atcmd(-1, DISABLE_CP2_LOG_CMD, pWcndManger);
}



#define GET_CP2_VERSION_INFO_ATCMD "at+spatgetcp2info\r"

/**
* Store the CP2 Version info, used when startup
*/
static int store_cp2_version_info(WcndManager *pWcndManger)
{
	if(!pWcndManger) return -1;

	int count = 100;

#ifdef WCND_CP2_POWER_ONOFF_DISABLED

	return 0;

#endif

	wcnd_send_selfcmd(pWcndManger, "wcn "WCND_SELF_CMD_START_CP2);

	//wait CP2 started, wait 10s at most
	while(count-- > 0)
	{
		if(pWcndManger->state == WCND_STATE_CP2_STARTED)
			break;

		usleep(100*1000);
	}

	if(pWcndManger->state != WCND_STATE_CP2_STARTED)
	{
		WCND_LOGE("%s: CP2 does not start successs, just return!!", __func__);
		return -1;
	}

//#ifdef WCND_CP2_POWER_ONOFF_DISABLED

//	return 0;

//#endif

	wcnd_process_atcmd(-1, GET_CP2_VERSION_INFO_ATCMD, pWcndManger);

	wcnd_send_selfcmd(pWcndManger, "wcn "WCND_SELF_CMD_STOP_CP2);

	return 0;
}


#ifndef FOR_UNIT_TEST

#ifdef WIFI_ENGINEER_ENABLE
//external cmd executer declare here.
extern WcnCmdExecuter wcn_eng_cmdexecuter;
#endif

int main(int argc, char *argv[])
{
	generate_wifi_mac();
	generate_bt_mac();

	blockSigpipe();

	WcndManager *pWcndManger = wcnd_get_default_manager();
	if(!pWcndManger)
	{
		WCND_LOGE("wcnd_get_default_manager Fail!!!");
		return -1;
	}

	if(init(pWcndManger) < 0)
	{
		WCND_LOGE("Init pWcnManager Fail!!!");
		return -1;
	}

	if(start_client_listener(pWcndManger) < 0)
	{
		WCND_LOGE("Start client listener Fail!!!");
		return -1;
	}

#ifdef CP2_WATCHER_ENABLE
	if(start_cp2_listener(pWcndManger) < 0)
	{
		WCND_LOGE("Start CP2 listener Fail!!!");
		return -1;
	}
#endif

#ifdef LOOP_CHECK
	if(start_cp2_loop_check(pWcndManger) < 0)
	{
		WCND_LOGE("Start CP2loop_check Fail!!!");
	}
#endif

	//Start engineer service , such as for get CP2 log from PC.
	start_engineer_service();

	//register builin cmd executer
	wcnd_register_cmdexecuter(pWcndManger, &wcn_cmdexecuter);

#ifdef WIFI_ENGINEER_ENABLE
	//register external cmd executer such eng mode
	wcnd_register_cmdexecuter(pWcndManger, &wcn_eng_cmdexecuter);
#endif

	// Disable the CP2 log, if it is a user version
	check_disable_cp2_log(pWcndManger);


	//get CP2 version and save it
	store_cp2_version_info(pWcndManger);

	//do nothing, just sleep
	do {
		sleep(1000);
	} while(1);

	return 0;
}

#else

//##########################################################################################################
//For Test start
//###########################################################################################################

#define TEST_STOP "stop"
#define TEST_START "start"
#define TEST_DOWNLOAD "download"
#define TEST_RESET "reset"
#define TEST_POLLING "poll"
#define TEST_ENG_CMD "eng"
#define TEST_WCN_CMD "wcn"

static void * test_client_thread(void *arg)
{
	int client_fd = -1;
	client_fd = socket_local_client( WCND_SOCKET_NAME,
		ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);

	while(client_fd < 0)
	{
		WCND_LOGD("%s: Unable bind server %s, waiting...\n",__func__, WCND_SOCKET_NAME);
		usleep(100*1000);
		client_fd = socket_local_client( WCND_SOCKET_NAME,
			ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
	}

	for(;;)
	{
		char buffer[128];
		int n = 0;
		memset(buffer, 0, 128);
		WCND_LOGD("%s: waiting for server %s\n",__func__, WCND_SOCKET_NAME);
		n = read(client_fd, buffer, 128);
		WCND_LOGD("%s: get %d bytes %s\n", __func__, n, buffer);
	}

	return NULL;
}

#ifdef WIFI_ENGINEER_ENABLE
extern WcnCmdExecuter wcn_eng_cmdexecuter;
#endif

int main(int argc, char *argv[])
{
	blockSigpipe();

	WcndManager *pWcndManger = wcnd_get_default_manager();
	if(!pWcndManger)
	{
		WCND_LOGE("Malloc mem for pWcnManager Fail!!!");
		return -1;
	}

	if(init(pWcndManger) < 0)
	{
		WCND_LOGE("Init pWcnManager Fail!!!");
		return -1;
	}

	if(start_client_listener(pWcndManger) < 0)
	{
		WCND_LOGE("Start client listener Fail!!!");
		return -1;
	}

#ifdef CP2_WATCHER_ENABLE
	if(start_cp2_listener(pWcndManger) < 0)
	{
		WCND_LOGE("Start CP2 listener Fail!!!");
		return -1;
	}
#endif

	pthread_t test_thread_id;

	if (pthread_create(&test_thread_id, NULL, test_client_thread, pWcndManger))
	{
		WCND_LOGE("tes_client_thread: pthread_create (%s)", strerror(errno));
		return -1;
	}

	//register builin cmd executer
	wcnd_register_cmdexecuter(pWcndManger, &wcn_cmdexecuter);

#ifdef WIFI_ENGINEER_ENABLE
	//register external cmd executer such eng mode
	wcnd_register_cmdexecuter(pWcndManger, &wcn_eng_cmdexecuter);
#endif

	if(argc > 1)
	{
		if(!strncasecmp(argv[1], TEST_STOP, strlen(TEST_STOP)))
		{
			stop_cp2(pWcndManger);
		}
		else if(!strncasecmp(argv[1], TEST_START, strlen(TEST_START)))
		{
			start_cp2(pWcndManger);
		}
		else if(!strncasecmp(argv[1], TEST_DOWNLOAD, strlen(TEST_DOWNLOAD)))
		{
			download_image_to_cp2(pWcndManger);
		}
		else if(!strncasecmp(argv[1], TEST_RESET, strlen(TEST_RESET)))
		{
			reset_cp2(pWcndManger);
		}
		else if(!strncasecmp(argv[1], TEST_POLLING, strlen(TEST_POLLING)))
		{
			int loop_test_count = 0;

		polling_test2:
			//polling loop interface to check if CP2 is boot up complete.
			if(loop_test_count++ > MAX_LOOP_TEST_COUNT)
			{
				WCND_LOGE("%s: write test for %d counts, still failed return!!!!!", __func__, MAX_LOOP_TEST_COUNT);
				return -1;
			}

			if(is_cp2_alive_ok(pWcndManger, 1)<0)
			{
				//wait a moment, and go on.
				usleep(LOOP_TEST_INTERVAL_MSECS*1000);
				goto polling_test2;

			}
		}
		else if((!strncasecmp(argv[1], TEST_ENG_CMD, strlen(TEST_ENG_CMD)) && argc > 2) ||
			(!strncasecmp(argv[1], TEST_WCN_CMD, strlen(TEST_WCN_CMD)) && argc > 2))
		{
			cmd_handler handler = NULL;
			int i = 0;
			pthread_mutex_lock(&pWcndManger->cmdexecuter_list_lock);
			for (i = 0; i <WCND_MAX_CMD_EXECUTER_NUM; ++i)
			{
				if (pWcndManger->cmdexecuter_list[i] && pWcndManger->cmdexecuter_list[i]->name &&
					(!strcmp(argv[1], pWcndManger->cmdexecuter_list[i]->name)))
				{
					handler = pWcndManger->cmdexecuter_list[i]->runcommand;
					break;
				}
			}
			pthread_mutex_unlock(&pWcndManger->cmdexecuter_list_lock);

			if(!handler)
			{
				WCND_LOGE( "Command not recognized");
			}

			handler(-1, argc-2, &argv[2]);
		}

	}
	else
	{
		while(1)sleep(20);
	}
	return 0;
}

#endif
//##########################################################################################################
//For Test stop
//##########################################################################################################

