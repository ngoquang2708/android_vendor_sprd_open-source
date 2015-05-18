/*
 *  cp_log_cmn.cpp - Common functions for the CP log and dump program.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "cp_log_cmn.h"

int get_timezone()
{
	time_t t1;
	struct tm* p;
	struct tm local_tm;
	struct tm gm_tm;

	t1 = time(0);
	if (static_cast<time_t>(-1) == t1) {
		return 0;
	}
	p = gmtime_r(&t1, &gm_tm);
	if (!p) {
		return 0;
	}
	p = localtime_r(&t1, &local_tm);
	if (!p) {
		return 0;
	}

	int tz = local_tm.tm_hour - gm_tm.tm_hour;
	if (tz > 12) {
		tz -= 24;
	} else if (tz < -12) {
		tz += 24;
	}

	return tz;
}

static int copy_file(int src_fd, int dest_fd)
{
	int err = 0;
	static const size_t FILE_COPY_BUF_SIZE = (1024 * 32);
	static uint8_t s_copy_buf[FILE_COPY_BUF_SIZE];

	while (true) {
		ssize_t n = read(src_fd, s_copy_buf, FILE_COPY_BUF_SIZE);
		if (-1 == n) {
			err = -1;
			break;
		}
		if (!n) {  // End of file
			break;
		}
		size_t to_wr = n;
		n = write(dest_fd, s_copy_buf, to_wr);
		if (-1 == n || static_cast<size_t>(n) != to_wr) {
			err = -1;
			break;
		}
	}

	return err;
}

int copy_file(const char* src, const char* dest)
{
	// Open the source and the destination file
	int src_fd;
	int dest_fd;

	src_fd = open(src, O_RDONLY);
	if (-1 == src_fd) {
		err_log("open source file %s failed", src);
		return -1;
	}

	dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC,
		       S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (-1 == dest_fd) {
		close(src_fd);
		err_log("open dest file %s failed", dest);
		return -1;
	}

	int err = copy_file(src_fd, dest_fd);

	close(dest_fd);
	close(src_fd);

	return err;
}


