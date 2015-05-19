/*
 *  stor_mgr.cpp - storage manager.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-5-14 Zhang Ziyi
 *  Initial version.
 */

#include <cstdlib>
#ifdef HOST_TEST_
	#include "prop_test.h"
#else
	#include <cutils/properties.h>
#endif
#include "stor_mgr.h"
#include "cp_stor.h"
#include "cp_set_dir.h"
#include "cp_dir.h"
#include "log_pipe_hdl.h"

StorageManager::StorageManager()
	:m_cur_type {MT_NONE},
	 m_data_part(LogString("/data")),
	 m_overwrite {true}
{
}

int StorageManager::init(const LogString& sd_top_dir)
{
	if (m_data_part.init()) {
		return -1;
	}

	int ret = 0;
	m_sd_card.set_top_dir(sd_top_dir);
	if (get_sd_state()) {  // SD card is present
		if (m_sd_card.init()) {
			ret = -1;
		}
	}

	return ret;
}

bool StorageManager::get_sd_state()
{
	char val[PROPERTY_VALUE_MAX];

	property_get("persist.storage.type", val, "");
	if (!val[0]) {
		return false;
	}

	unsigned long type;
	char* endp;

	type = strtoul(val, &endp, 0);
	if (1 != type) {
		return false;
	}

	property_get("init.svc.fuse_sdcard0", val, "");

	return !strcmp(val, "running");
}

void StorageManager::set_overwrite(bool ow)
{
	m_overwrite = ow;
	m_data_part.set_overwrite(ow);
	m_sd_card.set_overwrite(ow);
}

void StorageManager::set_file_size_limit(size_t sz)
{
	m_data_part.set_file_size_limit(sz);
	m_sd_card.set_file_size_limit(sz);
}

void StorageManager::set_data_part_limit(uint64_t sz)
{
	m_data_part.set_total_limit(sz);
}

void StorageManager::set_sd_limit(uint64_t sz)
{
	m_sd_card.set_total_limit(sz);
}

CpStorage* StorageManager::create_storage(LogPipeHandler& cp_log)
{
	CpStorage* cp_stor = new CpStorage(*this, cp_log);

	m_cp_handles.push_back(cp_stor);
	return cp_stor;
}

void StorageManager::delete_storage(CpStorage* stor)
{
	LogList<CpStorage*>::iterator it;

	for (it = m_cp_handles.begin(); it != m_cp_handles.end(); ++it) {
		CpStorage* cp = *it;
		if (stor == cp) {
			m_cp_handles.erase(it);
			delete stor;
			break;
		}
	}
}

LogFile* StorageManager::request_file(LogPipeHandler& cp)
{
	if (MT_NONE == m_cur_type) {
		return 0;
	}

	MediaStorage* m = get_media_stor(m_cur_type);
	CpDirectory* cp_dir = m->current_cp_set()->create_cp_dir(cp.name());

	if (!cp_dir) {
		return 0;
	}

	return cp_dir->create_log_file();
}

void StorageManager::stop_all_cps()
{
	LogList<CpStorage*>::iterator it;

	for (it = m_cp_handles.begin(); it != m_cp_handles.end(); ++it) {
		CpStorage* stor = *it;
		stor->stop();
	}
}

bool StorageManager::check_media_change()
{
	bool has_sd = get_sd_state();
	bool changed = false;

	if (has_sd) {  // SD is present
		if (MT_SD_CARD != m_cur_type) {
			if (MT_INTERNAL == m_cur_type) {
				stop_all_cps();
			}

			if (!m_sd_card.inited()) {
				if (m_sd_card.init()) {
					err_log("init SD card failed");
					return true;
				}
			}

			// Create CP set directory
			m_sd_card.create_cp_set();

			changed = true;

			m_cur_type = MT_SD_CARD;
		}
	} else {  // No SD card
		if (MT_INTERNAL != m_cur_type) {
			if (MT_SD_CARD == m_cur_type) {
				stop_all_cps();
			}

			// Create CP set directory
			m_data_part.create_cp_set();

			changed = true;

			m_cur_type = MT_INTERNAL;
		}
	}

	return changed;
}

void StorageManager::clear()
{
	stop_all_cps();
	m_data_part.clear();
	m_sd_card.clear();
	// Reset current media to MT_NONE so that the next request will
	// recreate the CP set directory on the appropriate media.
	m_cur_type = MT_NONE;
}
