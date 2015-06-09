/*
 *  cp_stor.cpp - CP storage handle.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-5-15 Zhang Ziyi
 *  Initial version.
 */

#include "cp_dir.h"
#include "cp_set_dir.h"
#include "cp_stor.h"
#include "log_file.h"
#include "log_pipe_hdl.h"
#include "stor_mgr.h"

CpStorage::CpStorage(StorageManager& stor_mgr, LogPipeHandler& cp)
	:m_stor_mgr(stor_mgr),
	 m_cp(cp),
	 m_new_log_cb {0},
	 m_new_dir_cb {0},
	 m_cur_file {0},
	 m_shall_stop {false}
{
}

CpStorage::~CpStorage()
{
	if (m_cur_file) {
		m_cur_file->close();
	}
}

ssize_t CpStorage::write(const void* data, size_t len)
{
	if (m_stor_mgr.check_media_change()) {
		if (m_cur_file) {
			m_cur_file->close();
			m_cur_file = 0;
		}
	}
	if (!m_cur_file) {  // Log file not created
		m_cur_file = m_stor_mgr.request_file(m_cp);
		if (!m_cur_file) {
			return -1;
		}
		if (m_new_dir_cb) {
			m_new_dir_cb(&m_cp);
		}
		if (m_new_log_cb) {
			m_new_log_cb(&m_cp, m_cur_file);
		}
	} else {  // Log file opened
		// Check whether the file exists
		MediaStorage* m = m_cur_file->dir()->cp_set_dir()->get_media();
		CpDirectory* cp_dir = m_cur_file->dir();

		if (!m_cur_file->exists()) {
			bool cp_dir_created = false;

			m_cur_file->close();
			cp_dir->file_removed(m_cur_file);
			m_cur_file = m->recreate_log_file(m_cp.name(),
							  cp_dir_created);
			if (!m_cur_file) {
				return -1;
			}
			if (cp_dir_created && m_new_dir_cb) {
				m_new_dir_cb(&m_cp);
			}
			if (m_new_log_cb) {
				m_new_log_cb(&m_cp, m_cur_file);
			}
		}
	}

	MediaStorage* ms = m_stor_mgr.get_media_stor();
	// It's full on last time write. Try to make more room.
	if (m_shall_stop) {
		if (ms->check_quota(m_cur_file->dir())) {
			return -1;
		}
		m_shall_stop = false;
	}

	ssize_t n = m_cur_file->write(data, len);

	if (n > 0) {
		if (m_cur_file->size() >= ms->file_size_limit()) {
			CpDirectory* cp_dir = m_cur_file->dir();
			cp_dir->close_log_file();
			m_cur_file = 0;
			cp_dir->rotate();
			m_cur_file = cp_dir->create_log_file();
		}
		if (ms->check_quota(m_cur_file->dir())) {
			m_shall_stop = true;
			m_cur_file->flush();
		}
	} else if (-2 == n) {  // Disk space full
		if (ms->process_disk_full(m_cur_file->dir())) {
			m_shall_stop = true;
			m_cur_file->flush();
		}
	}

	return n;
}

void CpStorage::stop()
{
	if (m_cur_file) {
		m_cur_file->close();
		m_cur_file = 0;
	}
}

LogFile* CpStorage::create_file(const LogString& fname, LogFile::LogType t)
{
	// Ensure the CP set directory is created
	m_stor_mgr.check_media_change();
	MediaStorage* ms = m_stor_mgr.get_media_stor();
	LogFile* f = 0;
	if (ms) {
		f = ms->create_file(m_cp.name(), fname, t);
	}

	return f;
}
