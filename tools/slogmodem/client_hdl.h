/*
 *  client_hdl.h - The base class declaration for file descriptor
 *                 handler.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#ifndef CLIENT_HDL_H_
#define CLIENT_HDL_H_

#include "data_proc_hdl.h"

class ClientManager;

class ClientHandler : public DataProcessHandler
{
public:
	ClientHandler(int sock, LogController* ctrl,
		      Multiplexer* multiplexer,
		      ClientManager* mgr);

private:
	#define CLIENT_BUF_SIZE 256

	ClientManager* m_mgr;

	int process_data();
	void process_conn_closed();
	void process_conn_error(int err);

	void process_req(const uint8_t* req, size_t len);
	void proc_slogctl(const uint8_t* req, size_t len);
	void proc_enable_log(const uint8_t* req, size_t len);
	void proc_disable_log(const uint8_t* req, size_t len);
	void proc_enable_md(const uint8_t* req, size_t len);
	void proc_disable_md(const uint8_t* req, size_t len);
	void proc_mini_dump(const uint8_t* req, size_t len);

	static const uint8_t* search_end(const uint8_t* req, size_t len);
};

#endif  // !CLIENT_HDL_H_

