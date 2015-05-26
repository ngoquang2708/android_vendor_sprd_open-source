/*
 *  modem_stat_hdl.h - The MODEM status handler declaration.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#ifndef _MODEM_STAT_HDL_H_
#define _MODEM_STAT_HDL_H_

#include "cp_stat_hdl.h"

class ModemStateHandler : public CpStateHandler
{
public:
	ModemStateHandler(LogController* ctrl,
			  Multiplexer* multiplexer,
			  const char* serv_name);

private:
	CpEvent parse_notify(const uint8_t* buf, size_t len,
			     CpType& type);

	static CpType get_cp_type(const ConnectionBuffer& cbuf);
	/*
	 *    get_alive_cp_type - parse the MODEM type in alive notification.
	 *
	 *    Alive notifications are sent by modemd. There are 5 MODEM
	 *    types: TD, W, TL, LF, L
	 *
	 *    Return Value:
	 *      The MODEM type.
	 */
	static CpType get_alive_cp_type(const ConnectionBuffer& cbuf);
};

#endif  // !_MODEM_STAT_HDL_H_

