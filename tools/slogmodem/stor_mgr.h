/*
 *  stor_mgr.h - storage manager.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-5-14 Zhang Ziyi
 *  Initial version.
 */
#ifndef _STOR_MGR_H_
#define _STOR_MGR_H_

#include "media_stor.h"

class LogPipeHandler;
class CpStorage;

class StorageManager
{
public:
	enum MediaType
	{
		MT_NONE,
		MT_INTERNAL,
		MT_SD_CARD
	};

	StorageManager();

	/*  init - initialize the storage manager.
	 *  @sd_top_dir: top directory of SD card.
	 *
	 *  Return 0 on success, -1 on failure.
	 */
	int init(const LogString& sd_top_dir);

	void set_overwrite(bool ow);
	bool overwrite() const
	{
		return m_overwrite;
	}
	void set_file_size_limit(size_t sz);
	void set_data_part_limit(uint64_t sz);
	void set_sd_limit(uint64_t sz);

	/*  create_storage - create CP storage handle.
	 *  @cp_log: the LogPipeHandler object reference.
	 *
	 *  Return the CpStorage pointer on success, NULL on failure.
	 */
	CpStorage* create_storage(LogPipeHandler& cp_log);
	void delete_storage(CpStorage* stor);

	/*  request_file - request to create a CP dir/log file on the
	 *                 current media.
	 *  @cp: LogPipeHandler reference.
	 *
	 *  Return the LogFile pointer on success, NULL on failure.
	 */
	LogFile* request_file(LogPipeHandler& cp);

	MediaStorage* get_media_stor()
	{
		return get_media_stor(m_cur_type);
	}

	/*  check_media_change - check the media change and create the
	 *                       CP set directory.
	 *
	 *  Return true if the media changed, false otherwise.
	 */
	bool check_media_change();

	/*  clear - delete all logs.
	 */
	void clear();

private:
	// Current storage media
	MediaType m_cur_type;
	MediaStorage m_data_part;
	MediaStorage m_sd_card;
	// Overwrite?
	bool m_overwrite;
	// CP storage handle
	LogList<CpStorage*> m_cp_handles;

	MediaStorage* get_media_stor(MediaType t)
	{
		MediaStorage* m;

		switch (t) {
		case MT_INTERNAL:
			m = &m_data_part;
			break;
		case MT_SD_CARD:
			m = &m_sd_card;
			break;
		default:
			m = 0;
			break;
		}
		return m;
	}

	void stop_all_cps();

	static bool get_sd_state();
};

#endif  // !_STOR_MGR_H_

