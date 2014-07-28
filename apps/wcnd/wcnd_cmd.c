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

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

/**
* Note:
* For the connection that will only be used to send a commond, when handle the command from this
* connection, its client type will be set to WCND_CLIENT_TYPE_CMD. With this type, after the command
* result is send back, its client type will be set to WCND_CLIENT_TYPE_SLEEP, then when there is an event
* to notify, these connections will be ignored.
*/

static int wcn_process_btwificmd(int client_fd, char* cmd_str, WcndManager *pWcndManger)
{
	if(!pWcndManger || !cmd_str) return -1;

#ifdef WCND_CP2_POWER_ONOFF_DISABLED

	wcnd_send_notify_to_client(pWcndManger, WCND_CMD_RESPONSE_STRING" OK", WCND_CLIENT_TYPE_CMD);

	return 0;

#endif

	WcndMessage message;

	message.event = 0;
	message.replyto_fd = client_fd;

	if(!strcmp(cmd_str, WCND_CMD_BT_CLOSE_STRING))
		message.event = WCND_EVENT_BT_CLOSE;
	else if(!strcmp(cmd_str, WCND_CMD_BT_OPEN_STRING))
		message.event = WCND_EVENT_BT_OPEN;
	else if(!strcmp(cmd_str, WCND_CMD_WIFI_CLOSE_STRING))
		message.event = WCND_EVENT_WIFI_CLOSE;
	else if(!strcmp(cmd_str, WCND_CMD_WIFI_OPEN_STRING))
		message.event = WCND_EVENT_WIFI_OPEN;

	return wcnd_sm_step(pWcndManger, &message);
}

/**
* client_fd: the fd to send back the comand response
* return < 0 for fail
*/
int wcnd_process_atcmd(int client_fd, char *atcmd_str, WcndManager *pWcndManger)
{
	int len = 0;
	int atcmd_fd = -1;
	char buffer[255] ;
	int to_get_cp2_version = 0;
	int ret_value = -1;
	int to_tell_cp2_sleep = 0;

	if( !atcmd_str || !pWcndManger)
		return -1;

	int atcmd_len = strlen(atcmd_str);

	WCND_LOGD("%s: Receive AT CMD: %s, len = %d", __func__, atcmd_str, atcmd_len);

	memset(buffer, 0, sizeof(buffer));

	//check if it is going to get cp2 version
	if(strstr(atcmd_str, "spatgetcp2info") || strstr(atcmd_str, "SPATGETCP2INFO"))
	{
		WCND_LOGD("%s: To get cp2 version", __func__);
		to_get_cp2_version = 1;
	}
	else if(!strcmp(atcmd_str, WCND_ATCMD_CP2_SLEEP))
	{
		WCND_LOGD("%s: To tell cp2 to sleep ", __func__);
		to_tell_cp2_sleep = 1;
	}

	//special case for getting cp2 version, IF CP2 not started, use the saved VERSION info.
	if((pWcndManger->state != WCND_STATE_CP2_STARTED) && !to_tell_cp2_sleep)
	{
		if(to_get_cp2_version)
		{
			snprintf(buffer, 254, "%s", pWcndManger->cp2_version_info);
			WCND_LOGD("%s: Save version info: '%s'", __func__, buffer);
			ret_value = 0;
		}
		else
			snprintf(buffer, 254, "Fail: No data available");

		goto out;
	}

	snprintf(buffer, 255, "%s", atcmd_str);

	//at cmd shoud end with '\r'
	if((atcmd_len < 254) && (buffer[atcmd_len - 1] != '\r'))
	{
		buffer[atcmd_len] = '\r';
		atcmd_len++;
	}

	atcmd_fd = open( pWcndManger->wcn_atcmd_iface_name, O_RDWR|O_NONBLOCK);
	WCND_LOGD("%s: open at cmd interface: %s, fd = %d", __func__, pWcndManger->wcn_atcmd_iface_name, atcmd_fd);
	if (atcmd_fd < 0)
	{
		WCND_LOGE("open %s failed, error: %s", pWcndManger->wcn_atcmd_iface_name, strerror(errno));
		return -1;
	}

	len = write(atcmd_fd, buffer, atcmd_len);
	if(len < 0)
	{
		WCND_LOGE("%s: write %s failed, error:%s", __func__, pWcndManger->wcn_atcmd_iface_name, strerror(errno));
		close(atcmd_fd);
		return -1;
	}

	//wait
	usleep(100*1000);

	WCND_LOGD("%s: Wait ATcmd to return", __func__);

	//Get AT Cmd Response
	int try_counts = 0;

try_again:
	if(try_counts++ > 5)
	{
		WCND_LOGE("%s: wait for response fail!!!!!", __func__);
		if(to_get_cp2_version)
		{
			snprintf(buffer, 254, "%s", pWcndManger->cp2_version_info);
			ret_value = 0;
		}
		else
			snprintf(buffer, 254, "Fail: No data available");

	}
	else
	{
		memset(buffer, 0, sizeof(buffer));

		do {
			len = read(atcmd_fd, buffer, sizeof(buffer)-1);
		} while(len < 0 && errno == EINTR);

		if ((len <= 0))
		{
			WCND_LOGE("%s: read fd(%d) return len(%d), errno = %s", __func__, atcmd_fd , len, strerror(errno));
			usleep(300*1000);
			goto try_again;
		}
		else
		{
			//save the CP2 version info
			if(to_get_cp2_version)
				memcpy(pWcndManger->cp2_version_info, buffer, sizeof(buffer));

			if(to_tell_cp2_sleep && (strstr(buffer, "fail") || strstr(buffer, "FAIL")))
			{
				ret_value = -1;
			}
			else
				ret_value = 0;

		}
	}

	WCND_LOGD("%s: ATcmd to %s return: '%s'", __func__, pWcndManger->wcn_atcmd_iface_name, buffer);

	close(atcmd_fd);

out:
	if(client_fd <= 0)
	{
		WCND_LOGE("Write '%s' to Invalid client_fd, ret_value: %d", buffer, ret_value);
		return ret_value;
	}
	//send back the response
	int ret = write(client_fd, buffer, strlen(buffer)+1);
	if(ret < 0)
	{
		WCND_LOGE("write %s to client_fd:%d fail (error:%s)", buffer, client_fd, strerror(errno));
		return -1;
	}

	return ret_value;
}

int wcnd_runcommand(int client_fd, int argc, char* argv[])
{
	WcndManager *pWcndManger = wcnd_get_default_manager();

	if(argc < 1)
	{
		wcnd_send_back_cmd_result(client_fd, "Missing argument", 0);
		return 0;
	}

#if 1
	int k = 0;
	for (k = 0; k < argc; k++)
	{
		WCND_LOGD("%s: arg[%d] = '%s'", __FUNCTION__, k, argv[k]);
	}
#endif

	if (!strcmp(argv[0], "reset"))
	{
		//tell the client the reset cmd is executed
		wcnd_send_back_cmd_result(client_fd, NULL, 1);

		wcnd_do_cp2_reset_process(pWcndManger);
	}
	else if(!strcmp(argv[0], "test"))
	{
		WCND_LOGD("%s: do nothing for test cmd", __FUNCTION__);
		wcnd_send_back_cmd_result(client_fd, NULL, 1);
	}
	else if(strstr(argv[0], "at+"))
	{
		WCND_LOGD("%s: AT cmd(%s)(len=%d)", __FUNCTION__, argv[0], strlen(argv[0]));
		wcnd_process_atcmd(client_fd, argv[0], pWcndManger);
	}
	else if(strstr(argv[0], "BT") || strstr(argv[0], "WIFI")) //bt/wifi cmd
	{
		int i = 0;
		//to set the type to be cmd
		pthread_mutex_lock(&pWcndManger->clients_lock);
		for (i = 0; i < WCND_MAX_CLIENT_NUM; i++)
		{
			if(pWcndManger->clients[i].sockfd == client_fd)
			{
				pWcndManger->clients[i].type = WCND_CLIENT_TYPE_CMD;
				break;
			}
		}
		pthread_mutex_unlock(&pWcndManger->clients_lock);

		wcn_process_btwificmd(client_fd, argv[0], pWcndManger);

	}
	else if(!strcmp(argv[0], WCND_SELF_CMD_START_CP2))
	{

		wcnd_open_cp2(pWcndManger);
	}
	else if(!strcmp(argv[0], WCND_SELF_CMD_STOP_CP2))
	{

		wcnd_close_cp2(pWcndManger);
	}
	else if(!strcmp(argv[0], WCND_SELF_CMD_PENDINGEVENT))
	{
		WcndMessage message;

		message.event = WCND_EVENT_PENGING_EVENT;
		message.replyto_fd = -1;
		wcnd_sm_step(pWcndManger, &message);
	}
	else if(!strcmp(argv[0], WCND_SELF_EVENT_CP2_ASSERT))
	{
		WcndMessage message;

		message.event = WCND_EVENT_CP2_ASSERT;
		message.replyto_fd = -1;
		wcnd_sm_step(pWcndManger, &message);
	}
	else if(!strcmp(argv[0], WCND_CMD_CP2_POWER_ON))
	{
		WcndMessage message;
		int i = 0;

		//to set the type to be cmd
		pthread_mutex_lock(&pWcndManger->clients_lock);
		for (i = 0; i < WCND_MAX_CLIENT_NUM; i++)
		{
			if(pWcndManger->clients[i].sockfd == client_fd)
			{
				pWcndManger->clients[i].type = WCND_CLIENT_TYPE_CMD;
				break;
			}
		}
		pthread_mutex_unlock(&pWcndManger->clients_lock);


#ifdef WCND_CP2_POWER_ONOFF_DISABLED

		wcnd_send_notify_to_client(pWcndManger, WCND_CMD_RESPONSE_STRING" OK", WCND_CLIENT_TYPE_CMD);

		return 0;

#endif

		message.event = WCND_EVENT_CP2POWERON_REQ;
		message.replyto_fd = client_fd;
		wcnd_sm_step(pWcndManger, &message);
	}
	else if(!strcmp(argv[0], WCND_CMD_CP2_POWER_OFF))
	{
		WcndMessage message;
		int i = 0;

		//to set the type to be cmd
		pthread_mutex_lock(&pWcndManger->clients_lock);
		for (i = 0; i < WCND_MAX_CLIENT_NUM; i++)
		{
			if(pWcndManger->clients[i].sockfd == client_fd)
			{
				pWcndManger->clients[i].type = WCND_CLIENT_TYPE_CMD;
				break;
			}
		}
		pthread_mutex_unlock(&pWcndManger->clients_lock);

#ifdef WCND_CP2_POWER_ONOFF_DISABLED

		wcnd_send_notify_to_client(pWcndManger, WCND_CMD_RESPONSE_STRING" OK", WCND_CLIENT_TYPE_CMD);

		return 0;

#endif

		message.event = WCND_EVENT_CP2POWEROFF_REQ;
		message.replyto_fd = client_fd;
		wcnd_sm_step(pWcndManger, &message);
	}
	else
		wcnd_send_back_cmd_result(client_fd, "Not support cmd", 0);


	return 0;
}


