/*
 *  wan_modem_log.cpp - The WAN MODEM log and dump handler class.
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
#include "log_ctrl.h"
#include "modem_dump.h"
#include "wan_modem_log.h"

WanModemLogHandler::WanModemLogHandler(LogController* ctrl,
				       Multiplexer* multi,
				       const LogConfig::ConfigEntry* conf,
				       StorageManager& stor_mgr)
	:LogPipeHandler(ctrl, multi, conf, stor_mgr)
{
}

int WanModemLogHandler::start_dump(const struct tm& lt)
{
	// Create the data consumer first
	int err = 0;
	ModemDumpConsumer* dump;
	DiagDeviceHandler* diag;

	dump = new ModemDumpConsumer(name(), *storage(), lt);

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

void WanModemLogHandler::diag_transaction_notify(void* client,
						 DataConsumer::LogProcResult res)
{
	WanModemLogHandler* cp = static_cast<WanModemLogHandler*>(client);
	bool valid = true;

	if (CWS_DUMP == cp->cp_state()) {
		if (DataConsumer::LPR_SUCCESS != res) {
			info_log("Read dump from spipe failed, save /proc/cpxxx/mem ...");

			CpDumpConsumer* cons = static_cast<CpDumpConsumer*>(cp->consumer());
			LogFile* mem_file = cp->open_dump_mem_file(cons->time());
			if (mem_file) {
				cp->save_dump_proc(mem_file);
				mem_file->close();
			} else {
				err_log("create dump mem file failed");
			}
		}

		cp->stop_transaction(CWS_NOT_WORKING);
		system("am broadcast -a slogui.intent.action.DUMP_END");
	} else {
		err_log("Receive diag notify %d under state %d, ignore",
			res, cp->cp_state());
	}
}
