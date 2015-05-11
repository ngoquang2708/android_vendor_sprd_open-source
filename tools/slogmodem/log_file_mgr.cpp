/*
 *  log_file_mgr.cpp - The log file manager class for each CP.
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
#include <dirent.h>
#include <unistd.h>
#include <poll.h>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <climits>

#include "log_file_mgr.h"
#include "log_pipe_hdl.h"
#include "log_ctrl.h"
#include "log_stat.h"

LogFileManager::LogFileManager(const LogString& cp_name,
			       LogPipeHandler* handler)
	:m_handler {handler},
	 m_cp_name (cp_name),
	 m_size_limit {1024 * 1024 * 5},
	 m_cur_file {0},
	 m_cur_log {0},
	 m_size {0},
	 m_log_stat {0}
{
}

LogFileManager::~LogFileManager()
{
	clear_ptr_container(m_log_list);
	close_log();
}

int LogFileManager::change_dir(const LogString& timed_d, LogStat* log_stat)
{
	info_log("new dir %s, LogStat %p",
		 ls2cstring(timed_d), log_stat);
	LogString log_dir = timed_d + "/" + m_cp_name;

	if (log_dir == m_log_dir) {  // The dir has not changed
		return 0;
	}

	close_log();

	m_size = 0;
	LogList<LogFile*>::iterator it;

	for (it = m_log_list.begin(); it != m_log_list.end(); ++it) {
		delete (*it);
	}
	m_log_list.clear();

	m_log_dir = log_dir;
	int ret = mkdir(ls2cstring(m_log_dir),
			S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	if (-1 == ret && EEXIST == errno) {
		ret = 0;
	}
	m_cur_log_path.clear();

	m_log_stat = log_stat;

	return ret;
}

bool LogFileManager::gen_log_name(LogString& log_name,
				  const LogString& modem_name)
{
	char tm_str[32];
	struct tm* p;
	struct tm tm1;
	time_t t1;

	t1 = time(0);
	if (static_cast<time_t>(-1) == t1) {
		return false;
	}
	p = localtime_r(&t1, &tm1);
	if (!p) {
		return false;
	}
	snprintf(tm_str, 32, "-%02d-%02d-%02d.log",
		 tm1.tm_hour,
		 tm1.tm_min,
		 tm1.tm_sec);
	log_name = "0-";
	log_name += (modem_name + tm_str);

	return true;
}

int LogFileManager::create_log()
{
	LogString log_name;
	int ret = -1;

	gen_log_name(log_name, m_cp_name);

	LogString full_log_path = m_log_dir + "/" + log_name;

	m_cur_log = fopen(ls2cstring(full_log_path), "w+b");
	if (m_cur_log) {
		setvbuf(m_cur_log, 0, _IOFBF, LOG_FILE_WRITE_BUF_SIZE);
		save_timestamp();

		m_cur_log_path = full_log_path;
		m_cur_file = new LogFile;
		m_cur_file->name = log_name;
		m_cur_file->size = 0;
		m_log_list.push_back(m_cur_file);
		ret = 0;
	}

	return ret;
}

void LogFileManager::close_log()
{
	if (m_cur_log) {
		fclose(m_cur_log);
		m_cur_log = 0;
		m_cur_file = 0;
	}
}

int LogFileManager::check_file_exist()
{
	int ret = 0;

	if (access(ls2cstring(m_cur_log_path), F_OK)) {
		info_log("CP %s log %s removed",
			 ls2cstring(m_cp_name),
			 ls2cstring(m_cur_log_path));
		// Close the current log file.
		close_log();

		// Check the current log dir
		if (access(ls2cstring(m_log_dir), R_OK | W_OK | X_OK)) {
			// The CP log dir does not exist.
			// Clear all log file sizes
			LogList<LogFile*>::iterator it;
			size_t dir_size = 0;

			for (it = m_log_list.begin(); it != m_log_list.end();
			     ++it) {
				LogFile* pf = *it;
				dir_size += pf->size;
				delete pf;
			}
			m_log_list.clear();
			m_log_stat->dec(dir_size);
			m_size = 0;

			// LogController shall check the timed directory.
			// If the timed directory is removed, LogController shall
			// change log file for all CPs.
			// If the timed directory is not removed,
			// the LogPipeHandler shall change its log file.
			LogController* ctrl = m_handler->controller();
			ret = ctrl->check_dir_exist();
			if (!ret) {  // Timed directory not changed
				// Create the CP log dir
				ret = change_dir(ctrl->file_manager()->get_cp_par_dir(),
						 m_log_stat);
				if (ret) {
					warn_log("CP %s create %s failed",
						 ls2cstring(m_cp_name),
						 ls2cstring(m_log_dir));
				} else {
					ret = create_log();
					if (!ret) {
						ret = 1;
					}
				}
			}
		} else {  // The log dir exists, but the current file is lost.
			LogFile* pf = rm_last(m_log_list);
			m_size -= pf->size;
			m_log_stat->dec(pf->size);
			delete pf;
			// Create the log file
			ret = create_log();
			if (!ret) {
				ret = 1;
			}
		}
	}

	return ret;
}

bool LogFileManager::save_timestamp()
{
	modem_timestamp ts;
	int ts_file;

	// Open the CP time stamp FIFO
	ts_file = open(ls2cstring(m_timestamp_file), O_RDWR | O_NONBLOCK);
	if (-1 == ts_file) {
		return false;
	}

	struct pollfd pol_fifo;

	pol_fifo.fd = ts_file;
	pol_fifo.events = POLLIN;
	pol_fifo.revents = 0;
	int ret = poll(&pol_fifo, 1, 1000);
	ssize_t n = 0;

	if (ret > 0 && (pol_fifo.revents & POLLIN)) {
		n = read(ts_file,
			 (uint8_t*)&ts + offsetof(modem_timestamp, tv),
			 12);
	}
	close(ts_file);
	if (n < 12) {
		return false;
	}

	ts.magic_number = 0x12345678;
	int tz = get_timezone();
	ts.tv.tv_sec += (3600 * tz);
	return 1 == fwrite(&ts, sizeof ts, 1, m_cur_log);
}

int LogFileManager::write_log(const uint8_t* buf, size_t len,
			      FILE* pf, size_t& written)
{
	const uint8_t* start = buf;
	size_t to_write = len;
	int err;

	while (to_write) {
		size_t n = fwrite(start, 1, to_write, pf);
		if (!n) {
			// Error
			break;
		}
		to_write -= n;
		start += n;
	}

	written = len - to_write;
	if (!written) {
		err = WL_ERROR;
	} else if (written != len) {
		err = WL_PARTIAL;
	} else {
		err = WL_SUCCESS;
	}

	return err;
}

int LogFileManager::write(const void* data, size_t len)
{
	LogController* ctrl = m_handler->controller();

	ctrl->check_storage();

	check_file_exist();

	if (!m_cur_log) {
		return -1;
	}

	int err;
	size_t written;

	err = write_log(static_cast<const uint8_t*>(data), len,
			m_cur_log, written);
	if (WL_ERROR != err) {
		err = 0;

		m_cur_file->size += written;
		if (m_cur_file->size >= m_size_limit) {
			fclose(m_cur_log);
			m_cur_log = 0;
			rotate();
			err = create_log();
			if (!err) {
				err = 1;
			}
		}
		m_log_stat->inc(this, written);
	} else {
		// Error occurs when writing.
		err_log("modem %s save error, %u lost",
			ls2cstring(m_cp_name),
			static_cast<unsigned>(len));
	}

	return err;
}

int LogFileManager::rotate()
{
	LogString old_path;
	LogString new_path;
	LogString new_name;
	LogList<LogFile*>::iterator it;

	info_log("rotate");

	for (it = m_log_list.begin(); it != m_log_list.end(); ++it) {
		LogFile* pf = *it;
		old_path = m_log_dir + "/" + pf->name;

		int num;
		int len;
		const char* old_name = ls2cstring(pf->name);
		int err = parse_file_name(old_name,
					  num, len);

		if (!err) {
			++num;
			char str_num[32];
			snprintf(str_num, 32, "%d", num);
			new_name = str_num;
			new_name += (old_name + len);
			new_path = m_log_dir + "/" + new_name;
			rename(ls2cstring(old_path), ls2cstring(new_path));
			pf->name = new_name;
		}
	}

	return 0;
}

int LogFileManager::parse_file_name(const char* name,
				    int& num,
				    int& len)
{
	if (!isdigit(name[0])) {
		return -1;
	}

	const char* endp;
	unsigned long n;

	n = strtoul(name, const_cast<char**>(&endp), 0);
	if (ULONG_MAX == n && ERANGE == errno) {
		return -1;
	}

	num = static_cast<int>(n);
	len = static_cast<int>(endp - name);
	return 0;
}

size_t LogFileManager::shrink(size_t dec_num)
{
	LogList<LogFile*>::iterator it;
	size_t dec_len = 0;

	while (dec_len < dec_num) {
		if (m_log_list.empty()) {  // No more log file
			break;
		}
		LogFile* p = *m_log_list.begin();
		if (p == m_cur_file) {  // Last file, can not delete
			break;
		}
		m_log_list.erase(m_log_list.begin());
		LogString full_log_path = m_log_dir + "/" + p->name;
		remove(ls2cstring(full_log_path));
		dec_len += p->size;
		delete p;
	}

	return dec_len;
}
