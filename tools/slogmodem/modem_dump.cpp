/*
 *  modem_dump.cpp - The 3G/4G MODEM dump class.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-6-15 Zhang Ziyi
 *  Initial version.
 */
#include "diag_dev_hdl.h"
#include "modem_dump.h"
#include "multiplexer.h"

ModemDumpConsumer::ModemDumpConsumer(const LogString& cp_name,
				     CpStorage& cp_stor,
				     const struct tm& lt)
	:CpDumpConsumer(cp_name, cp_stor, lt),
	 m_read_num {0},
	 m_timer {0}
{
}

ModemDumpConsumer::~ModemDumpConsumer()
{
	if (m_timer) {
		TimerManager& tmgr = diag_dev()->multiplexer()->timer_mgr();
		tmgr.del_timer(m_timer);
	}
}

int ModemDumpConsumer::start()
{
	// Send command
	uint8_t dump_cmd[2] = { 0x33, 0xa };
	DiagDeviceHandler* diag = diag_dev();
	int ret = -1;

	ssize_t n = ::write(diag->fd(), dump_cmd, 2);
	if (2 == n) {
		if (open_dump_file()) {
			// Set read timeout
			TimerManager& tmgr = diag->multiplexer()->timer_mgr();
			m_timer = tmgr.create_timer(3000, dump_read_timeout, this);
			ret = 0;
		}
	}

	return ret;
}

bool ModemDumpConsumer::process(DeviceFileHandler::DataBuffer& buffer)
//{Debug: test /proc/cpxxx/mem
#if 0
{
	TimerManager& tmgr = diag_dev()->multiplexer()->timer_mgr();

	remove_dump_file();
	tmgr.del_timer(m_timer);
	m_timer = 0;
	notify_client(LPR_FAILURE);
	return true;
}
#else
{
	bool ret = false;
	// Destroy the timer
	TimerManager& tmgr = diag_dev()->multiplexer()->timer_mgr();
	tmgr.set_new_due_time(m_timer, 3000);

	++m_read_num;

	LogFile* f = dump_file();
	ssize_t n = f->write(buffer.buffer + buffer.data_start,
			     buffer.data_len);
	if (static_cast<size_t>(n) != buffer.data_len) {
		remove_dump_file();
		tmgr.del_timer(m_timer);
		m_timer = 0;
		notify_client(LPR_FAILURE);
		ret = true;
		err_log("need to write %lu, write returns %d",
			static_cast<unsigned long>(buffer.data_len),
			static_cast<int>(n));
	} else {
		buffer.data_start = 0;
		buffer.data_len = 0;
	}
	return ret;
}
#endif

void ModemDumpConsumer::dump_read_timeout(void* param)
{
	ModemDumpConsumer* consumer = static_cast<ModemDumpConsumer*>(param);
	LogProcResult result;

	consumer->m_timer = 0;
	if (consumer->m_read_num >= 10) {
		consumer->close_dump_file();
		info_log("timeout after %u reads, finished",
			 consumer->m_read_num);
		result = LPR_SUCCESS;
	} else {
		consumer->remove_dump_file();
		err_log("timeout after %u reads", consumer->m_read_num);
		result = LPR_FAILURE;
	}
	consumer->notify_client(result);
}
