/*
 *  int_wcn_log_hdl.cpp - internal WCN log handler.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-4-8 Zhang Ziyi
 *  Initial version.
 */
#include <cstdint>
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "int_wcn_log_hdl.h"
#include "parse_utils.h"

IntWcnLogHandler::IntWcnLogHandler(LogController* ctrl, Multiplexer* multi,
				   const LogConfig::ConfigEntry* conf)
	:LogPipeHandler { ctrl, multi, conf }
{
}

bool IntWcnLogHandler::will_be_reset() const
{
	char reset[PROPERTY_VALUE_MAX];
	long n;
	char* endp;

	property_get(MODEM_WCN_DEVICE_RESET, reset, "");
	n = strtoul(reset, &endp, 0);
	return 1 == n ? true : false;
}

int IntWcnLogHandler::save_dump(const struct tm& lt)
{
	return 0;
}
