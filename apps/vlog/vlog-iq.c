#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/time.h>

#define DATA_BUF_SIZE (4096 * 4)
static char log_data[DATA_BUF_SIZE];

#define VLOG_PRI  -20
static void set_vlog_priority(void)
{
	int inc = VLOG_PRI;
	int res = 0;

	errno = 0;
	res = nice(inc);
	if (res < 0){
		printf("cannot set vlog priority, res:%d ,%s\n", res,
				strerror(errno));
		return;
	}
	int pri = getpriority(PRIO_PROCESS, getpid());
	printf("now vlog priority is %d\n", pri);
	return;
}

#define FILE_PATH "/mnt/sdcard/"
/*
 * create simple unique file name
 * filename = path+pid.log
 */
void create_log_file_name(char *file_name)
{
	char temp_str[20] = {0};
	pid_t pid;
	int len;

	pid = getpid();
	
	sprintf(temp_str,"%d.log", pid);

	len = strlen(FILE_PATH);
	strncpy(file_name, FILE_PATH, len);
	strncpy(&file_name[len], temp_str, strlen(temp_str));
}

int main(int argc, char **argv)
{
	int pipe_fd;
	int dst_fd;
	int res;
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;

	char file_name[100] = {0};

	set_vlog_priority();

	printf("open usb serial\n");

	create_log_file_name(file_name);

	dst_fd = open(file_name,O_WRONLY | O_CREAT | O_TRUNC, mode);
	if(dst_fd < 0) {
		printf("cannot open log file \n");
		exit(1);
	}
	printf("create log file %s\n", file_name);

	printf("open vitual pipe\n");
	pipe_fd = open("/dev/vbpipe4",O_RDONLY);
	if(pipe_fd < 0) {
		printf("cannot open vpipe4\n");
		exit(1);
	}

	printf("put log data from pipe to destination\n");
	while(1) {
		ssize_t r_cnt, w_cnt;
		r_cnt = read(pipe_fd, log_data, DATA_BUF_SIZE);
		if (r_cnt < 0) {
			printf("no log data :%d\n", r_cnt);
			continue;
		}
		//printf("read %d\n", r_cnt);
		w_cnt = write(dst_fd, log_data, r_cnt);
		if (w_cnt < 0) {
			printf("no log data write:%d ,%s\n", w_cnt,
					strerror(errno));


			printf("close dst file\n");
			printf("something wrong, maybe no space in SDCARD\n");
			close(dst_fd);
			break;

		}
		//printf("read %d, write %d\n", r_cnt, w_cnt);
	}
out:
	close(pipe_fd);
	close(dst_fd);
	return 0;
}
