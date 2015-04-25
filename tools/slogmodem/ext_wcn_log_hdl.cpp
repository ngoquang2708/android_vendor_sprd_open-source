/*
 *  ext_wcn_log_hdl.cpp - External WCN log handler.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-17 Zhang Ziyi
 *  Initial version.
 */
#include <cstdint>
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "ext_wcn_log_hdl.h"
#include "parse_utils.h"

ExtWcnLogHandler::ExtWcnLogHandler(LogController* ctrl, Multiplexer* multi,
				   const LogConfig::ConfigEntry* conf)
	:LogPipeHandler { ctrl, multi, conf }
{
}

bool ExtWcnLogHandler::will_be_reset() const
{
	char reset[PROPERTY_VALUE_MAX];
	long n;
	char* endp;

	property_get(MODEM_WCN_DEVICE_RESET, reset, "");
	n = strtoul(reset, &endp, 0);
	return 1 == n ? true : false;
}

int ExtWcnLogHandler::save_dump()
{
	// Open the dump file from the CP
	int err = -1;
	int logf;
	int fd_to_read;
	long flags;

	logf = open_dump_file();

	if (-1 == logf) {
		goto set_dump_prop;
	}

	fd_to_read = dump_fd();
	flags = fcntl(fd_to_read, F_GETFL);
	flags &= ~O_NONBLOCK;
	fcntl(fd_to_read, F_SETFL, flags);

	while (true) {
		ssize_t n = read(fd_to_read, log_buffer, LOG_BUFFER_SIZE);
		if (-1 == n) {
			if (EINTR != errno) {
				break;
			}
		} else if (!n) {
			break;
		} else {
			if (n < 4096) {
				// A short message, may be end of dump.
				if (find_str(log_buffer, n,
					     reinterpret_cast<const uint8_t*>("marlin_memdump_finish"),
					     21)) {
					err = 0;
					break;
				}
			}
			size_t to_write = n;
			n = write(logf, log_buffer, to_write);
			if (static_cast<size_t>(n) != to_write) {
				break;
			}
		}
	}

	flags |= O_NONBLOCK;
	fcntl(fd_to_read, F_SETFL, flags);

	close(logf);

set_dump_prop:
	property_set(MODEM_WCN_DUMP_LOG_COMPLETE, "1");
	return err;
}
