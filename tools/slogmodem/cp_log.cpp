/*
 *  cp_log.cpp - The main function for the CP log and dump program.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#include <signal.h>
#include <cstring>

#include "def_config.h"
#include "log_ctrl.h"
#include "log_config.h"
#include "slog_config.h"

/*
 *  read_config - read config file.
 *
 *  To keep compatible with existing design, we first read configs
 *  from slog_modem.conf, then we update the settings with configs
 *  from /data/local/tmp/slog/slog.conf, and save the new config
 *  into slog_modem.conf.
 *  In the future the engineering mode application will stop saving
 *  CP log related configs into slog.conf, and slog.conf won't be
 *  read by then.
 *
 *  Return 0 on success, -1 on error.
 */
static int read_config(LogConfig& config)
{
	int err = config.read_config();
	if (err < 0) {
		return err;
	}

	SLogConfig slog_config;
	if (!slog_config.read_config(TMP_SLOG_CONFIG)) {
		const SLogConfig::ConfigList& clist = slog_config.get_conf();
		SLogConfig::ConstConfigIter it;
		for (it = clist.begin(); it != clist.end(); ++it) {
			SLogConfig::ConfigEntry* p = *it;
			config.enable_log(p->type, p->enable);
		}
		size_t file_size;
		if (slog_config.get_file_size(file_size)) {
			config.set_log_file_size(file_size);
		}

		if (config.dirty()) {
			if (config.save()) {
				err_log("save config failed");
			}
		}
	}

	return 0;
}

int main(int argc, char** argv)
{
	LogController log_controller;
	LogConfig log_config(CP_LOG_TMP_CONFIG_FILE);

	info_log("slogmodem start");
	int err = log_config.set_stor_pos(argc, argv);
	if (err < 0) {
		err_log("command line error");
		return 1;
	}

	err = read_config(log_config);
	if (err < 0) {
		err_log("read config error: %d", err);
		return 2;
	}

	// Ignore SIGPIPE to avoid to be killed by the kernel
	// when writing to a socket which is closed by the peer.
	struct sigaction siga;

	memset(&siga, 0, sizeof siga);
	siga.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &siga, 0);

	if (log_controller.init(&log_config) < 0) {
		return 2;
	}
	return log_controller.run();
}
