/*
 *  client_hdl.h - The base class declaration for file descriptor
 *                 handler.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 *
 *  2015-6-5 Zhang Ziyi
 *  CP dump notification added.
 */
#ifndef CLIENT_HDL_H_
#define CLIENT_HDL_H_

#include "cp_log_cmn.h"
#include "data_proc_hdl.h"

class ClientManager;

class ClientHandler : public DataProcessHandler
{
public:
	enum CpEvent
	{
		CE_DUMP_START,
		CE_DUMP_END
	};

	ClientHandler(int sock, LogController* ctrl,
		      Multiplexer* multiplexer,
		      ClientManager* mgr);

	void notify_cp_dump(CpType cpt, CpEvent evt);

private:
	#define CLIENT_BUF_SIZE 256

	ClientManager* m_mgr;
	bool m_cp_dump_notify[CT_NUMBER];

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
	void proc_get_log_file_size(const uint8_t* req, size_t len);
	void proc_set_log_file_size(const uint8_t* req, size_t len);
	void proc_enable_overwrite(const uint8_t* req, size_t len);
	void proc_disable_overwrite(const uint8_t* req, size_t len);
	void proc_get_data_part_size(const uint8_t* req, size_t len);
	void proc_set_data_part_size(const uint8_t* req, size_t len);
	void proc_get_sd_size(const uint8_t* req, size_t len);
	void proc_set_sd_size(const uint8_t* req, size_t len);
	void proc_get_log_overwrite(const uint8_t* req, size_t len);
	void proc_subscribe(const uint8_t* req, size_t len);
	void proc_unsubscribe(const uint8_t* req, size_t len);

	static const uint8_t* search_end(const uint8_t* req, size_t len);
	static int send_dump_notify(int fd, CpType cpt, CpEvent evt);
};

#endif  // !CLIENT_HDL_H_

