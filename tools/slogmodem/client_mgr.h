/*
 *  client_mgr.h - The client manager class declaration.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#ifndef CLIENT_MGR_H_
#define CLIENT_MGR_H_

#include "cp_log_cmn.h"
#include "client_hdl.h"

class ClientManager : public FdHandler
{
public:
	ClientManager(LogController* ctrl, Multiplexer* multi);
	~ClientManager();

	int init(const char* serv_name);

	void process(int events);

	void process_client_disconn(ClientHandler*);

private:
	LogList<ClientHandler*> m_clients;

	void clear_clients();
};

#endif  // !CLIENT_MGR_H_

