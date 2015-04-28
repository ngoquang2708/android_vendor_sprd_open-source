/*
 *  client_hdl.cpp - The base class implementation for file descriptor
 *                   handler.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#include <poll.h>
#include <cstring>

#include "client_hdl.h"
#include "client_mgr.h"
#include "client_req.h"
#include "parse_utils.h"
#include "log_ctrl.h"

ClientHandler::ClientHandler(int sock,
			     LogController* ctrl,
			     Multiplexer* multiplexer,
			     ClientManager* mgr)
	:DataProcessHandler(sock, ctrl, multiplexer, CLIENT_BUF_SIZE),
	 m_mgr(mgr)
{
}

const uint8_t* ClientHandler::search_end(const uint8_t* req, size_t len)
{
	const uint8_t* endp = req + len;

	while (req < endp) {
		if ('\0' == *req || '\n' == *req) {
			break;
		}
		++req;
	}

	if (req == endp) {
		req = 0;
	}

	return req;
}

int ClientHandler::process_data()
{
	const uint8_t* start = m_buffer.buffer;
	const uint8_t* end = m_buffer.buffer + m_buffer.data_len;
	size_t rlen = m_buffer.data_len;

	info_log("%d bytes", static_cast<int>(rlen));

	while (start < end) {
		const uint8_t* p1 = search_end(start, rlen);
		if (!p1) {  // Not complete request
			break;
		}
		process_req(start, p1 - start);
		start = p1 + 1;
		rlen = end - start;
	}

	if (rlen && start != m_buffer.buffer) {
		memmove(m_buffer.buffer, start, rlen);
	}
	m_buffer.data_len = rlen;

	return 0;
}

void ClientHandler::process_req(const uint8_t* req, size_t len)
{
	size_t tok_len;
	const uint8_t* endp = req + len;
	const uint8_t* token = get_token(req, len, tok_len);

	info_log("client data %u bytes", static_cast<unsigned>(len));

	if (!token) {
		// Empty line: ignore it.
		return;
	}

	// What request?
	bool known_req = false;
	req = token + tok_len;
	len = endp - req;
	switch (tok_len) {
	case 7:
		if (!memcmp(token, "slogctl", 7)) {
			proc_slogctl(req, len);
			known_req = true;
		}
		break;
	case 9:
		if (!memcmp(token, "ENABLE_MD", 9)) {
			proc_enable_md(req, len);
			known_req = true;
		} else if (!memcmp(token, "MINI_DUMP", 9)) {
			proc_mini_dump(req, len);
			known_req = true;
		}
		break;
	case 10:
		if (!memcmp(token, "ENABLE_LOG", 10)) {
			proc_enable_log(req, len);
			known_req = true;
		} else if (!memcmp(token, "DISABLE_MD", 10)) {
			proc_disable_md(req, len);
			known_req = true;
		}
		break;
	case 11:
		if (!memcmp(token, "DISABLE_LOG", 11)) {
			proc_disable_log(req, len);
			known_req = true;
		}
		break;
	default:
		break;
	}

	if (!known_req) {
		err_log("unknown request");

		send_response(m_fd, REC_UNKNOWN_REQ);
	}
}

void ClientHandler::proc_slogctl(const uint8_t* req, size_t len)
{
	const uint8_t* tok;
	size_t tlen;

	tok = get_token(req, len, tlen);
	if (!tok) {
		send_response(m_fd, REC_UNKNOWN_REQ);
		return;
	}

	if (5 == tlen && !memcmp(tok, "clear", 5)) {
		info_log("remove CP log");
		// Delete all logs
		controller()->file_manager()->clear();
		send_response(m_fd, REC_SUCCESS);
	} else if (6 == tlen && !memcmp(tok, "reload", 6)) {
		info_log("reload slog.conf");
		// Reload slog.conf and update CP log and log file size
		int ret = controller()->reload_slog_conf();
		ResponseErrorCode code;
		if (!ret) {
			code = REC_SUCCESS;
		} else {
			code = REC_FAILURE;
		}
		send_response(m_fd, code);
	} else {
		send_response(m_fd, REC_UNKNOWN_REQ);
	}
}

void ClientHandler::proc_enable_log(const uint8_t* req, size_t len)
{
	// Parse MODEM types
	ModemSet ms;
	int err = parse_modem_set(req, len, ms);

	if (err || !ms.num) {
		send_response(m_fd, REC_INVAL_PARAM);
		return;
	}

	err = controller()->enable_log(ms.modems, ms.num);
	ResponseErrorCode ec = REC_SUCCESS;
	if (err) {
		ec = REC_FAILURE;
	}

	send_response(m_fd, ec);
}

void ClientHandler::proc_disable_log(const uint8_t* req, size_t len)
{
	// Parse MODEM types
	ModemSet ms;
	int err = parse_modem_set(req, len, ms);

	if (err || !ms.num) {
		send_response(m_fd, REC_INVAL_PARAM);
		return;
	}

	err = controller()->disable_log(ms.modems, ms.num);
	ResponseErrorCode ec = REC_SUCCESS;
	if (err) {
		ec = REC_FAILURE;
	}

	send_response(m_fd, ec);
}

void ClientHandler::proc_enable_md(const uint8_t* req, size_t len)
{
	size_t i;

	for (i = 0; i < len; ++i) {
		uint8_t c = req[i];
		if (' ' != c || '\t' != c) {
			break;
		}
	}
	if (i < len) {
		send_response(m_fd, REC_INVAL_PARAM);
		return;
	}

	int err = controller()->enable_md();
	ResponseErrorCode ec = REC_SUCCESS;
	if (err) {
		ec = REC_FAILURE;
	}

	send_response(m_fd, ec);
}

void ClientHandler::proc_disable_md(const uint8_t* req, size_t len)
{
	size_t i;

	for (i = 0; i < len; ++i) {
		uint8_t c = req[i];
		if (' ' != c || '\t' != c) {
			break;
		}
	}
	if (i < len) {
		send_response(m_fd, REC_INVAL_PARAM);
		return;
	}

	int err = controller()->disable_md();
	ResponseErrorCode ec = REC_SUCCESS;
	if (err) {
		ec = REC_FAILURE;
	}

	send_response(m_fd, ec);
}

void ClientHandler::proc_mini_dump(const uint8_t* req, size_t len)
{
	size_t i;

	for (i = 0; i < len; ++i) {
		uint8_t c = req[i];
		if (' ' != c || '\t' != c) {
			break;
		}
	}
	if (i < len) {
		send_response(m_fd, REC_INVAL_PARAM);
		return;
	}

	int err = controller()->mini_dump();
	ResponseErrorCode ec = REC_SUCCESS;
	if (err) {
		ec = REC_FAILURE;
	}

	send_response(m_fd, ec);
}

void ClientHandler::process_conn_closed()
{
	// Parse the request and inform the LogController to execute it.
	del_events(POLLIN);
	m_fd = -1;
	// Inform ClientManager the connection is closed
	m_mgr->process_client_disconn(this);
}

void ClientHandler::process_conn_error(int err)
{
	ClientHandler::process_conn_closed();
}
