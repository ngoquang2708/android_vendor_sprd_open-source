/*
 *  log_pipe_hdl.h - The CP log and dump handler class declaration.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-3-2 Zhang Ziyi
 *  Initial version.
 */
#ifndef LOG_PIPE_HDL_H_
#define LOG_PIPE_HDL_H_

#include <time.h>
#ifdef HOST_TEST_
	#include "prop_test.h"
#else
	#include <cutils/properties.h>
#endif
#include "cp_log_cmn.h"
#include "fd_hdl.h"
#include "log_config.h"
#include "log_file_mgr.h"

class LogPipeHandler : public FdHandler
{
public:
	LogPipeHandler(LogController* ctrl,
		       Multiplexer* multi,
		       const LogConfig::ConfigEntry* conf);
	~LogPipeHandler();

	CpType type() const
	{
		return m_type;
	}

	const LogString& name() const
	{
		return m_modem_name;
	}

	bool enabled() const
	{
		return m_enable;
	}

	/*
	 *    start - Initialize the log device and start logging.
	 *
	 *    This function put the LogPipeHandler in an adminitrative
	 *    enable state and try to open the log device.
	 *
	 *    Return Value:
	 *      0
	 */
	int start();

	/*
	 *    stop - Stop logging.
	 *
	 *    This function put the LogPipeHandler in an adminitrative
	 *    disable state and close the log device.
	 *
	 *    Return Value:
	 *      0
	 */
	int stop();

	void process(int events);

	/*
	 *    process_assert - Process the CP assertion.
	 *
	 *    @save_md: whether to save the mini dump
	 *
	 *    This function will be called by the LogController when the
	 *    CP asserts.
	 */
	void process_assert(bool save_md = true);

	/*
	 *    change_log_file - Change the log file (because the storage
	 *                      media changes).
	 *    @par_dir: the parent directory for the CP directory.
	 *    @log_stat: the pointer to the new LogStat object to use.
	 *
	 *    Return Value:
	 *      Return 0 on success, -1 on error.
	 */
	int change_log_file(const LogString& par_dir, LogStat* log_stat);

	void set_log_limit(size_t sz);

	int open_dump_file(const struct tm& lt);
	virtual int save_dump(const struct tm& lt);

	void open_on_alive();

protected:
	int dump_fd() const
	{
		return m_dump_fd;
	}

	/*  open_dump_device - Open the dump device.
	 *
	 *  Return Value:
	 *      Return 0 if the devices are opened, -1 on error.
	 *      If the dump device is opened, m_dump_fd will hold its
	 *      file descriptor.
	 */
	int open_dump_device();

	void close_dump_device();

	/*
	 *    log_buffer - Buffer shared by all LogPipeHandlers.
	 *
	 *    process function of all LogPipeHandler objects are called
	 *    from the same thread.
	 */
	static const size_t LOG_BUFFER_SIZE = (64 * 1024);
	static uint8_t log_buffer[LOG_BUFFER_SIZE];

private:
	enum CpWorkState
	{
		CWS_NOT_WORKING,
		CWS_WORKING
	};

	// Log turned on
	bool m_enable;
	LogString m_modem_name;
	CpType m_type;
	CpWorkState m_cp_state;
	int m_dump_fd;
	LogString m_log_file_name;
	LogFileManager m_log_mgr;

	/*
	 *    open_devices - Open the log device and the mini dump device.
	 *
	 *    Return Value:
	 *      Return 0 if the devices are opened, -1 on error.
	 */
	int open_devices();

	/*
	 *    close_devices - Close the log device and the mini dump device.
	 *
	 *    This function assumes m_fd and m_dump_fd are opened.
	 */
	void close_devices();

	/*
	 *    save_version - Save version.
	 */
	bool save_version();

	/*
	 *    will_be_reset - check whether the CP will be reset.
	 */
	virtual bool will_be_reset() const;

	int save_dump_ipc(int fd);
	int save_dump_proc(int fd);

	void close_on_assert();

	/*
	 *    reopen_log_dev - Try to open the log device and the mini
	 *                     dump device.
	 *
	 *    This function is called by the multiplexer on check event.
	 */
	static void reopen_log_dev(void* param);
};

#endif  // !LOG_PIPE_HDL_H_

