/*
 *  log_config.cpp - The configuration class implementation.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#include <climits>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "log_config.h"

LogConfig::LogConfig(const char* tmp_config)
	:m_dirty {false},
	 m_stor_pos {SP_DATA},
	 m_ext_stor_type {1},
	 m_config_file(tmp_config),
	 m_enable_md {false},
	 m_log_file_size {5},
	 m_data_size {25},
	 m_sd_size {250},
	 m_overwrite_old_log {true}
{
}

LogConfig::~LogConfig()
{
	clear_config();
}

int LogConfig::set_stor_pos(int argc, char** argv)
{
	int i;
	int ret = 0;
	StoragePosition stor_pos = SP_DATA;
	LogString top_dir;

	for (i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "-s")) {
			if (SP_DATA != stor_pos) {
				err_log("multiple -s options");
				ret = -1;
				break;
			}
			++i;
			if (i >= argc) {
				err_log("no storage defined");
				ret = -1;
				break;
			}
			char* dir;
			if (!strcmp(argv[i], "ext")) {
				dir = getenv("EXTERNAL_STORAGE");
				if (dir) {
					m_ext_stor_type = 1;
				}
			} else if (!strcmp(argv[i], "sec")) {
				dir = getenv("SECONDARY_STORAGE");
				if (dir) {
					m_ext_stor_type = 2;
				}
			} else {
				err_log("unknown SD type option: %s", argv[i]);
				ret = -1;
				break;
			}
			if (dir) {
				stor_pos = SP_SD_CARD;
				top_dir = dir;
			}
		} else {
			ret = -1;
			break;
		}
	}

	if (!ret) {
		m_stor_pos = stor_pos;
		m_sd_top_dir = top_dir;
	}

	return ret;
}

int LogConfig::read_config()
{
	uint8_t* buf;
	FILE* pf;
	int line_num = 0;

	buf = new uint8_t[1024];

	const char* str_cfile = ls2cstring(m_config_file);
	pf = fopen(str_cfile, "rt");
	if (!pf) {
		LogString cmd;

		str_assign(cmd, "cp /system/etc/slog_modem.conf ", 31);
		cmd += m_config_file;
		system(ls2cstring(cmd));

		pf = fopen(str_cfile, "rt");
		if (!pf) {
			err_log("can not open %s", str_cfile);
			goto delBuf;
		}
	}

	// Read config file
	while (true) {
		char* p = fgets(reinterpret_cast<char*>(buf), 1024, pf);
		if (!p) {
			break;
		}
		++line_num;
		int err = parse_line(buf);
		if (-1 == err) {
			err_log("invalid line: %d\n", line_num);
		}
	}

	fclose(pf);
	delete [] buf;

	return 0;

delBuf:
	delete [] buf;
	return -1;
}

void LogConfig::clear_config()
{
	while(!m_config.empty()) {
		ConfigIter it = m_config.begin();
		ConfigEntry* pe = *(it);
		m_config.erase(it);
		delete pe;
	}
}

int LogConfig::parse_enable_disable(const uint8_t* buf, bool& en)
{
	const uint8_t* t;
	size_t tlen;

	t = get_token(buf, tlen);
	if (!t) {
		return -1;
	}

	if (6 == tlen && !memcmp(t, "enable", 6)) {
		en = true;
	} else {
		en = false;
	}

	return 0;
}

int LogConfig::parse_number(const uint8_t* buf, size_t& num)
{
	unsigned long n;
	char* endp;

	n = strtoul(reinterpret_cast<const char*>(buf), &endp, 0);
	if ((ULONG_MAX == n && ERANGE == errno) || !isspace(*endp)) {
		return -1;
	}

	num = static_cast<size_t>(n);
	return 0;
}

int LogConfig::parse_stream_line(const uint8_t* buf)
{
	const uint8_t* t;
	size_t tlen;
	const uint8_t* pn;
	size_t nlen;

	// Get the modem name
	pn = get_token(buf, nlen);
	if (!pn) {
		return -1;
	}
	CpType cp_type = get_modem_type(pn, nlen);
	if (CT_UNKNOWN == cp_type) {
		// Ignore unknown CP
		return 0;
	}

	// Get enable state
	bool is_enable;

	buf = pn + nlen;
	t = get_token(buf, tlen);
	if (!t) {
		return -1;
	}
	if (2 == tlen && !memcmp(t, "on", 2)) {
		is_enable = true;
	} else {
		is_enable = false;
	}

	// Size
	buf = t + tlen;
	t = get_token(buf, tlen);
	if (!t) {
		return -1;
	}
	char* endp;
	unsigned long sz = strtoul(reinterpret_cast<const char*>(t),
				    &endp, 0);
	if ((ULONG_MAX == sz && ERANGE == errno) ||
	    (' ' != *endp && '\t' != *endp && '\r' != *endp && '\n' != *endp
	     && '\0' != *endp)) {
		return -1;
	}

	// Level
	buf = reinterpret_cast<const uint8_t*>(endp);
	t = get_token(buf, tlen);
	if (!t) {
		return -1;
	}
	unsigned long level = strtoul(reinterpret_cast<const char*>(t),
				      &endp, 0);
	if ((ULONG_MAX == level && ERANGE == errno) ||
	    (' ' != *endp && '\t' != *endp && '\r' != *endp && '\n' != *endp
	     && '\0' != *endp)) {
		return -1;
	}

	ConfigList::iterator it = find(m_config, cp_type);
	if (it != m_config.end()) {
		ConfigEntry* pe = *it;
		str_assign(pe->modem_name,
			   reinterpret_cast<const char*>(pn), nlen);
		pe->enable = is_enable;
		pe->size = sz;
		pe->level = static_cast<int>(level);

		err_log("slogcp: duplicate CP %s in config file",
			ls2cstring(pe->modem_name));
	} else {
		ConfigEntry* pe = new ConfigEntry{reinterpret_cast<const char*>(pn),
						  nlen,
						  cp_type,
						  is_enable,
						  sz,
						  static_cast<int>(level)};
		m_config.push_back(pe);
	}

	return 0;
}

int LogConfig::parse_line(const uint8_t* buf)
{
	// Search for the first token
	const uint8_t* t;
	size_t tlen;
	int err = -1;

	t = get_token(buf, tlen);
	if (!t || '#' == *t) {
		return 0;
	}

	// What line?
	buf += tlen;
	switch (tlen) {
	case 6:
		if (!memcmp(t, "stream", 6)) {
			err = parse_stream_line(buf);
		}
		break;
	case 8:
		if (!memcmp(t, "minidump", 8)) {
			err = parse_enable_disable(buf, m_enable_md);
		}
		break;
	case 11:
		if (!memcmp(t, "sd_log_size", 11)) {
			err = parse_number(buf, m_sd_size);
		}
		break;
	case 13:
		if (!memcmp(t, "log_file_size", 13)) {
			err = parse_number(buf, m_log_file_size);
		} else if (!memcmp(t, "data_log_size", 13)) {
			err = parse_number(buf, m_data_size);
		} else if (!memcmp(t, "log_overwrite", 13)) {
			err = parse_enable_disable(buf, m_overwrite_old_log);
		}
		break;
	default:
		break;
	}

	return err;
}

const uint8_t* LogConfig::get_token(const uint8_t* buf, size_t& tlen)
{
	// Search for the first non-blank
	while (*buf) {
		uint8_t c = *buf;
		if (' ' != c && '\t' != c && '\r' != c && '\n' != c) {
			break;
		}
		++buf;
	}
	if (!*buf) {
		return 0;
	}

	// Search for the first blank
	const uint8_t* p = buf + 1;
	while (*p) {
		uint8_t c = *p;
		if (' ' == c || '\t' == c || '\r' == c || '\n' == c) {
			break;
		}
		++p;
	}

	tlen = p - buf;
	return buf;
}

CpType LogConfig::get_modem_type(const uint8_t* name, size_t len)
{
	CpType type = CT_UNKNOWN;

	switch (len) {
	case 6:
		if (!memcmp(name, "cp_wcn", 6)) {
			type = CT_WCN;
		}
		break;
	case 8:
		if (!memcmp(name, "cp_wcdma", 8)) {
			type = CT_WCDMA;
		} else if (!memcmp(name, "cp_3mode", 8)) {
			type = CT_3MODE;
		} else if (!memcmp(name, "cp_4mode", 8)) {
			type = CT_4MODE;
		} else if (!memcmp(name, "cp_5mode", 8)) {
			type = CT_5MODE;
		}
		break;
	case 11:
		if (!memcmp(name, "cp_td-scdma", 11)) {
			type = CT_TD;
		}
		break;
	default:
		break;
	}

	return type;
}

int LogConfig::save()
{
	FILE* pf = fopen(ls2cstring(m_config_file), "w+t");

	if (!pf) {
		return -1;
	}

	// Mini dump
	fprintf(pf, "minidump %s\n", m_enable_md ? "enable" : "disable");
	// Max log file size
	fprintf(pf, "log_file_size %u\n",
		static_cast<unsigned>(m_log_file_size));
	// Max log size on /data partition
	fprintf(pf, "data_log_size %u\n",
		static_cast<unsigned>(m_data_size));
	// Max log size on SD card
	fprintf(pf, "sd_log_size %u\n",
		static_cast<unsigned>(m_sd_size));
	// Overwrite old log
	fprintf(pf, "log_overwrite %s\n",
		m_overwrite_old_log ? "enable" : "disable");

	fprintf(pf, "\n");
	fprintf(pf, "#Type\tName\tState\tSize\tLevel\n");

	for (ConfigIter it = m_config.begin(); it != m_config.end(); ++it) {
		ConfigEntry* pe = *it;
		fprintf(pf, "stream\t%s\t%s\t%u\t%u\n",
			ls2cstring(pe->modem_name),
			pe->enable ? "on" : "off",
			static_cast<unsigned>(pe->size),
			pe->level);
	}

	fclose(pf);

	m_dirty = false;

	return 0;
}

void LogConfig::enable_log(CpType cp, bool en /*= true*/)
{
	for (ConfigList::iterator it = m_config.begin();
	     it != m_config.end();
	     ++it) {
		ConfigEntry* p = *it;
		if (p->type == cp) {
			if (p->enable != en) {
				p->enable = en;
				m_dirty = true;
			}
			break;
		}
	}
}

void LogConfig::enable_md(bool en /*= true*/)
{
	if (m_enable_md != en) {
		m_enable_md = en;
		m_dirty = true;
	}
}

LogConfig::ConfigList::iterator LogConfig::find(ConfigList& clist, CpType t)
{
	ConfigList::iterator it;

	for (it = clist.begin(); it != clist.end(); ++it) {
		ConfigEntry* p = *it;
		if (p->type == t) {
			break;
		}
	}

	return it;
}
