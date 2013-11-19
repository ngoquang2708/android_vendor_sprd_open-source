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
	WCND_LOGD("generate seed: %u", seed);
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

void force_replace_config_file()
{
	FILE *f_src, *f_dst;
	char buf[100];

	f_src = fopen(WCND_FACTORY_CONFIG_FILE_PATH, "r");
	if (f_src == NULL) return;
	fgets(buf, sizeof(buf), f_src);
	fclose(f_src);
	
	f_dst = fopen(WCND_CONFIG_FILE_PATH, "w");
	if (f_dst == NULL) return;
	fputs(buf, f_dst);
	fclose(f_dst);

	sprintf(buf, "chmod 666 %s", WCND_CONFIG_FILE_PATH);
	system(buf);
	WCND_LOGD("force_replace_config_file: %s", buf);
}

void generate_mac()
{
	unsigned char mac[MAC_LEN];
	// force replace configuration file if vaild mac is in factory configuration file
	if(is_file_exists(WCND_FACTORY_CONFIG_FILE_PATH)) {
		WCND_LOGD("factory configuration file exists");
		read_mac_from_file(WCND_FACTORY_CONFIG_FILE_PATH, mac);
		if(!is_zero_ether_addr(mac)) {
			force_replace_config_file();
			return;
		}
	}
	// if vaild mac is in configuration file, use it
	if(is_file_exists(WCND_CONFIG_FILE_PATH)) {
		WCND_LOGD("configuration file exists");
		read_mac_from_file(WCND_CONFIG_FILE_PATH, mac);
		if(!is_zero_ether_addr(mac)) return;
	}
	// generate random mac and write to configuration file
	get_random_mac(mac);
	write_mac_to_file(WCND_CONFIG_FILE_PATH, mac);
}

int main(int argc, char *argv[])
{
	generate_mac();
	return 0;
}
