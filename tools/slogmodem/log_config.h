/*
 *  log_config.h - The configuration class declaration.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#ifndef LOG_CONFIG_H_
#define LOG_CONFIG_H_

#include <cstdint>

#include "def_config.h"
#include "cp_log_cmn.h"

class LogConfig
{
public:
	enum StoragePosition
	{
		SP_DATA,  // /data directory
		SP_SD_CARD  // On SD card
	};

	struct ConfigEntry
	{
		LogString modem_name;
		CpType type;
		bool enable;
		size_t size;
		int level;

		ConfigEntry(const char* modem, size_t len,
			    CpType t,
			    bool en,
			    size_t sz,
			    int lvl)
			:modem_name (modem, len),
			 type {t},
			 enable {en},
			 size {sz},
			 level {lvl}
		{
		}
	};

	typedef LogList<ConfigEntry*> ConfigList;
	typedef LogList<ConfigEntry*>::iterator ConfigIter;
	typedef LogList<ConfigEntry*>::const_iterator ConstConfigIter;

	LogConfig(const char* tmp_config);
	~LogConfig();

	/*
	 * set_stor_pos - set storage position.
	 *
	 * Return Value:
	 *   If the function succeeds, return 0; otherwise return -1.
	 */
	int set_stor_pos(int argc, char** argv);
	StoragePosition stor_pos() const
	{
		return m_stor_pos;
	}
	const LogString& sd_top_dir() const
	{
		return m_sd_top_dir;
	}

	/*
	 * read_config - read config from specifed file.
	 *
	 * Return Value:
	 *   If the function succeeds, return 0; otherwise return -1.
	 */
	int read_config();

	bool dirty() const
	{
		return m_dirty;
	}

	/*
	 * save - save config to the config file.
	 *
	 * Return Value:
	 *   If the function succeeds, return 0; otherwise return -1.
	 */
	int save();

	const ConfigList& get_conf() const
	{
		return m_config;
	}

	bool md_enabled() const
	{
		return m_enable_md;
	}
	
	void enable_log(CpType cp, bool en = true);
	void enable_md(bool en = true);

	size_t max_log_file() const
	{
		return m_log_file_size;
	}
	void set_log_file_size(size_t sz)
	{
		if (m_log_file_size != sz) {
			m_log_file_size = sz;
			m_dirty = true;
		}
	}
	size_t max_data_part_size() const
	{
		return m_data_size;
	}
	size_t max_sd_size() const
	{
		return m_sd_size;
	}
	bool overwrite_old_log() const
	{
		return m_overwrite_old_log;
	}

	static const uint8_t* get_token(const uint8_t* buf, size_t& tlen);
	static int parse_enable_disable(const uint8_t* buf, bool& en);
	static int parse_number(const uint8_t* buf, size_t& num);

private:
	bool m_dirty;
	StoragePosition m_stor_pos;
	LogString m_sd_top_dir;
	LogString m_config_file;
	ConfigList m_config;
	bool m_enable_md;
	// Maximum size of a single log file in MB
	size_t m_log_file_size;
	// Maximum log size on /data partition in MB
	size_t m_data_size;
	// Maximum log size on SD card in MB
	size_t m_sd_size;
	// Overwrite on log size full?
	bool m_overwrite_old_log;

	int parse_line(const uint8_t* buf);
	int parse_stream_line(const uint8_t* buf);
	void clear_config();

	static CpType get_modem_type(const uint8_t* name, size_t len);
	static ConfigList::iterator find(ConfigList& clist, CpType t);
};

#endif  // !LOG_CONFIG_H_
