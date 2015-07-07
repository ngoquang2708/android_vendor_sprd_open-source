/*
 *  cp_log.cpp - The main function for the CP log and dump program.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#include <cstring>
#include <signal.h>

#include "def_config.h"
#include "log_ctrl.h"
#include "log_config.h"

int main(int argc, char** argv)
{
	LogController log_controller;
	LogConfig log_config(CP_LOG_TMP_CONFIG_FILE);

	info_log("slogmodem start");

	int err = log_config.read_config();
	if (err < 0) {
		return err;
	}

	err = log_config.set_stor_pos(argc, argv);
	if (err < 0) {
		err_log("command line error");
		return 1;
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
