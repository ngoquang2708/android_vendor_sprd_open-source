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
#include <dirent.h>

#include "wcnd.h"



#include <cutils/properties.h>

#include <fcntl.h>
#include <sys/socket.h>


#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/limits.h>

#include <netinet/in.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <errno.h>
#include <netutils/ifc.h>



static int wcnd_read_to_buf(const char *filename, char *buf, int buf_size)
{
	int fd;

	if(buf_size <= 1) return -1;

	ssize_t ret = -1;
	fd = open(filename, O_RDONLY);
	if(fd >= 0)
	{
		ret = read(fd, buf, buf_size-1);
		close(fd);
	}
	((char *)buf)[ret > 0 ? ret : 0] = '\0';
	return ret;
}



static unsigned wcnd_strtou(const char *string)
{
	unsigned long v;
	char *endptr;
	char **endp = &endptr;

	if(!string) return UINT_MAX;

	*endp = (char*) string;

	if (!isalnum(string[0])) return UINT_MAX;
	errno = 0;
	v = strtoul(string, endp, 10);
	if (v > UINT_MAX) return UINT_MAX;

	char next_ch = **endp;
	if(next_ch)
	{
		/* "1234abcg" or out-of-range */
		if (isalnum(next_ch) || errno)
			return UINT_MAX;

		/* good number, just suspicious terminator */
		errno = EINVAL;

	}

	return v;
}



static pid_t wcnd_find_pid_by_name(const char *proc_name)
{
	pid_t target_pid = 0;

	DIR *proc_dir = NULL;
	struct dirent *entry = NULL;

	if(!proc_name) return 0;

	proc_dir = opendir("/proc");

	if(!proc_dir)
	{
		WCND_LOGE("open /proc fail: %s", strerror(errno));
		return 0;
	}

	while((entry = readdir(proc_dir)))
	{
		char buf[1024];
		unsigned pid;
		int n;
		char filename[sizeof("/proc/%u/task/%u/cmdline") + sizeof(int)*3 * 2];


		pid = wcnd_strtou(entry->d_name);
		if (errno)
			continue;


		sprintf(filename, "/proc/%u/cmdline", pid);

		n = wcnd_read_to_buf(filename, buf, 1024);

		if(n < 0)	continue;

		WCND_LOGD("pid: %d, command name: %s", pid, buf);


		if(strcmp(buf, proc_name) == 0)
		{
			WCND_LOGD("find pid: %d for target process name: %s", pid, proc_name);

			target_pid = pid;

			break;
		}
	} /* for (;;) */

	if(proc_dir) closedir(proc_dir);


	return target_pid;
}



int wcnd_kill_process(pid_t pid, int signal)
{
	//signal such as SIGKILL
	return kill(pid, signal);
}


int wcnd_kill_process_by_name(const char *proc_name, int signal)
{

	pid_t target_pid = wcnd_find_pid_by_name(proc_name);

	if(target_pid == 0)
	{
		WCND_LOGD("Cannot find the target pid!!");
		return -1;
	}

	return wcnd_kill_process(target_pid, signal);
}


/**
 * Return 0: for the process is not exist.
 * Otherwise return the pid of the process.
 */
int wcnd_find_process_by_name(const char *proc_name)
{
	pid_t target_pid = wcnd_find_pid_by_name(proc_name);

	if(target_pid == 0)
	{
		WCND_LOGD("Cannot find the target process!!");
		return 0;
	}

	return (int)target_pid;
}


int wcnd_down_network_interface(const char *ifname)
{
	ifc_init();

	if (ifc_down(ifname))
	{
		WCND_LOGE("Error downing interface: %s", strerror(errno));
	}
	ifc_close();
	return 0;
}

int wcnd_up_network_interface(const char *ifname)
{
	ifc_init();

	if (ifc_up(ifname))
	{
		WCND_LOGE("Error upping interface: %s", strerror(errno));
	}
	ifc_close();
	return 0;
}

#define WAIT_ONE_TIME (200)   /* wait for 200ms at a time when polling for property values */

void wcnd_wait_for_supplicant_stopped(void)
{
	char value1[PROPERTY_VALUE_MAX] = {'\0'};
	char value2[PROPERTY_VALUE_MAX] = {'\0'};

	property_get("init.svc.p2p_supplicant", value1, "stopped");
	property_get("init.svc.wpa_supplicant", value2, "stopped");

	if((strcmp(value1, "stopped") == 0) && (strcmp(value2, "stopped") == 0))
		return;

	int maxwait = 3; // wait max 30 s for slog dump cp2 log
	int maxnaps = (maxwait * 1000) / WAIT_ONE_TIME;

	if (maxnaps < 1)
	{
		maxnaps = 1;
	}

	memset(value1, 0, sizeof(value1));
	memset(value2, 0, sizeof(value2));

	while (maxnaps-- > 0)
	{
		usleep(200 * 1000);
		if (property_get("init.svc.p2p_supplicant", value1, "stopped") && property_get("init.svc.wpa_supplicant", value2, "stopped"))
		{
			if((strcmp(value1, "stopped") == 0) && (strcmp(value2, "stopped") == 0))
			{
				return;
			}
		}
	}
}

