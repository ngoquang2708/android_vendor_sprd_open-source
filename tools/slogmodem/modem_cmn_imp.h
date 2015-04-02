/*
 *  modem_cmn_imp.h - Internal declarations for modem_common.c.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-3-26 Zhang Ziyi
 *      Initial version.
 */

#ifndef _MODEM_CMN_IMP_H_
#define _MODEM_CMN_IMP_H_

#ifdef __cplusplus
extern "C" {
#endif

struct RenameEntry
{
	int num;
	size_t num_len;
	char name[MAX_NAME_LEN];
	char new_name[MAX_NAME_LEN];
	struct RenameEntry* prev;
	struct RenameEntry* next;
};

struct RenameList
{
	struct RenameEntry* head;
	struct RenameEntry* tail;
};

#ifdef __cplusplus
}
#endif

#endif  //!_MODEM_CMN_IMP_H_
