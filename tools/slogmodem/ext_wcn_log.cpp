/*
 *  ext_wcn_log.h - The external WCN log and dump handler class declaration.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-7-13 Zhang Ziyi
 *  Initial version.
 */

#include <cstdlib>
#include <poll.h>

#include "client_mgr.h"
#include "diag_dev_hdl.h"
#include "ext_wcn_dump.h"
#include "ext_wcn_log.h"
#include "log_ctrl.h"

ExtWcnLogHandler::ExtWcnLogHandler(LogController* ctrl,
				   Multiplexer* multi,
				   const LogConfig::ConfigEntry* conf,
				   StorageManager& stor_mgr)
	:LogPipeHandler(ctrl, multi, conf, stor_mgr)
{
}

int ExtWcnLogHandler::start_dump(const struct tm& lt)
{
	// Create the data consumer first
	int err = 0;
	ExtWcnDumpConsumer* dump;
	DiagDeviceHandler* diag;

	dump = new ExtWcnDumpConsumer(name(), *storage(), lt);

	dump->set_callback(this, diag_transaction_notify);

	diag = create_diag_device(dump);
	if (diag) {
		diag->add_events(POLLIN);
		dump->bind(diag);
		if (dump->start()) {
			err = -1;
		}
	} else {
		err = -1;
	}

	if (err) {
		delete diag;
		delete dump;
	} else {
		start_transaction(diag, dump, CWS_DUMP);
	}

	return err;
}

void ExtWcnLogHandler::diag_transaction_notify(void* client,
					       DataConsumer::LogProcResult res)
{
	ExtWcnLogHandler* cp = static_cast<ExtWcnLogHandler*>(client);
	bool valid = true;

	if (CWS_DUMP == cp->cp_state()) {
		cp->stop_transaction(CWS_NOT_WORKING);
		system("am broadcast -a slogui.intent.action.DUMP_END");
	} else {
		err_log("Receive diag notify %d under state %d, ignore",
			res, cp->cp_state());
	}
}
