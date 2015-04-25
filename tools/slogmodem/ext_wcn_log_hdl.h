/*
 *  ext_wcn_log_hdl.h - External WCN log handler declaration.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-17 Zhang Ziyi
 *  Initial version.
 */
#ifndef _EXT_WCN_LOG_HDL_H_
#define _EXT_WCN_LOG_HDL_H_

#include "log_pipe_hdl.h"

class ExtWcnLogHandler : public LogPipeHandler
{
public:
	ExtWcnLogHandler(LogController* ctrl,
		         Multiplexer* multi,
		         const LogConfig::ConfigEntry* conf);

	int save_dump();

private:
	bool will_be_reset() const;
};

#endif  // !_EXT_WCN_LOG_HDL_H_

