/*
 *  cp_config.c - Config file parser API.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc., Ltd.
 *
 *  History:
 *  2015-3-26 Zhang Ziyi
 *      Initial version.
 */

#ifndef _CP_CONFIG_H_
#define _CP_CONFIG_H_

#include "slog_modem.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_slog_enable;
extern int g_minidump_enable;
extern int g_overwrite_enable;
extern int g_log_size;

int cp_parse_config(void);
int str2unsigned(const char* s, size_t len, unsigned* pn);

#ifdef __cplusplus
}
#endif

#endif  //!_CP_CONFIG_H_
