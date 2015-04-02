/*
 *  cp_config.c - Config file parser.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc., Ltd.
 *
 *  History:
 *  2015-3-26 Zhang Ziyi
 *      Initial version.
 */

#include <stdint.h>
#include <limits.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <cutils/properties.h>

#include "slog_modem.h"
#include "cp_config.h"
#include "config_imp.h"

int g_slog_enable = SLOG_ENABLE;
int g_minidump_enable = SLOG_ENABLE;
int g_overwrite_enable = SLOG_ENABLE;
int g_log_size = DEFAULT_LOG_SIZE_CP;

const uint8_t* get_token(const uint8_t* buf, size_t* tlen)
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

	*tlen = p - buf;
	return buf;
}

int str2unsigned(const char* s, size_t len, unsigned* pn)
{
	unsigned n = 0;

	for (unsigned i = 0; i < len; ++i) {
		int c = s[i];
		if (!isdigit(c)) {
			return -1;
		}
		if (n > UINT_MAX / 10) {
			return -1;
		}
		n = n * 10 + (c - '0');
	}

	*pn = n;
	return 0;
}

static int parse_stream_line(const char* line, struct StreamEntry* e)
{
	const uint8_t* tok;
	size_t tlen;
	size_t n;
	char* name;

	// Name
	tok = get_token((const uint8_t*)line, &tlen);
	if (!tok || tlen < 3 || memcmp(tok, "cp_", 3)) {
		return -1;
	}
	n = tlen + 1;
	name = (char*)malloc(n);
	if (!name) {
		return -1;
	}
	memcpy(name, tok, tlen);
	name[tlen] = '\0';

	// Now state (on/off)
	line = (const char*)(tok + tlen);
	tok = get_token((const uint8_t*)line, &tlen);
	if (!tok) {
		free(name);
		return -1;
	}
	if (2 == tlen && !memcmp("on", tok, 2)) {
		e->enable = SLOG_STATE_ON;
	} else {
		e->enable = SLOG_STATE_OFF;
	}

	// Now size
	line = (const char*)(tok + tlen);
	tok = get_token((const uint8_t*)line, &tlen);
	if (!tok) {
		free(name);
		return -1;
	}
	unsigned un;
	if (str2unsigned((const char*)tok, tlen, &un) || un > INT_MAX) {
		free(name);
		return -1;
	}
	e->size = un;

	// Level
	line = (const char*)(tok + tlen);
	tok = get_token((const uint8_t*)line, &tlen);
	if (!tok) {
		free(name);
		return -1;
	}
	if (str2unsigned((const char*)tok, tlen, &un) || un > INT_MAX) {
		free(name);
		return -1;
	}
	e->level = (int)un;

	e->name = name;
	return 0;
}

/*
 *  parse_log_size - Parse stream line.
 *  @line: pointer to the char after "stream"
 *  @log_sz: pointer to the int variable to hold the parsed log size.
 *
 *  Return Value:
 *      0: success
 *      -1: failure
 *
 */
static int parse_log_size(const char* line, int* log_sz)
{
	const uint8_t* tok;
	size_t tlen;
	int ret;
	unsigned un;

	tok = get_token((const uint8_t*)line, &tlen);
	if (!tok) {
		return -1;
	}
	ret = str2unsigned((const char*)tok, tlen, &un);
	if (!ret && un <= INT_MAX) {
		*log_sz = (int)un;
	} else {
		ret = -1;
	}

	return ret;
}

/*
 *  parse_ow - Parse log_overwrite line.
 *  @line: pointer to the char after "log_overwrite"
 *  @ow: pointer to the int variable to hold the parsed value.
 *
 *  Return Value:
 *      0: success
 *      -1: failure
 *
 */
static int parse_ow(const char* line, int* ow)
{
	const uint8_t* tok;
	size_t tlen;
	int ret;

	tok = get_token((const uint8_t*)line, &tlen);
	if (!tok) {
		return -1;
	}
	if (6 == tlen && !memcmp(tok, "enable", 6)) {
		*ow = 1;
	} else {
		*ow = 0;
	}

	return 0;
}

/*
 *  cp_para_config_entries - Parse stream line.
 *  @line: pointer to the char after "stream"
 *
 *  Return Value:
 *      0: success
 *      -1: failure
 *
 */
static int cp_para_config_entries(const char* line)
{
	struct slog_info* info;
	struct StreamEntry cp_entry;

	if (parse_stream_line(line, &cp_entry)) {
		return -1;
	}

	debug_log("config: %s %s",
		  cp_entry.name,
		  SLOG_STATE_ON == cp_entry.enable ? "on" : "off");

	/* alloc node */
	info = find_device(cp_entry.name);
	if(!info) {
		info = calloc(1, sizeof(struct slog_info));
		if(info == NULL) {
			err_log("slogcp:calloc failed!");
			return -1;
		}
		info->fd_device = info->fd_dump_cp = -1;
		/* init data structure according to type */
		info->type = SLOG_TYPE_STREAM;
		info->name = cp_entry.name;
		cp_entry.name = 0;
		if (!strcmp(info->name, "cp_wcn") ||
		    !strcmp(info->name, "cp_td-lte") ||
		    !strcmp(info->name, "cp_tdd-lte") ||
		    !strcmp(info->name, "cp_fdd-lte")) {
			info->log_path = strdup(info->name);
		} else {
			info->log_path = strdup("android");
		}
		info->log_basename = strdup(info->name);
		info->state = cp_entry.enable;
		info->max_size = (int)cp_entry.size;
		info->level = cp_entry.level;

		// Insert into the list
		if(!cp_log_head) {
			cp_log_head = info;
		} else {
			info->next = cp_log_head->next;
			cp_log_head->next = info;
		}
		debug_log("type %lu, name %s, %d %d %d\n",
			  info->type, info->name, info->state,
			  info->max_size, info->level);
	} else {  // CP entry already exist, just modify its state
		info->state = cp_entry.enable;
		free(cp_entry.name);
		cp_entry.name = 0;
	}

	return 0;
}

int cp_parse_config()
{
	FILE *fp;
	int ret = 0;
	char buffer[MAX_LINE_LEN];
	struct stat st;

	/* we use tmp log config file first */
	if (stat(TMP_SLOG_CONFIG, &st)) {
		ret = mkdir(TMP_FILE_PATH, S_IRWXU | S_IRWXG | S_IRWXO);
		if (-1 == ret && (errno != EEXIST)) {
			err_log("mkdir %s failed.", TMP_FILE_PATH);
			exit(0);
		}
		property_get("ro.debuggable", buffer, "");
		if (strcmp(buffer, "1") != 0) {
			if(!stat(DEFAULT_USER_SLOG_CONFIG, &st)){	
				//cp_file(DEFAULT_USER_SLOG_CONFIG, TMP_SLOG_CONFIG);
				debug_log("use slog config file");
			}
			else {
				err_log("cannot find config files!");
				exit(0);
			}
		} else {
			if(!stat(DEFAULT_DEBUG_SLOG_CONFIG, &st)){
				//cp_file(DEFAULT_DEBUG_SLOG_CONFIG, TMP_SLOG_CONFIG);
				debug_log("use slog config file");
			}
			else {
				err_log("cannot find config files!");
				exit(0);
			}
		}
	}

	fp = fopen(TMP_SLOG_CONFIG, "r");
	if(fp == NULL) {
		err_log("open file failed, %s.", TMP_SLOG_CONFIG);
		property_get("ro.debuggable", buffer, "");
		if (strcmp(buffer, "1") != 0) {
			fp = fopen(DEFAULT_USER_SLOG_CONFIG, "r");
			if(fp == NULL) {
				err_log("open file failed, %s.", DEFAULT_USER_SLOG_CONFIG);
				exit(0);
			}
		} else {
			fp = fopen(DEFAULT_DEBUG_SLOG_CONFIG, "r");
			if(fp == NULL) {
				err_log("open file failed, %s.", DEFAULT_DEBUG_SLOG_CONFIG);
				exit(0);
			}
		}
	}

	/* parse line by line */
	int line_cnt = 0;
	ret = 0;
	while(0 != fgets(buffer, MAX_LINE_LEN, fp)) {
		++line_cnt;
		if(buffer[0] == '#') {
			continue;
		}
		const uint8_t* tok;
		size_t tlen;

		tok = get_token((const uint8_t*)buffer, &tlen);
		if (!tok) {  // Empty line
			continue;
		}
		const uint8_t* param = tok + tlen;
		switch (tlen) {
		case 6:
			if(!memcmp("enable", tok, 6)) {
				debug_log("config: CP log enable");
				g_slog_enable = SLOG_ENABLE;
			} else if (!memcmp("stream", tok, 6)) {
				if (cp_para_config_entries((const char*)param)) {
					err_log("invalid config stream %s",
						buffer);
				}
			}
			break;
		case 7:
			if (!memcmp("disable", tok, 7)) {
				debug_log("config: CP log disable");
				g_slog_enable = SLOG_DISABLE;
			} else if (!memcmp("logsize", tok, 7)) {
				if (parse_log_size((const char*)param, &g_log_size)) {
					err_log("invalid config logsize %s",
						buffer);
				} else {
					debug_log("config: log_size %d",
						  g_log_size);
				}
			}
			break;
		case 13:
			if (!memcmp(tok, "log_overwrite", 13)) {
				if (parse_ow((const char*)param, &g_overwrite_enable)) {
					err_log("invalid overwrite: %s",
						buffer);
				} else {
					debug_log("config: log_overwrite %s",
						  g_overwrite_enable ? "enable" : "disable");
				}
			}
			break;
		default:
			break;
		}

		if(ret != 0) {
			err_log("parse slog.conf return %d.  reload", ret);
			fclose(fp);
			unlink(TMP_SLOG_CONFIG);
			exit(0);
		}
	}
	fclose(fp);
	debug_log("%d lines in config file", line_cnt);

	// Integrity check: if there is a "disable" line, set all CP state
	// to SLOG_STATE_OFF.
	if (SLOG_ENABLE != g_slog_enable) {
		struct slog_info* p = cp_log_head;

		while (p) {
			p->state = SLOG_STATE_OFF;
			p = p->next;
		}
	}

	return ret;
}
