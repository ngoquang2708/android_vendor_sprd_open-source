/*
 *  log_ctrl.h - The log controller class declaration.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#ifndef LOG_CTRL_H_
#define LOG_CTRL_H_

#include "cp_log_cmn.h"
#include "multiplexer.h"
#include "client_mgr.h"
#include "log_pipe_hdl.h"
#include "modem_stat_hdl.h"
#ifdef EXTERNAL_WCN
	#include "ext_wcn_stat_hdl.h"
	#include "ext_wcn_log_hdl.h"
#else
	#include "int_wcn_stat_hdl.h"
	#include "int_wcn_log_hdl.h"
#endif
#include "log_config.h"
#include "file_mgr.h"
#include "log_stat.h"

class LogController
{
public:
	LogController();
	~LogController();

	int init(LogConfig* config);
	int run();

	/*
	 *    check_storage - Check whether the storage media is changed.
	 *
	 *    If the media is changed, the function creates the new time
	 *    directory and creates all log files.
	 *
	 *    Return true if the storage changed, false otherwise.
	 */
	bool check_storage();

	/*
	 *    check_dir_exist - Check whether the timed directory exists.
	 *
	 *    If the timed directory is removed, LogController shall
	 *    change log file for all CPs.
	 *    If the timed directory is not removed, the LogPipeHandler
	 *    shall change its log file.
	 *
	 *    Return Value:
	 *        1: directory changed and all CP logs have been changed.
	 *        0: directory exists.
	 *        -1: error occurs.
	 */
	int check_dir_exist();

	FileManager* file_manager()
	{
		return &m_file_mgr;
	}

	void process_cp_alive(CpType type);
	void process_cp_blocked(CpType type);
	void process_cp_assert(CpType type);

	// Client requests
	int enable_log(const CpType* cps, size_t num);
	int disable_log(const CpType* cps, size_t num);
	int enable_md();
	int disable_md();
	int mini_dump();
	int reload_slog_conf();

	/*
	 *    save_mini_dump - Save the mini dump.
	 *
	 *    @par_dir: the directory in which the mini dump is to
	 *              be saved
	 *    @t: the time to be used as the file name
	 *
	 *    Return Value:
	 *      Return 0 on success, -1 otherwise.
	 */
	static int save_mini_dump(const LogString& par_dir,
				  const struct tm& t);

	/*
	 *    save_sipc_dump - Save the SIPC dump.
	 *    @par_dir: the directory in which the SIPC dump is to
	 *              be saved
	 *    @t: the time to be used as the file name
	 *
	 *    Return Value:
	 *      Return 0 on success, -1 otherwise.
	 */
	static int save_sipc_dump(const LogString& par_dir,
				  const struct tm& t);

private:
	typedef LogList<LogPipeHandler*>::iterator LogPipeIter;

	LogConfig* m_config;
	bool m_save_md;
	Multiplexer m_multiplexer;
	ClientManager* m_cli_mgr;
	LogList<LogPipeHandler*> m_log_pipes;
	ModemStateHandler* m_modem_state;
#ifdef EXTERNAL_WCN
	ExtWcnStateHandler* m_wcn_state;
#else
	IntWcnStateHandler* m_wcn_state;
#endif

	FileManager m_file_mgr;
	// Log statistics for /data partition
	LogStat m_data_part_stat;
	// Log statistics for SD card
	LogStat m_sd_stat;

	LogStat* get_cur_stat();

	/*
	 * create_handler - Create the LogPipeHandler object and try to
	 *                  open the device if it's enabled in the config.
	 */
	LogPipeHandler* create_handler(const LogConfig::ConfigEntry* e);

	/*
	 * init_state_handler - Create the ModemStateHandler object for
	 *                      connections to modemd and wcnd.
	 */
	void init_state_handler(ModemStateHandler*& handler,
				const char* serv_name);
	/*
	 * init_wcn_state_handler - Create the ExtWcnStateHandler
	 *                          or the IntWcnStateHandler.
	 */
	template<typename T>
	void init_wcn_state_handler(T*& handler, const char* serv_name);

	void clear_log_pipes();

	static LogList<LogPipeHandler*>::iterator
		find_log_handler(LogList<LogPipeHandler*>& handlers,
				 CpType t);
};

#endif  // !LOG_CTRL_H_

