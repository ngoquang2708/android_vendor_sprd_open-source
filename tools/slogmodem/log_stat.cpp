/*
 *  log_stat.cpp - Log statistics implementation.
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
#include <stdlib.h>
#include <cstring>
#include <cassert>
#include "log_stat.h"
#include "log_file_mgr.h"

LogStat::LogStat()
	:m_max_size {0},
	 m_overwrite {false},
	 m_cur_dir {0}
{
}

LogStat::~LogStat()
{
	delete m_cur_dir;

	clear_ptr_container(m_history);
}

int LogStat::init(const char* top_dir)
{
	clear_ptr_container(m_history);
	delete m_cur_dir;
	m_cur_dir = 0;
	m_cur_size = 0;

	m_top_dir = top_dir;

	DIR* dirp = opendir(top_dir);
	if (!dirp) {
		return -1;
	}

	struct dirent* dep;

	while (true) {
		dep = readdir(dirp);
		if (!dep) {
			break;
		}

		if (!strcmp(dep->d_name, ".") ||
		    !strcmp(dep->d_name, "..")) {
			continue;
		}

		struct stat dir_stat;
		LogString dir_path = m_top_dir + "/" + dep->d_name;

		if (!stat(ls2cstring(dir_path), &dir_stat) &&
		    S_ISDIR(dir_stat.st_mode)) {
			TotalDirStat* total = new TotalDirStat(ls2cstring(dir_path));
			if (!total->stat_size()) {
				insert_ascending(m_history, total);
				m_cur_size += total->size();
			} else {
				delete total;
			}
		}
	}

	closedir(dirp);

	return 0;
}

void LogStat::change_dir(const LogString& dir)
{
	info_log("change to %s", ls2cstring(dir));

	assert(!m_cur_dir);

	m_cur_dir = new TotalDirStat(ls2cstring(dir));
}

void LogStat::end_stat()
{
	info_log("stop stat for %s", ls2cstring(m_cur_dir->dir()));

	assert(m_cur_dir);

	m_history.push_back(m_cur_dir);
	m_cur_dir = 0;
}

int LogStat::inc(LogFileManager* log_mgr, size_t sz)
{
	if (!m_max_size) {
		return 0;
	}

	assert(m_cur_dir);
	m_cur_dir->inc(sz);
	m_cur_size += sz;

	int ret = 0;
	if (m_cur_size > m_max_size) {
		if (m_overwrite) {
			ret = shrink(log_mgr);
		} else {
			ret = -1;
		}
	}

	return ret;
}

int LogStat::shrink(LogFileManager* log_mgr)
{
	// First try to remove history
	while (!m_history.empty()) {
		TotalDirStat* p = (*m_history.begin());
		m_history.erase(m_history.begin());
		LogString s;
		str_assign(s, "rm -fr ", 7);
		s += p->dir();
		system(ls2cstring(s));
		m_cur_size -= p->size();
		delete p;
		if (m_cur_size < m_max_size) {
			break;
		}
	}

	int ret = 0;
	if (m_cur_size > m_max_size) {
		// Now we can only delete files in current dir.
		size_t to_dec = static_cast<size_t>(m_cur_size - m_max_size);
		size_t del_num = log_mgr->shrink(to_dec);
		m_cur_size -= del_num;
		m_cur_dir->dec(del_num);

		if (m_cur_size > m_max_size) {
			ret = -1;
			err_log("still too large after shrink");
		}
	}

	return ret;
}

void LogStat::dec(size_t sz)
{
	m_cur_size -= sz;
	m_cur_dir->dec(sz);
}

void LogStat::insert_ascending(LogList<TotalDirStat*>& l,
			       TotalDirStat* total)
{
	LogList<TotalDirStat*>::iterator it;

	for (it = l.begin(); it != l.end(); ++it) {
		if (*total < **it) {
			break;
		}
	}
	l.insert(it, total);
}
