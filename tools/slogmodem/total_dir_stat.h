/*
 *  total_dir_stat.h - one timed directory statistics.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-4-23 Zhang Ziyi
 *  Initial version.
 */
#ifndef _TOTAL_DIR_STAT_H_
#define _TOTAL_DIR_STAT_H_

#include <cstdint>
#include <cstddef>
#include "cp_log_cmn.h"

class TotalDirStat
{
public:
	TotalDirStat(const char* path);

	/*
	 *  stat_size - compute the total size under the timed dir.
	 */
	int stat_size();

	void inc(size_t sz)
	{
		m_cur_size += sz;
	}
	void dec(size_t sz)
	{
		m_cur_size -= sz;
	}

	const LogString& dir() const
	{
		return m_path;
	}

	uint64_t size() const
	{
		return m_cur_size;
	}

private:
	// Full directory path
	LogString m_path;
	// Current total size
	uint64_t m_cur_size;

	static int compute_files_size(const LogString& path,
				      uint64_t& file_size);
};

bool operator < (const TotalDirStat& t1, const TotalDirStat& t2);

#endif  // !_TOTAL_DIR_STAT_H_

