/*
 *  cp_stor.h - CP storage handle.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-5-15 Zhang Ziyi
 *  Initial version.
 */
#ifndef _CP_STOR_H_
#define _CP_STOR_H_

#include "cp_log_cmn.h"
#include "log_file.h"

class LogPipeHandler;
class StorageManager;
class CpDirectory;

class CpStorage
{
public:
	typedef void (*new_log_callback_t)(LogPipeHandler*, LogFile* f);
	typedef void (*new_dir_callback_t)(LogPipeHandler*);

	CpStorage(StorageManager& stor_mgr, LogPipeHandler& cp);
	~CpStorage();

	/*  write - write file and update the file size.
	 *  @data: the pointer to the data to be written.
	 *  @len: the length of the data in byte.
	 *
	 *  Return the number of bytes written on success, -1 on failure.
	 */
	ssize_t write(const void* data, size_t len);

	void stop();

	void set_new_log_callback(new_log_callback_t cb)
	{
		m_new_log_cb = cb;
	}
	void set_new_dir_callback(new_dir_callback_t cb)
	{
		m_new_dir_cb = cb;
	}

	// Non-log files
	LogFile* create_file(const LogString& name, LogFile::LogType t);

private:
	StorageManager& m_stor_mgr;
	LogPipeHandler& m_cp;
	new_log_callback_t m_new_log_cb;
	new_dir_callback_t m_new_dir_cb;
	// Current log file
	LogFile* m_cur_file;
	// Log capacity state
	bool m_shall_stop;
};

#endif  // !_CP_STOR_H_

