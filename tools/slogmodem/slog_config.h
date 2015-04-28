/*
 *  slog_config.h - slog.conf parsing class.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-4-23 Zhang Ziyi
 *  Initial version.
 */
#ifndef _SLOG_CONFIG_H_
#define _SLOG_CONFIG_H_

#include <cstdint>

#include "def_config.h"
#include "cp_log_cmn.h"

class SLogConfig
{
public:
	struct ConfigEntry
	{
		CpType type;
		bool enable;

		ConfigEntry(CpType t,
			    bool en)
			:type {t},
			 enable {en}
		{
		}
	};

	typedef LogList<ConfigEntry*> ConfigList;
	typedef LogList<ConfigEntry*>::iterator ConfigIter;
	typedef LogList<ConfigEntry*>::const_iterator ConstConfigIter;

	SLogConfig();
	~SLogConfig();

	/*
	 *  read_config - read config from specifed file.
	 *  @cfile: the full path of slog.conf
	 *
	 *  Return Value:
	 *    If the function succeeds, return 0; otherwise return -1.
	 */
	int read_config(const char* cfile);

	const ConfigList& get_conf() const
	{
		return m_config;
	}

	bool get_file_size(size_t& sz) const
	{
		if (m_define_file_size) {
			sz = m_log_file_size;
		}
		return m_define_file_size;
	}

private:
	ConfigList m_config;
	bool m_total_enable;
	bool m_define_file_size;
	size_t m_log_file_size;

	int parse_line(const uint8_t* buf);
	int parse_stream_line(const uint8_t* buf);

	static CpType get_modem_type(const uint8_t* name, size_t len);
};

#endif  // !_SLOG_CONFIG_H_
