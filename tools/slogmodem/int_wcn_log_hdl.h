/*
 *  int_wcn_log_hdl.h - internal WCN log handler declaration.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-4-8 Zhang Ziyi
 *  Initial version.
 */
#ifndef _INT_WCN_LOG_HDL_H_
#define _INT_WCN_LOG_HDL_H_

#include "log_pipe_hdl.h"

class IntWcnLogHandler : public LogPipeHandler
{
public:
	IntWcnLogHandler(LogController* ctrl,
		         Multiplexer* multi,
		         const LogConfig::ConfigEntry* conf,
		         StorageManager& stor_mgr);

	int save_dump(const struct tm& lt);

private:
	bool will_be_reset() const;
};

#endif  // !_INT_WCN_LOG_HDL_H_

