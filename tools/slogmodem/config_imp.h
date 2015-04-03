/*
 * config_imp.h - Internal declarations for config file parsing.
 *
 * Copyright (C) 2015 Spreadtrum Communications Inc., Ltd.
 *
 * History:
 * 2015-3-26 Zhang Ziyi
 * Initial version.
 */

#ifndef _CONFIG_IMP_H_
#define _CONFIG_IMP_H_

#ifdef __cplusplus
extern "C" {
#endif

struct StreamEntry
{
	char* name;
	int enable;
	size_t size;
	int level;
};

#ifdef __cplusplus
}
#endif

#endif // !_CONFIG_IMP_H_

