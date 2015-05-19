/*
 *  modem_stat_hdl.cpp - The MODEM status handler implementation.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#include <sys/types.h>
#include <sys/socket.h>
#ifdef HOST_TEST_
	#include "sock_test.h"
#else
	#include "cutils/sockets.h"
#endif

#include "modem_stat_hdl.h"
#include "multiplexer.h"
#include "log_ctrl.h"
#include "parse_utils.h"

ModemStateHandler::ModemStateHandler(LogController* ctrl,
				     Multiplexer* multiplexer,
				     const char* serv_name)
	:CpStateHandler(ctrl, multiplexer, serv_name)
{
}

CpStateHandler::CpEvent ModemStateHandler::parse_notify(const uint8_t* buf,
							size_t len,
							CpType& type)
{
	const uint8_t* p = find_str(buf, len,
				    reinterpret_cast<const uint8_t*>("TD Modem Assert"),
				    15);
	if (p) {
		type = CT_TD;
		return CE_ASSERT;
	}

	p = find_str(buf, len,
		     reinterpret_cast<const uint8_t*>("Modem Assert"),
		     12);
	if (p) {
		CpEvent evt = CE_ASSERT;
		type = get_cp_type(m_buffer);
		if (CT_UNKNOWN == type) {
			evt = CE_NONE;
		}
		return evt;
	}

	p = find_str(buf, len,
		     reinterpret_cast<const uint8_t*>("Modem Alive"),
		     11);
	if (p) {
		CpEvent evt = CE_ALIVE;
		type = get_cp_type(m_buffer);
		if (CT_UNKNOWN == type) {
			evt = CE_NONE;
		}
		return evt;
	}

	p = find_str(buf, len,
		     reinterpret_cast<const uint8_t*>("Modem Blocked"),
		     13);
	if (p) {
		CpEvent evt = CE_BLOCKED;
		type = get_cp_type(m_buffer);
		if (CT_UNKNOWN == type) {
			evt = CE_NONE;
		}
		return evt;
	}

	return CE_NONE;
}

CpType ModemStateHandler::get_cp_type(const ConnectionBuffer& cbuf)
{
	const uint8_t* p = find_str(cbuf.buffer, cbuf.data_len,
				    reinterpret_cast<const uint8_t*>("W "), 2);
	if (p) {
		return CT_WCDMA;
	}

	p = find_str(cbuf.buffer, cbuf.data_len,
		     reinterpret_cast<const uint8_t*>("TDD"), 3);
	if (p) {
		return CT_3MODE;
	}

	p = find_str(cbuf.buffer, cbuf.data_len,
		     reinterpret_cast<const uint8_t*>("FDD"), 3);
	if (p) {
		return CT_4MODE;
	}

	p = find_str(cbuf.buffer, cbuf.data_len,
		     reinterpret_cast<const uint8_t*>("5MODE"), 5);
	if (p) {
		return CT_5MODE;
	}

	p = find_str(cbuf.buffer, cbuf.data_len,
		     reinterpret_cast<const uint8_t*>("WCN"), 3);
	if (p) {
		return CT_WCN;
	}

	return CT_UNKNOWN;
}
