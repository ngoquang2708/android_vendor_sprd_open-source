/*
 *  ext_wcn_dump.cpp - The external WCN dump class.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-6-15 Zhang Ziyi
 *  Initial version.
 */
#include "ext_wcn_dump.h"
#include "parse_utils.h"

ExtWcnDumpConsumer::ExtWcnDumpConsumer(const LogString& cp_name,
				       CpStorage& cp_stor,
				       const struct tm& lt)
	:CpDumpConsumer(cp_name, cp_stor, lt)
{
}

int ExtWcnDumpConsumer::start()
{
	if (!open_dump_file()) {
		err_log("open dump file failed!");
		return -1;
	}

	return 0;
}

bool ExtWcnDumpConsumer::process(DeviceFileHandler::DataBuffer& buffer)
{
	bool ret = false;
	const uint8_t* p = buffer.buffer + buffer.data_start;
	if (buffer.data_len < 4096 &&
	    find_str(p, buffer.data_len,
		     reinterpret_cast<const uint8_t*>("marlin_memdump_finish"),
		     21)) {
		buffer.data_start = 0;
		buffer.data_len = 0;
		close_dump_file();
		notify_client(LPR_SUCCESS);
		ret = true;
		return ret;
	}

	LogFile* f = dump_file();
	size_t len = buffer.data_len;
	ssize_t n = f->write(buffer.buffer + buffer.data_start,
			     len);
	buffer.data_start = 0;
	buffer.data_len = 0;
	if (static_cast<size_t>(n) != len) {
		remove_dump_file();
		notify_client(LPR_FAILURE);
		ret = true;
	}

	return ret;
}
