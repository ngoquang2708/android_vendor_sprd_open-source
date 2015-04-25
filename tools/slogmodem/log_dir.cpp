/*
 *  log_dir.cpp - Log directory manager implementation.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-4-22 Zhang Ziyi
 *  Initial version.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include "log_dir.h"

LogDir::LogDir(const LogString& path)
	:m_dir_path(path)
{
}

int LogDir::stat_size()
{
	DIR* dirp = opendir(ls2cstring(m_dir_path));

	if (!dirp) {
		return -1;
	}

	struct dirent* entp;
	struct stat file_stat;
	LogString fpath;
	int ret = 0;
	off_t total_size = 0;

	while (true) {
		entp = readdir(dirp);
		if (!entp) {
			break;
		}
		fpath = m_dir_path + "/" + entp->d_name;
		ret = stat(ls2cstring(fpath), &file_stat);
		if (-1 == ret) {
			break;
		}
		if (S_ISREG(file_stat.st_mode)) {
			total_size += file_stat.st_size;
		}
	}
	closedir(dirp);

	if (!ret) {
		m_size = static_cast<uint64_t>(total_size);
	}
	return ret;
}
