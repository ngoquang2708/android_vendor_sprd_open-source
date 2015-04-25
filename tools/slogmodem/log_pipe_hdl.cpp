/*
 *  log_pipe_hdl.cpp - The CP log and dump handler class.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-3-2 Zhang Ziyi
 *  Initial version.
 */
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <cstdlib>

#include "log_pipe_hdl.h"
#include "multiplexer.h"
#include "log_ctrl.h"

uint8_t LogPipeHandler::log_buffer[LogPipeHandler::LOG_BUFFER_SIZE];

LogPipeHandler::LogPipeHandler(LogController* ctrl,
			       Multiplexer* multi,
			       const LogConfig::ConfigEntry* conf)
	:FdHandler {-1, ctrl, multi},
	 m_enable {false},
	 m_modem_name (conf->modem_name),
	 m_type {conf->type},
	 m_cp_state {CWS_WORKING},
	 m_dump_fd {-1},
	 m_log_mgr {m_modem_name, this}
{
	const char* fifo = "";

	switch (conf->type) {
	case CT_WCDMA:
		fifo = W_TIME;
		break;
	case CT_TD:
		fifo = TD_TIME;
		break;
	case CT_3MODE:
	case CT_4MODE:
	case CT_5MODE:
		fifo = L_TIME;
		break;
	default:
		break;
	}

	m_log_mgr.set_timestamp_fifo(fifo);
}

LogPipeHandler::~LogPipeHandler()
{
	if (m_dump_fd >= 0 && m_dump_fd != m_fd) {
		close(m_dump_fd);
	}
	m_dump_fd = -1;
}

void LogPipeHandler::set_log_limit(size_t sz)
{
	m_log_mgr.set_log_limit(sz);
}

int LogPipeHandler::start()
{
	m_enable = true;

	if (CWS_WORKING == m_cp_state) {
		int err = open_devices();

		if (err < 0) {
			err_log("slogcp: open %s error",
				ls2cstring(m_modem_name));
			multiplexer()->add_check_event(reopen_log_dev, this);
		} else {
			add_events(POLLIN);
		}
	}

	return 0;
}

int LogPipeHandler::stop()
{
	m_enable = false;
	if (m_fd >= 0) {
		del_events(POLLIN);
	}
	close_devices();

	return 0;
}

void LogPipeHandler::close_devices()
{
	if (m_dump_fd != m_fd) {
		close(m_dump_fd);
	}
	close(m_fd);
	m_fd = -1;
	m_dump_fd = -1;
}

void LogPipeHandler::process(int events)
{
	// The log device file readable, read it

	ssize_t nr = read(m_fd, log_buffer, LOG_BUFFER_SIZE);
	if (-1 == nr) {
		if (EAGAIN == errno || EINTR == errno) {
			return;
		}
		// Other errors: try to reopen the device
		del_events(POLLIN);
		close_devices();
		int err = open_devices();
		if (!err) {  // Success
			add_events(POLLIN);
		} else {  // Failure: arrange a check callback
			multiplexer()->add_check_event(reopen_log_dev,
						       this);
			return;
		}
	}

	// There is a bug one CP2 log device: poll reports the device
	// is readable, but read returns 0.
	if (!nr) {
		err_log("log device bug: read returns 0");
		return;
	}

	int err = m_log_mgr.write(log_buffer, nr);
	if (err < 0) {
		err_log("CP %s save log error", ls2cstring(m_modem_name));
	}
}

void LogPipeHandler::reopen_log_dev(void* param)
{
	LogPipeHandler* log_pipe = static_cast<LogPipeHandler*>(param);

	if (log_pipe->m_enable && CWS_WORKING == log_pipe->m_cp_state) {
		int err = log_pipe->open_devices();
		if (!err) {
			log_pipe->add_events(POLLIN);
		} else {
			err_log("slogcp: reopen %s error",
				ls2cstring(log_pipe->m_modem_name));
			log_pipe->multiplexer()->add_check_event(reopen_log_dev, param);
		}
	}
}

int LogPipeHandler::change_log_file(const LogString& par_dir,
				    LogStat* log_stat)
{
	if (m_log_mgr.change_dir(par_dir, log_stat)) {
		return -1;
	}

	save_version();

	return m_log_mgr.create_log();
}

bool LogPipeHandler::save_version()
{
	char version[PROPERTY_VALUE_MAX];

	property_get(MODEM_VERSION, version, "");
	if (!version[0]) {
		return false;
	}

	bool ret = false;
	LogString ver_file = m_log_mgr.dir() + "/" + m_modem_name + ".version";
	FILE* pf = fopen(ls2cstring(ver_file), "w+");
	if (pf) {
		size_t n = fwrite(version, strlen(version), 1, pf);
		fclose(pf);
		if (n) {
			ret = true;
		} else {
			unlink(ls2cstring(ver_file));
		}
	}

	return ret;
}

int LogPipeHandler::open_dump_file()
{
	char log_name[64];
	time_t t;
	struct tm lt;

	t = time(0);
	if (static_cast<time_t>(-1) == t || !localtime_r(&t, &lt)) {
		return -1;
	}
	snprintf(log_name, 64, "%04d-%02d-%02d_%02d-%02d-%02d.dmp",
		 lt.tm_year + 1900,
		 lt.tm_mon + 1,
		 lt.tm_mday,
		 lt.tm_hour,
		 lt.tm_min,
		 lt.tm_sec);
	LogString fn = m_log_mgr.dir() + "/" + m_modem_name + "_memory_" + log_name;
	return open(ls2cstring(fn), O_WRONLY | O_CREAT | O_TRUNC,
		    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
}

int LogPipeHandler::save_dump()
{
	if (m_dump_fd < 0) {
		return -1;
	}

	int out_fd = open_dump_file();
	if (-1 == out_fd) {
		return -1;
	}

	uint8_t dump_cmd[2] = { 0x33, 0xa };

	if (write(m_dump_fd, dump_cmd, 2) < 2) {
		return -1;
	}

	int err = save_dump_ipc(out_fd);
	if (err) {  // Comminication with the CP failed
		// Discard data read
		if (-1 == ftruncate(out_fd, 0)) {
			close(out_fd);
			return -1;
		}
		lseek(out_fd, 0, SEEK_SET);

		err = save_dump_proc(out_fd);
	}

	return err;
}

int LogPipeHandler::save_dump_ipc(int fd)
{
	pollfd pol_dump;
	int to;
	int err = -1;
	int read_num = 0;

	pol_dump.fd = m_dump_fd;
	pol_dump.events = POLLIN;
	pol_dump.revents = 0;
	to = 3;

	while (true) {
		int n = poll(&pol_dump, 1, to);
		if (-1 == n) {
			if (EINTR != errno) {
				break;
			}
			continue;
		}
		if (!n) {  // Timeout
			if (read_num >= 10) {
				err = 0;
			}
			break;
		}
		if (pol_dump.revents & (POLLERR | POLLHUP)) {
			// Error
			break;
		}
		if (!(pol_dump.revents & POLLIN)) {
			continue;
		}

		ssize_t read_len = read(m_dump_fd,
					log_buffer, LOG_BUFFER_SIZE);
		if (-1 == read_len) {
			if (EINTR == errno || EAGAIN == errno) {
				continue;
			}
			break;
		}
		if (!read_len) {
			break;
		}
		write(fd, log_buffer, read_len);
		++read_num;
	}

	return err;
}

int LogPipeHandler::save_dump_proc(int fd)
{
	const char* fname;

	switch (m_type) {
	case CT_WCDMA:
		fname = "/proc/cpw/mem";
		break;
	case CT_TD:
		fname = "/proc/cpt/mem";
		break;
	case CT_3MODE:
	case CT_4MODE:
	case CT_5MODE:
		fname = "/proc/cptl/mem";
		break;
	default:  // CT_WCN
		fname = "/proc/cpwcn/mem";
		break;
	}

	return FileManager::copy_file(fname, fd);
}

void LogPipeHandler::open_on_alive()
{
	if (m_enable) {
		if (!open_devices()) {
			add_events(POLLIN);
		}

		m_cp_state = CWS_WORKING;
	}
}

void LogPipeHandler::close_on_assert()
{
	if (m_fd >= 0) {
		del_events(POLLIN);
	}
	close_devices();

	m_cp_state = CWS_NOT_WORKING;
}

void LogPipeHandler::process_assert(bool save_md /*= true*/)
{
	if (CWS_NOT_WORKING == m_cp_state) {
		return;
	}
	m_cp_state = CWS_NOT_WORKING;

	if (will_be_reset()) {
		return;
	}

	time_t t = time(0);

	if (static_cast<time_t>(-1) == t) {
		return;
	}

	struct tm lt;

	if (!localtime_r(&t, &lt)) {
		return;
	}

	LogController::save_sipc_dump(m_log_mgr.dir(), lt);

	if (save_md) {  // Mini dump shall be saved
		LogController::save_mini_dump(m_log_mgr.dir(), lt);
	}

	// Save MODEM dump
	system("am broadcast -a slogui.intent.action.DUMP_START");
	save_dump();
	system("am broadcast -a slogui.intent.action.DUMP_END");

	close_on_assert();
}

bool LogPipeHandler::will_be_reset() const
{
	char reset[PROPERTY_VALUE_MAX];
	long n;
	char* endp;

	property_get(MODEMRESET_PROPERTY, reset, "");
	n = strtoul(reset, &endp, 0);
	return 1 == n ? true : false;
}
