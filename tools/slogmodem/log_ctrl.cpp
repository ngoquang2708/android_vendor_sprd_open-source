/*
 *  log_ctrl.cpp - The log controller class implementation.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#ifdef HOST_TEST_
	#include "prop_test.h"
#else
	#include <cutils/properties.h>
#endif

#include "def_config.h"
#include "log_ctrl.h"
#ifdef EXTERNAL_WCN
	#include "ext_wcn_log_hdl.h"
#else
	#include "int_wcn_log_hdl.h"
#endif
#include "slog_config.h"

LogController::LogController()
	:m_config {0},
	 m_save_md {false},
	 m_cli_mgr {0},
	 m_modem_state {0},
	 m_wcn_state {0},
	 m_file_mgr {this}
{
}

LogController::~LogController()
{
	delete m_wcn_state;
	delete m_modem_state;
	clear_log_pipes();
	delete m_cli_mgr;
}

LogPipeHandler* LogController::create_handler(const LogConfig::ConfigEntry* e)
{
	LogPipeHandler* log_pipe;

	if (CT_WCN == e->type) {
#ifdef EXTERNAL_WCN
		log_pipe = new ExtWcnLogHandler(this, &m_multiplexer, e);
#else
		log_pipe = new IntWcnLogHandler(this, &m_multiplexer, e);
#endif
	} else {
		log_pipe = new LogPipeHandler(this, &m_multiplexer, e);
	}
	// Set max log file size
	size_t limit = (m_config->max_log_file() << 20);
	log_pipe->set_log_limit(limit);
	if (e->enable) {
		int err = log_pipe->start();
		if (err < 0) {
			err_log("slogcp: start %s log failed",
				ls2cstring(e->modem_name));
		}
	}
	return log_pipe;
}

void LogController::clear_log_pipes()
{
	for(LogPipeIter it = m_log_pipes.begin();
	    it != m_log_pipes.end(); ++it) {
		delete *it;
	}
	m_log_pipes.clear();
}

int LogController::init(LogConfig* config)
{
	// Init the /data statistics
	uint64_t log_sz = config->max_data_part_size();
	log_sz <<= 20;
	m_data_part_stat.set_max_size(log_sz);
	m_data_part_stat.set_overwrite(config->overwrite_old_log());
	int err = mkdir("/data/modem_log",
			S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	if (err && EEXIST != errno) {
		return -1;
	}
	if (m_data_part_stat.init("/data/modem_log")) {
		err_log("init /data partition stat failed");
		return -1;
	}

	log_sz = config->max_sd_size();
	log_sz <<= 20;
	m_sd_stat.set_max_size(log_sz);
	m_sd_stat.set_overwrite(config->overwrite_old_log());

	// Initialize the file manager
	err = m_file_mgr.init(config->stor_pos(), config->sd_top_dir());
	if (err < 0) {
		return -1;
	}
	if (err > 0) {  // External SD card is available
		LogString sd_top;
		m_file_mgr.sd_root(sd_top);
		if (m_sd_stat.init(ls2cstring(sd_top))) {
			err_log("init SD stat failed");
			return -1;
		}
	}

	// Create LogPipeHandlers according to settings in config
	// And add them to m_log_pipes
	m_config = config;
	m_save_md = config->md_enabled();

	m_multiplexer.set_check_timeout(3000);

	const LogConfig::ConfigList& conf_list = config->get_conf();
	for(LogConfig::ConstConfigIter it = conf_list.begin();
	    it != conf_list.end(); ++it) {
		LogConfig::ConfigEntry* pc = *it;
		LogPipeHandler* handler = create_handler(pc);
		if (handler) {
			m_log_pipes.push_back(handler);
		}
	}

	// Server socket for clients (Engineering mode)
	m_cli_mgr = new ClientManager(this, &m_multiplexer);
	if (m_cli_mgr->init(SLOG_MODEM_SERVER_SOCK_NAME) < 0) {
		delete m_cli_mgr;
		m_cli_mgr = 0;
	}

	// Connection to modemd/wcnd
	init_state_handler(m_modem_state, MODEM_SOCKET_NAME);
#ifdef EXTERNAL_WCN
	init_wcn_state_handler(m_wcn_state, WCN_SOCKET_NAME);
#else
	init_wcn_state_handler(m_wcn_state, WCN_SOCKET_NAME);
#endif

	return 0;
}

void LogController::init_state_handler(ModemStateHandler*& handler,
				       const char* serv_name)
{
	ModemStateHandler* p = new ModemStateHandler(this, &m_multiplexer,
						     serv_name);

	int err = p->init();
	if (err < 0) {
		delete p;
		err_log("slogcp: connect to %s error", serv_name);
	} else {
		handler = p;
	}
}

template<typename T>
void LogController::init_wcn_state_handler(T*& handler,
					   const char* serv_name)
{
	T* p = new T(this, &m_multiplexer, serv_name);

	int err = p->init();
	if (err < 0) {
		delete p;
		err_log("connect to %s error", serv_name);
	} else {
		handler = p;
	}
}

int LogController::run()
{
	return m_multiplexer.run();
}

LogStat* LogController::get_cur_stat()
{
	LogStat* cur_stat;

	switch (m_file_mgr.current_media()) {
	case FileManager::LM_INTERNAL:
		cur_stat = &m_data_part_stat;
		break;
	case FileManager::LM_EXTERNAL:
		cur_stat = &m_sd_stat;
		break;
	default:
		cur_stat = 0;
		break;
	}

	return cur_stat;
}

bool LogController::check_storage()
{
	bool changed = false;
	LogStat* old_stat = get_cur_stat();

	if (m_file_mgr.check_media(m_sd_stat)) {
		LogStat* log_stat = get_cur_stat();
		for (LogList<LogPipeHandler*>::iterator it = m_log_pipes.begin();
		     it != m_log_pipes.end();
		     ++it) {
			LogPipeHandler* p = *it;
			if (p->enabled()) {
				p->change_log_file(m_file_mgr.get_cp_par_dir(),
						   log_stat);
			}
		}
		// Change the stat to new dir
		if (old_stat) {
			old_stat->end_stat();
		}
		log_stat->change_dir(m_file_mgr.get_cp_par_dir());
		changed = true;
	}

	return changed;
}

void LogController::process_cp_alive(CpType type)
{
	// Open on alive
	LogList<LogPipeHandler*>::iterator it;

	for (it = m_log_pipes.begin(); it != m_log_pipes.end(); ++it) {
		LogPipeHandler* p = *it;
		if (p->type() == type) {
			p->open_on_alive();
			break;
		}
	}
}

void LogController::process_cp_assert(CpType type)
{
	LogList<LogPipeHandler*>::iterator it = find_log_handler(m_log_pipes,
								 type);
	if (it == m_log_pipes.end()) {
		err_log("slogcp: unknown MODEM type %d asserts", type);
		return;
	}

	info_log("CP %s assert", ls2cstring((*it)->name()));

	if (FileManager::LM_NONE == m_file_mgr.current_media()) {
		if (!m_file_mgr.check_media(m_sd_stat)) {
			return;
		}
	}

	LogPipeHandler* log_hdl = *it;

	if (!log_hdl->enabled()) {
		LogStat* log_stat = get_cur_stat();
		log_hdl->change_log_file(m_file_mgr.get_cp_par_dir(),
					 log_stat);
	}
	log_hdl->process_assert(m_save_md);
}

void LogController::process_cp_blocked(CpType type)
{
	process_cp_assert(type);
}

int LogController::save_mini_dump(const LogString& par_dir,
				  const struct tm& lt)
{
	char md_name[80];

	snprintf(md_name, 80,
		 "/mini_dump_%04d-%02d-%02d_%02d-%02d-%02d.bin",
		 lt.tm_year + 1900,
		 lt.tm_mon + 1,
		 lt.tm_mday,
		 lt.tm_hour,
		 lt.tm_min,
		 lt.tm_sec);
	LogString md_fn = par_dir + md_name;

	return FileManager::copy_file(MINI_DUMP_SRC_FILE, ls2cstring(md_fn));
}

int LogController::save_sipc_dump(const LogString& par_dir,
				  const struct tm& lt)
{
	char fn[64];
	int year = lt.tm_year + 1900;
	int mon = lt.tm_mon + 1;

	// /d/sipc/smsg
	snprintf(fn, 64, "/smsg_%04d-%02d-%02d_%02d-%02d-%02d.log",
		 year, mon, lt.tm_mday,
		 lt.tm_hour, lt.tm_min, lt.tm_sec);
	LogString file_path = par_dir + fn;
	int err = 0;

	err = FileManager::copy_file(DEBUG_SMSG_PATH, ls2cstring(file_path));
	if (err) {
		err_log("slogcp: save SIPC smsg info failed");
	}

	// /d/sipc/sbuf
	snprintf(fn, 64, "/sbuf_%04d-%02d-%02d_%02d-%02d-%02d.log",
		 year, mon, lt.tm_mday,
		 lt.tm_hour, lt.tm_min, lt.tm_sec);
	file_path = par_dir + fn;
	err = FileManager::copy_file(DEBUG_SBUF_PATH, ls2cstring(file_path));
	if (err) {
		err_log("slogcp: save SIPC sbuf info failed");
	}

	// /d/sipc/sblock
	snprintf(fn, 64, "/sblock_%04d-%02d-%02d_%02d-%02d-%02d.log",
		 year, mon, lt.tm_mday,
		 lt.tm_hour, lt.tm_min, lt.tm_sec);
	file_path = par_dir + fn;
	err = FileManager::copy_file(DEBUG_SBLOCK_PATH, ls2cstring(file_path));
	if (err) {
		err_log("slogcp: save SIPC sblock info failed");
	}

	return err;
}

LogList<LogPipeHandler*>::iterator
LogController::find_log_handler(LogList<LogPipeHandler*>& handlers,
				CpType t)
{
	LogList<LogPipeHandler*>::iterator it;

	for (it = handlers.begin(); it != handlers.end(); ++it) {
		if (t == (*it)->type()) {
			break;
		}
	}

	return it;
}

int LogController::enable_log(const CpType* cps, size_t num)
{
	int mod_num = 0;
	size_t i;
	LogStat* log_stat = get_cur_stat();

	for (i = 0; i < num; ++i) {
		LogList<LogPipeHandler*>::iterator it = find_log_handler(m_log_pipes,
									 cps[i]);
		if (it != m_log_pipes.end()) {
			LogPipeHandler* p = *it;
			if (!p->enabled()) {
				p->change_log_file(m_file_mgr.get_cp_par_dir(),
						   log_stat);
				p->start();
				m_config->enable_log(p->type());
				++mod_num;
			}
		}
	}

	if (mod_num) {
		int err = m_config->save();
		if (err) {
			err_log("save config file failed");
		}
	}

	return 0;
}

int LogController::disable_log(const CpType* cps, size_t num)
{
	int mod_num = 0;
	size_t i;

	for (i = 0; i < num; ++i) {
		LogList<LogPipeHandler*>::iterator it = find_log_handler(m_log_pipes,
									 cps[i]);
		if (it != m_log_pipes.end()) {
			LogPipeHandler* p = *it;
			if (p->enabled()) {
				p->stop();
				m_config->enable_log(p->type(), false);
				++mod_num;
			}
		}
	}

	if (mod_num) {
		int err = m_config->save();
		if (err) {
			err_log("save config file failed");
		}
	}

	return 0;
}

int LogController::enable_md()
{
	if (!m_save_md) {
		m_save_md = true;
		m_config->enable_md();
		if (m_config->save()) {
			err_log("save config file failed");
		}
	}

	return 0;
}

int LogController::disable_md()
{
	if (m_save_md) {
		m_save_md = false;
		m_config->enable_md(false);
		if (m_config->save()) {
			err_log("save config file failed");
		}
	}

	return 0;
}

int LogController::mini_dump()
{
	//{Debug
	info_log("mini dump");

	if (FileManager::LM_NONE == m_file_mgr.current_media()) {
		if (!m_file_mgr.check_media(m_sd_stat)) {
			return -1;
		}
	}

	time_t t = time(0);
	struct tm lt;

	if (static_cast<time_t>(-1) == t || !localtime_r(&t, &lt)) {
		return -1;
	}
	return save_mini_dump(m_file_mgr.get_cp_par_dir(), lt);
}

int LogController::reload_slog_conf()
{
	// Reload the /data/local/tmp/slog/slog.conf, and only update
	// the CP enable states and log file size.
	SLogConfig slogc;

	if (slogc.read_config(TMP_SLOG_CONFIG)) {
		err_log("load slog.conf failed");
		return -1;
	}

	size_t sz;
	if (slogc.get_file_size(sz)) {
		for (auto it = m_log_pipes.begin();
		     it != m_log_pipes.end();
		     ++it) {
			// sz is in unit of MB
			(*it)->set_log_limit(sz << 20);
		}
		m_config->set_log_file_size(sz);
	}

	const SLogConfig::ConfigList& clist = slogc.get_conf();
	SLogConfig::ConstConfigIter it;

	for (it = clist.begin(); it != clist.end(); ++it) {
		const SLogConfig::ConfigEntry* p = *it;
		LogList<LogPipeHandler*>::iterator it_cp = find_log_handler(m_log_pipes,
									    p->type);
		if (it_cp != m_log_pipes.end()) {
			LogPipeHandler* pipe_hdl = *it_cp;
			if (pipe_hdl->enabled() != p->enable) {
				info_log("%s CP %s",
					 p->enable ? "enable" : "disable",
					 ls2cstring(pipe_hdl->name()));
				if (p->enable) {
					pipe_hdl->start();
				} else {
					pipe_hdl->stop();
				}
				m_config->enable_log(p->type, p->enable);
			}
		}
	}

	if (m_config->dirty()) {
		if (m_config->save()) {
			err_log("save config failed");
		}
	}

	return 0;
}

size_t LogController::get_log_file_size() const
{
	return m_config->max_log_file();
}

int LogController::set_log_file_size(size_t len)
{
	LogList<LogPipeHandler*>::iterator it;

	for (it = m_log_pipes.begin(); it != m_log_pipes.end(); ++it) {
		(*it)->set_log_limit(len << 20);
	}

	m_config->set_log_file_size(len);
	if (m_config->dirty()) {
		if (m_config->save()) {
			err_log("save config file failed");
		}
	}

	return 0;
}

int LogController::set_log_overwrite(bool en)
{
	m_data_part_stat.set_overwrite(en);
	m_sd_stat.set_overwrite(en);

	m_config->set_overwrite(en);
	if (m_config->dirty()) {
		if (m_config->save()) {
			err_log("save config file failed");
		}
	}

	return 0;
}

size_t LogController::get_data_part_size() const
{
	return m_config->max_data_part_size();
}

int LogController::set_data_part_size(size_t sz)
{
	uint64_t byte_sz = sz;

	byte_sz <<= 20;

	m_data_part_stat.set_max_size(byte_sz);

	m_config->set_data_part_size(sz);
	if (m_config->dirty()) {
		if (m_config->save()) {
			err_log("save config file failed");
		}
	}

	return 0;
}

size_t LogController::get_sd_size() const
{
	return m_config->max_sd_size();
}

int LogController::set_sd_size(size_t sz)
{
	uint64_t byte_sz = sz;

	byte_sz <<= 20;

	m_sd_stat.set_max_size(byte_sz);

	m_config->set_sd_size(sz);
	if (m_config->dirty()) {
		if (m_config->save()) {
			err_log("save config file failed");
		}
	}

	return 0;
}

bool LogController::get_log_overwrite() const
{
	return m_config->overwrite_old_log();
}

int LogController::check_dir_exist()
{
	int ret = m_file_mgr.check_dir_exist();

	if (1 == ret) {
		LogStat* log_stat = get_cur_stat();
		// Timed directory changed.
		for (LogPipeIter it = m_log_pipes.begin();
		     it != m_log_pipes.end();
		     ++it) {
			LogPipeHandler* p = *it;
			if (p->enabled()) {
				p->change_log_file(m_file_mgr.get_cp_par_dir(),
						   log_stat);
			}
		}
	}

	return ret;
}
