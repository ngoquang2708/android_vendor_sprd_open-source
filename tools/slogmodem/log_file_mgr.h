/*
 *  log_file_mgr.h - The log file manager class for each CP.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#ifndef _LOG_FILE_MGR_H_
#define _LOG_FILE_MGR_H_

#include "cp_log_cmn.h"

class LogPipeHandler;
class LogStat;

class LogFileManager
{
public:
	LogFileManager(const LogString& cp_name, LogPipeHandler* handler);
	~LogFileManager();

	const LogString& dir() const
	{
		return m_log_dir;
	}

	void set_log_limit(size_t sz)
	{
		m_size_limit = sz;
	}

	void set_timestamp_fifo(const char* fifo)
	{
		m_timestamp_file = fifo;
	}

	/*
	 *  change_dir - create the CP directory under the new directory
	 *               and save the new LogStat object.
	 *  @timed_d: the timed directory under which the CP log directory
	 *            is to be created.
	 *  @log_stat: the pointer to the new LogStat object.
	 *
	 *  Return 0 on success, -1 otherwise.
	 *  If the log directory is the same, do nothing and return 0.
	 */
	int change_dir(const LogString& timed_d, LogStat* log_stat);

	/*
	 *    create_log - Create the log file.
	 *
	 *    Return 0 on success, -1 on error.
	 *
	 *    This function shall be called when there is no log file opened.
	 */
	int create_log();
	void close_log();

	/*
	 *  write - write log.
	 *
	 *  Return 0 on success, -1 on error, 1 on log file changed.
	 */
	int write(const void* data, size_t len);

	/*
	 *  shrink - try to remove old files to make room for new log.
	 *  @dec_num: number of bytes to be removed
	 *
	 *  Return the number of bytes removed.
	 */
	size_t shrink(size_t dec_num);

private:
	struct LogFile
	{
		// log file base name
		LogString name;
		size_t size;
	};

	LogPipeHandler* m_handler;
	LogString m_cp_name;
	LogString m_timestamp_file;
	// Single log file size limit
	size_t m_size_limit;
	// Full path of the log directory
	LogString m_log_dir;
	// Current log file full path
	LogString m_cur_log_path;
	LogFile* m_cur_file;
	FILE* m_cur_log;
	// Size of the current log file
	uint64_t m_size;
	LogList<LogFile*> m_log_list;
	// Current log stat object
	LogStat* m_log_stat;

	/*
	 *    gen_log_name - Generate log file name.
	 */
	static bool gen_log_name(LogString& log_name,
				 const LogString& cp_name);

	/*
	 *  parse_file_name - parse the log file name and get the file number.
	 *  @log_dir: the path of the directory where the file resides
	 *  @name: the base name of the file
	 *  @num: the number at the beginning of the file name
	 *  @len: the number of chars of the file number in the file name
	 *
	 */
	static int parse_file_name(const char* name,
				   int& num,
				   int& len);

	/*
	 *    check_file_exist - Check whether the current log file exists.
	 *
	 *    Return Value:
	 *        0: file exists
	 *        1: file removed and a new log file created
	 *        -1: error occurs
	 */
	int check_file_exist();

	#define WL_SUCCESS 0
	#define WL_PARTIAL 1
	#define WL_ERROR (-1)
	static int write_log(const uint8_t* buf, size_t len, FILE* pf,
			     size_t& written);

	/*
	 *    save_timestamp - Save the CP timestamp.
	 *
	 *    This function assumes the log file is opened.
	 *
	 *    Return true if the time stamp file is saved, false otherwise.
	 */
	bool save_timestamp();

	/*
	 *  rotate - rotate the log files.
	 */
	int rotate();
};

#endif  // !_LOG_FILE_MGR_H_

