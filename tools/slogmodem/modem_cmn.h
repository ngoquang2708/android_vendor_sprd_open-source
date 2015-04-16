/*
 *  modem_cmn.h - Common function declarations for modem_common.c.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-3-26 Zhang Ziyi
 *      Initial version.
 */

#ifndef _MODEM_CMN_H_
#define _MODEM_CMN_H_

#include "slog_modem.h"

#ifdef __cplusplus
extern "C" {
#endif

void gen_logfile(char *filename, struct slog_info *info);
int open_device(char* path);
FILE* gen_outfd(struct slog_info* info);
void cp_file(char *path, char *new_path);
void log_size_handler(struct slog_info* info);

/*
 *  del_oldest_log - delete the oldest log file or log directory of the
 *                   specified CP.
 *  @cp: the CP whose oldest log is to be deleted.
 *
 *  Return 0 on success, -1 on error.
 */
int del_oldest_log(const struct slog_info* cp);

/*
 *  clear_log - delete all log directories, including those on internal
 *              storage and external storage.
 */
void clear_log(void);

#ifdef __cplusplus
}
#endif

#endif  //!_MODEM_CMN_H_
