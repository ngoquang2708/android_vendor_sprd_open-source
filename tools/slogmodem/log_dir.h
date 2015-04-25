/*
 *  log_dir.h - Log directory manager.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-4-22 Zhang Ziyi
 *  Initial version.
 */
#ifndef _LOG_DIR_H_
#define _LOG_DIR_H_

#include <cstdint>
#include "cp_log_cmn.h"

class LogDir
{
public:
	LogDir(const LogString& path);

	const LogString& path() const
	{
		return m_dir_path;
	}

	/*
	 *  stat_size - Statistic the total size of files under the
	 *              directory.
	 *
	 *  Return 0 on success, -1 otherwise.
	 */
	int stat_size();

	uint64_t size() const
	{
		return m_size;
	}

private:
	LogString m_dir_path;
	// Total size of the files in the directory
	uint64_t m_size;
};

#endif  // !_LOG_DIR_H_

