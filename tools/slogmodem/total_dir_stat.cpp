/*
 *  total_dir_stat.cpp - one timed directory statistics.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-4-23 Zhang Ziyi
 *  Initial version.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include "total_dir_stat.h"

TotalDirStat::TotalDirStat(const char* path)
	:m_path(path),
	 m_cur_size {0}
{
}

int TotalDirStat::stat_size()
{
	DIR* dirp = opendir(ls2cstring(m_path));

	if (!dirp) {
		return -1;
	}

	struct dirent* dep;
	LogList<LogString> dir_list;

	while (true) {
		dep = readdir(dirp);
		if (!dep) {
			break;
		}
		if (!strcmp(dep->d_name, ".") ||
		    !strcmp(dep->d_name, "..")) {
			continue;
		}

		LogString sub_path = m_path + "/" + dep->d_name;
		struct stat file_stat;

		if (!stat(ls2cstring(sub_path), &file_stat) &&
		    S_ISDIR(file_stat.st_mode)) {
			dir_list.push_back(sub_path);
		}
	}

	closedir(dirp);

	int ret = 0;
	uint64_t file_size;

	for (auto it = dir_list.begin(); it != dir_list.end(); ++it) {
		if (compute_files_size(*it, file_size)) {
			ret = -1;
			break;
		}
		m_cur_size += file_size;
	}

	return ret;
}

int TotalDirStat::compute_files_size(const LogString& path,
				     uint64_t& file_size)
{
	DIR* dirp = opendir(ls2cstring(path));

	if (!dirp) {
		return -1;
	}

	file_size = 0;

	while (true) {
		struct dirent* dep = readdir(dirp);

		if (!dep) {
			break;
		}

		LogString fpath = path + "/" + dep->d_name;
		struct stat file_stat;

		if (!stat(ls2cstring(fpath), &file_stat) &&
		    S_ISREG(file_stat.st_mode)) {
			file_size += static_cast<uint64_t>(file_stat.st_size);
		}
	}

	closedir(dirp);

	return 0;
}

bool operator < (const TotalDirStat& t1, const TotalDirStat& t2)
{
	return t1.dir() < t2.dir();
}
