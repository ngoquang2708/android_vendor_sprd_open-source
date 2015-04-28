/*
 *  file_mgr.cpp - The storage media manager class implementation.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#include <stdlib.h>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#ifdef HOST_TEST_
	#include "prop_test.h"
#else
	#include <cutils/properties.h>
#endif
#include "file_mgr.h"
#include "log_stat.h"

uint8_t FileManager::s_copy_buf[FileManager::FILE_COPY_BUF_SIZE];

FileManager::FileManager(LogController* ctrl)
	:m_log_ctrl(ctrl),
	 m_save_sd(false),
	 m_log_media(LM_NONE)
{
}

int FileManager::init(LogConfig::StoragePosition pos,
		      const LogString& top_dir)
{
	m_save_sd = (LogConfig::SP_DATA != pos);
	if (m_save_sd) {
		m_sd_root = top_dir;
	}

	// If the SD card is present, create the top directory.
	int ret = 0;
	bool has_sd = get_sd_state();
	if (has_sd) {
		// Create modem_log dir
		LogString sd_dir = m_sd_root + "/modem_log";
		const char* str_sd_dir = ls2cstring(sd_dir);
		if (mkdir(str_sd_dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) &&
		    EEXIST != errno) {
			ret = -1;
		} else {
			ret = 1;
		}
	}

	return ret;
}

void FileManager::sd_root(LogString& sd_top) const
{
	sd_top = m_sd_root + "/modem_log";
}

bool FileManager::get_sd_state()
{
	char val[PROPERTY_VALUE_MAX];

	property_get("persist.storage.type", val, "");
	if (!val[0]) {
		return false;
	}

	unsigned long type;
	char* endp;

	type = strtoul(val, &endp, 0);
	if (1 != type) {
		return false;
	}

	property_get("init.svc.fuse_sdcard0", val, "");

	return !strcmp(val, "running");
}

bool FileManager::create_timed_dir(const LogString& top_dir)
{
	time_t t = time(0);
	tm calt;
	tm* cal_p;

	cal_p = localtime_r(&t, &calt);
	if (!cal_p) {
		return false;
	}

	char t_str[32];

	snprintf(t_str, 32, "/%04d-%02d-%02d-%02d-%02d-%02d",
		 calt.tm_year + 1900,
		 calt.tm_mon + 1,
		 calt.tm_mday,
		 calt.tm_hour,
		 calt.tm_min,
		 calt.tm_sec);
	LogString modem_dir = top_dir + "/modem_log";
	int err = mkdir(ls2cstring(modem_dir),
			S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	if (-1 == err && EEXIST != errno) {
		return false;
	}

	modem_dir += t_str;
	err = mkdir(ls2cstring(modem_dir),
		    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	bool is_success = false;
	if (!err || EEXIST == errno) {
		is_success = true;
		m_cur_timed_dir = modem_dir;
	}
	return is_success;
}

bool FileManager::check_media(LogStat& sd_stat)
{
	bool changed = false;

	if (m_save_sd) {  // Save to SD card
		bool has_sd = get_sd_state();
		if (has_sd) {  // SD card is present
			if (str_empty(sd_stat.top_dir())) {
				LogString modem_dir = m_sd_root + "/modem_log";
				const char* str_modem = ls2cstring(modem_dir);
				if (mkdir(str_modem, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) &&
				    EEXIST != errno) {
					err_log("create modem_log dir failed");
					return false;
				}
				if (sd_stat.init(str_modem)) {
					err_log("init SD log stat failed");
					return false;
				}
			}

			if (LM_NONE == m_log_media) {
				if (create_timed_dir(m_sd_root)) {
					m_root_dir = m_sd_root;
					m_log_media = LM_EXTERNAL;
					changed = true;
				} else if (create_timed_dir(LogString("/data"))) {
					m_root_dir = "/data";
					m_log_media = LM_INTERNAL;
					changed = true;
				}
			} else if (LM_INTERNAL == m_log_media) {
				if (create_timed_dir(m_sd_root)) {
					m_root_dir = m_sd_root;
					m_log_media = LM_EXTERNAL;
					changed = true;
				}
			}
		} else {  // SD card is absent
			if (LM_INTERNAL != m_log_media) {
				// Switch to internal storage
				if (create_timed_dir(LogString("/data"))) {
					m_root_dir = "/data";
					m_log_media = LM_INTERNAL;
					changed = true;
				} else {
					m_log_media = LM_NONE;
				}
			}
		}
	} else {  // Save to internal storage
		if (LM_NONE == m_log_media) {
			if (create_timed_dir(LogString("/data"))) {
				m_root_dir = "/data";
				m_log_media = LM_INTERNAL;
				changed = true;
			}
		}
	}

	return changed;
}

int FileManager::check_dir_exist()
{
	int ret = 0;

	if (access(ls2cstring(m_cur_timed_dir), F_OK)) {
		if (create_timed_dir(m_root_dir)) {
			ret = 1;
		} else {
			ret = -1;
		}
	}
	return ret;
}

int FileManager::copy_file(const char* src, const char* dest)
{
	// Open the source and the destination file
	int src_fd;
	int dest_fd;

	src_fd = open(src, O_RDONLY);
	if (-1 == src_fd) {
		return -1;
	}

	dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC,
		       S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (-1 == dest_fd) {
		close(src_fd);
		return -1;
	}

	int err = copy_file(src_fd, dest_fd);

	close(dest_fd);
	close(src_fd);

	return err;
}

int FileManager::copy_file(const char* src, int dest_fd)
{
	// Open the source and the destination file
	int src_fd;

	src_fd = open(src, O_RDONLY);
	if (-1 == src_fd) {
		return -1;
	}

	int err = copy_file(src_fd, dest_fd);
	close(src_fd);
	return err;
}

int FileManager::copy_file(int src_fd, int dest_fd)
{
	int err = 0;
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

int FileManager::clear()
{
	system("rm -fr /data/modem_log/*");
	if (!str_empty(m_sd_root)) {
		LogString cmd;
		
		str_assign(cmd, "rm -fr ", 7);

		cmd += m_sd_root;
		cmd += "/*";
		system(ls2cstring(cmd));
	}

	return 0;
}
