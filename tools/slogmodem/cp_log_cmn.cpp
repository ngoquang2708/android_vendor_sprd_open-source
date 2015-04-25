/*
 *  cp_log_cmn.cpp - Common functions for the CP log and dump program.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#include <time.h>
#include "cp_log_cmn.h"

int get_timezone()
{
	time_t t1;
	struct tm* p;
	struct tm local_tm;
	struct tm gm_tm;

	t1 = time(0);
	if (static_cast<time_t>(-1) == t1) {
		return 0;
	}
	p = gmtime_r(&t1, &gm_tm);
	if (!p) {
		return 0;
	}
	p = localtime_r(&t1, &local_tm);
	if (!p) {
		return 0;
	}

	int tz = local_tm.tm_hour - gm_tm.tm_hour;
	if (tz > 12) {
		tz -= 24;
	} else if (tz < -12) {
		tz += 24;
	}

	return tz;
}
