/*
 *  file_mgr.h - The storage media manager class declaration.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#ifndef FILE_MGR_H_
#define FILE_MGR_H_

#include "cp_log_cmn.h"
#include "log_config.h"

class LogController;
class LogStat;

class FileManager
{
public:
	enum LogMedia
	{
		LM_NONE,
		LM_INTERNAL,
		LM_EXTERNAL
	};

	FileManager(LogController* ctrl);

	/*
	 *  init - initialize FileManager.
	 *
	 *  Check storage media. Create top log dir as appropriate.
	 *
	 *  Return 1 if external SD card is available.
	 *  Return 0 if only /data is available.
	 *  Return -1 if failed.
	 */
	int init(LogConfig::StoragePosition pos, const LogString& top_dir);

	void sd_root(LogString&) const;

	/*
	 *    check_media - Check whether the storage media is changed.
	 *
	 *    If the media is changed, the function creates the new time
	 *    directory.
	 *
	 *    Return true if the storage changed, false otherwise.
	 */
	bool check_media(LogStat& sd_stat);

	/*
	 *    check_file_exist - Check whether the timed directory exists.
	 *
	 *    If the directory is deleted, the function creates a new 
	 *    directroy.
	 *
	 *    Return Value:
	 *        1: directory deleted and a new one created.
	 *        0: directory exists.
	 *        -1: error occurs.
	 */
	int check_dir_exist();

	LogMedia current_media() const
	{
		return m_log_media;
	}

	const LogString& get_cp_par_dir() const
	{
		return m_cur_timed_dir;
	}

	/*
	 *  clear - delete all logs on both internal and external storage.
	 *
	 *  Return 0 on success, -1 on error.
	 */
	int clear();

	static int copy_file(const char* src, const char* dest);
	static int copy_file(const char* src, int dest);

private:
	LogController* m_log_ctrl;
	bool m_save_sd;
	// Root directory of the SD card
	LogString m_sd_root;
	LogString m_root_dir;
	LogString m_cur_timed_dir;
	// Current log media
	LogMedia m_log_media;

	/*
	 *    create_timed_dir - Create the directory named by current time
	 *                       and update the m_cur_timed_dir member.
	 *
	 *    Return true if the new directory is created, false otherwise.
	 */
	bool create_timed_dir(const LogString& top_dir);

	static const size_t FILE_COPY_BUF_SIZE = (1024 * 32);
	static uint8_t s_copy_buf[FILE_COPY_BUF_SIZE];

	static bool get_sd_state();

	static int copy_file(int src, int dest);
};

#endif  // !FILE_MGR_H_
