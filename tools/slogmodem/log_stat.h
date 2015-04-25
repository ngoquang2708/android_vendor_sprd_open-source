/*
 *  log_stat.h - Base class for log statistics.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-4-23 Zhang Ziyi
 *  Initial version.
 */
#ifndef _LOG_STAT_H_
#define _LOG_STAT_H_

#include <total_dir_stat.h>

class LogFileManager;

class LogStat
{
public:
	LogStat();
	~LogStat();

	/*
	 *  init - compute the file size in the directory.
	 *  @top_dir: the directory to be manage.
	 *
	 *  Return 0 on success, -1 if top_dir can not be accessed.
	 */
	int init(const char* top_dir);

	const LogString& top_dir() const
	{
		return m_top_dir;
	}

	void set_max_size(uint64_t sz)
	{
		m_max_size = sz;
	}

	void set_overwrite(bool ow)
	{
		m_overwrite = ow;
	}

	/*
	 *  change_dir - start statistics for a new directory.
	 */
	void change_dir(const LogString& dir);
	/*
	 *  end_stat - end statistics for current directory.
	 */
	void end_stat();

	/*
	 *  inc - add log size statistics.
	 *
	 *  Return 0 if there is more room available, -1 if current size is
	 *  too large and caller shall not save log any more.
	 */
	int inc(LogFileManager* log_mgr, size_t sz);
	void dec(size_t sz);

private:
	LogString m_top_dir;
	uint64_t m_max_size;
	bool m_overwrite;
	// History dirs arranged in chronological order. The first element
	// is the oldest log. The list does not include current log dir.
	LogList<TotalDirStat*> m_history;
	// Current dirs for the CPs
	TotalDirStat* m_cur_dir;
	// Current total size
	uint64_t m_cur_size;

	static void insert_ascending(LogList<TotalDirStat*>& l, TotalDirStat* total);

	/*
	 *  shrink - try to remove old files to make room for new log.
	 *
	 *  Return 0 if enough room is made for new log, -1 otherwise.
	 */
	int shrink(LogFileManager* log_mgr);
};

#endif  // !_LOG_STAT_H_

