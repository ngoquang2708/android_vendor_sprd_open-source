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
#include "stor_mgr.h"
#include "cp_stor.h"
#include "cp_dir.h"

uint8_t LogPipeHandler::log_buffer[LogPipeHandler::LOG_BUFFER_SIZE];

LogPipeHandler::LogPipeHandler(LogController* ctrl,
			       Multiplexer* multi,
			       const LogConfig::ConfigEntry* conf,
			       StorageManager& stor_mgr)
	:FdHandler(-1, ctrl, multi),
	 m_enable(false),
	 m_modem_name(conf->modem_name),
	 m_type(conf->type),
	 m_cp_state(CWS_WORKING),
	 m_dump_fd(-1),
	 m_stor_mgr(stor_mgr),
	 m_storage(0)
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

	m_ts_fifo = fifo;
}

LogPipeHandler::~LogPipeHandler()
{
	if (m_storage) {
		m_stor_mgr.delete_storage(m_storage);
	}

	if (m_dump_fd >= 0 && m_dump_fd != m_fd) {
		close(m_dump_fd);
	}
	m_dump_fd = -1;
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

	// There is a bug in CP2 log device driver: poll reports the device
	// is readable, but read returns 0.
	if (!nr) {
		err_log("log device driver bug: read returns 0");
		return;
	}

	if (!m_storage) {
		m_storage = m_stor_mgr.create_storage(*this);
		if (!m_storage) {
			err_log("create %s CpStorage failed",
				ls2cstring(m_modem_name));
			return;
		}
		m_storage->set_new_log_callback(new_log_callback);
		// The first write: save the version
		save_version();
	}

	int err = m_storage->write(log_buffer, nr);
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

bool LogPipeHandler::save_version()
{
	char version[PROPERTY_VALUE_MAX];

	property_get(MODEM_VERSION, version, "");
	if (!version[0]) {
		return false;
	}

	bool ret = false;
	LogString ver_file = m_modem_name + ".version";
	LogFile* logf = m_storage->create_file(ver_file, LogFile::LT_VERSION);

	if (logf) {
		size_t len = strlen(version);
		ssize_t n = logf->write(version, len);
		logf->close();
		if (static_cast<size_t>(n) == len) {
			ret = true;
		}
	}

	return ret;
}

LogFile* LogPipeHandler::open_dump_file(const struct tm& lt)
{
	char log_name[64];

	snprintf(log_name, 64, "_memory_%04d-%02d-%02d_%02d-%02d-%02d.dmp",
		 lt.tm_year + 1900,
		 lt.tm_mon + 1,
		 lt.tm_mday,
		 lt.tm_hour,
		 lt.tm_min,
		 lt.tm_sec);
	LogString dump_file_name = m_modem_name + log_name;
	LogFile* f = m_storage->create_file(dump_file_name,
					    LogFile::LT_DUMP);
	if (!f) {
		err_log("open dump file %s failed", log_name);
	}
	return f;
}

int LogPipeHandler::save_dump(const struct tm& lt)
{
	bool close_dump = false;
	int err = -1;
	uint8_t dump_cmd[2] = { 0x33, 0xa };
	ssize_t nwr;

	// When log is not enabled, the dump device is not opened.
	if (m_dump_fd < 0) {
		if (open_dump_device() < 0) {
			err_log("open dump device failed");
			return -1;
		}
		close_dump = true;
	}

	LogFile* dumpf = open_dump_file(lt);
	if (!dumpf) {
		err_log("open dump file failed");
		goto cl_dump_dev;
	}

	nwr = write(m_dump_fd, dump_cmd, 2); 
	if (nwr < 2) {
		err_log("send dump command failed %d",
			static_cast<int>(nwr));
		goto cl_dump_dev;
	}

	err = save_dump_ipc(dumpf);
	if (err) {  // Communication with the CP failed
		err_log("save CP dump via SIPC failed, trying /proc file ...");
		// Discard data read
		if (dumpf->discard()) {
			dumpf->dir()->remove(dumpf);
			goto cl_dump_dev;
		}

		err = save_dump_proc(dumpf);
	}

cl_dump_dev:
	if (close_dump) {
		close(m_dump_fd);
		m_dump_fd = -1;
	}
	return err;
}

int LogPipeHandler::save_dump_ipc(LogFile* dumpf)
{
	pollfd pol_dump;
	int to;
	int err = -1;
	int read_num = 0;

	pol_dump.fd = m_dump_fd;
	pol_dump.events = POLLIN;
	pol_dump.revents = 0;
	to = 3000;

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
			err = 0;
			break;
		}
		ssize_t nwr = dumpf->write(log_buffer, read_len);
		if (nwr != read_len) {
			err_log("not all data writen to dump file");
			break;
		}
		++read_num;
	}

	return err;
}

int LogPipeHandler::save_dump_proc(LogFile* dumpf)
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

	return dumpf->copy(fname);
}

void LogPipeHandler::open_on_alive()
{
	if (m_enable) {
		if (!open_devices()) {
			add_events(POLLIN);
		}
	}
	m_cp_state = CWS_WORKING;
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

	bool will_reset_cp = will_be_reset();
	time_t t;
	struct tm lt;

	// If the CP will be reset, save mini dump if requested by the
	// caller.
	if (will_reset_cp) {
		if (save_md) {
			if (!m_storage) {
				m_storage = m_stor_mgr.create_storage(*this);
				if (!m_storage) {
					return;
				}
				m_storage->set_new_log_callback(new_log_callback);
			}

			t = time(0);
			if (static_cast<time_t>(-1) == t ||
			    !localtime_r(&t, &lt)) {
				return;
			}

			LogController::save_mini_dump(m_storage, lt);
		}
		return;
	}

	// CP will not be reset: save all dumps as requested.
	if (!m_storage) {
		m_storage = m_stor_mgr.create_storage(*this);
		if (!m_storage) {
			return;
		}
		m_storage->set_new_log_callback(new_log_callback);
	}

	t = time(0);
	if (static_cast<time_t>(-1) == t || !localtime_r(&t, &lt)) {
		return;
	}

	LogController::save_sipc_dump(m_storage, lt);

	if (save_md) {  // Mini dump shall be saved
		LogController::save_mini_dump(m_storage, lt);
	}

	// Save MODEM dump
	system("am broadcast -a slogui.intent.action.DUMP_START");
	save_dump(lt);
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

void LogPipeHandler::close_dump_device()
{
	close(m_dump_fd);
	m_dump_fd = -1;
}

void LogPipeHandler::new_log_callback(LogPipeHandler* cp, LogFile* f)
{
	cp->save_timestamp(f);
}

bool LogPipeHandler::save_timestamp(LogFile* f)
{
	modem_timestamp ts;
	int ts_file;

	// Open the CP time stamp FIFO
	ts_file = open(ls2cstring(m_ts_fifo), O_RDWR | O_NONBLOCK);
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
	n = f->write(&ts, sizeof ts);
	return (static_cast<size_t>(n) == sizeof ts);
}
