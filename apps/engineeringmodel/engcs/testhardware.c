#include<sys/types.h>
#include<sys/stat.h>
#include<errno.h>
#include<stdio.h>
#include<fcntl.h>
#include<unistd.h>
#include<stdlib.h>
#include<limits.h>
#include "hardware_legacy/wifi.h"
#include "engopt.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <cutils/properties.h>

void* thread_handle_test_wifi(void *data)
{
	int ret = 0;
	char reply[4096] = { 0 };
	size_t len = 4096;

	printf("00\n");
	ret = wifi_load_driver();
	printf("01\n");
	if (ret < 0)
		goto error;
	ret = wifi_start_supplicant();
	printf("02\n");
	if (ret < 0)
		goto error;
#if 0
	ret = wifi_connect_to_supplicant();
	printf("03\n");
	if (ret < 0)
	goto error;
	ret = wifi_command("scan", reply, &len);
	printf("04\n");
	if (ret < 0)
	goto error;
#endif
	property_set("wifi.init", "PASS");
	printf("pass\n");
	return 0;

	error: property_set("wifi.init", "FAIL");
	printf("fail\n");
	return -1;
}
int test_wifi()
{
	int i = 0;
	char buff[256] = { 0 };

	pthread_t pid;
	pthread_create(&pid, NULL, thread_handle_test_wifi, NULL);

	for (i = 0; i < 5; i++) {
		property_get("wifi.init", buff, "");
		if (strstr(buff, "PASS") != NULL)
			return 0;
		sleep(1);
	}
	property_set("wifi.init", "FAIL");

	return 0;
}

int test_gps()
{
	int i = 0;
	char buff[256] = { 0 };

	system("/system/bin/csr_gps_tm 300 &");
	for (i = 0; i < 30; i++) {
		property_get("gps.init", buff, "");
		if (strstr(buff, "PASS") != NULL)
			return 0;
		else if (strstr(buff, "FAIL") != NULL)
			return -1;
		sleep(1);
	}
	property_set("gps.init", "FAIL");

	return -1;
}

int main(void)
{
	int fd = 0;
	int rc = 0;
	int califlag = 0;
	char cmdline[1024] = { 0 };
	fd = open("/proc/cmdline", O_RDONLY);

	if (fd > 0) {
		rc = read(fd, cmdline, sizeof(cmdline));
		if (rc > 0) {
			if (strstr(cmdline, "calibration") != NULL)
				califlag = 1;
		}
	}

	if (califlag == 1) {
		test_gps();
		test_wifi();
		return 0;
	}
	return 0;
}
