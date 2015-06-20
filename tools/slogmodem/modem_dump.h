/*
 *  modem_dump.h - The 3G/4G MODEM dump class.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-6-15 Zhang Ziyi
 *  Initial version.
 */
#ifndef _MODEM_DUMP_H_
#define _MODEM_DUMP_H_

#include "cp_dump.h"
#include "timer_mgr.h"

class LogFile;

class ModemDumpConsumer : public CpDumpConsumer
{
public:
	ModemDumpConsumer(const LogString& cp_name, CpStorage& cp_stor,
			  const struct tm& lt);
	~ModemDumpConsumer();

	int start();

	bool process(DeviceFileHandler::DataBuffer& buffer);

private:
	unsigned m_read_num;
	TimerManager::Timer* m_timer;

	static void dump_read_timeout(void* param);
};

#endif  // !_MODEM_DUMP_H_
