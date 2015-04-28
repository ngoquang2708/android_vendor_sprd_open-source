/*
 *  client_mgr.cpp - The client manager class implementation.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef HOST_TEST_
	#include "sock_test.h"
#else
	#include <cutils/sockets.h>
#endif

#include "client_mgr.h"
#include "log_ctrl.h"
#include "multiplexer.h"

ClientManager::ClientManager(LogController* ctrl, Multiplexer* multi)
	:FdHandler(-1, ctrl, multi)
{
}

ClientManager::~ClientManager()
{
	clear_clients();
}

int ClientManager::init(const char* serv_name)
{
	int err = -1;

	m_fd = socket_local_server(serv_name,
				   ANDROID_SOCKET_NAMESPACE_ABSTRACT,
				   SOCK_STREAM);

	if (m_fd >= 0) {
		long flags = fcntl(m_fd, F_GETFL);
		flags |= O_NONBLOCK;
		err = fcntl(m_fd, F_SETFL, flags);
		if (-1 == err) {
			close(m_fd);
			m_fd = -1;
			err_log("set server socket non-block error");
		} else {
			multiplexer()->register_fd(this, POLLIN);
		}
	} else {
		err_log("can not create server socket");
	}

	return err;
}

void ClientManager::clear_clients()
{
	LogList<ClientHandler*>::iterator it;

	for (it = m_clients.begin(); it != m_clients.end(); ++it) {
		ClientHandler* p = *it;
		delete p;
	}

	m_clients.clear();
}

void ClientManager::process(int events)
{
	// Server socket readable

	int sock = accept(m_fd, 0, 0);

	if(sock >= 0) {  // Client connection established
		// The socket returned by accept does not inherit file
		// status flags such O_NONBLOCK on Linux, so we has to
		// set it manually.
		long flags = fcntl(sock, F_GETFL);
		flags |= O_NONBLOCK;
		int err = fcntl(sock, F_SETFL, flags);
		if (-1 == err) {
			err_log("set accepted socket to O_NONBLOCK error");
		}
		ClientHandler* client = new ClientHandler(sock,
							  controller(),
							  multiplexer(),
							  this);
		// Add the client to the multiplexer
		m_clients.push_back(client);
		multiplexer()->register_fd(client, POLLIN);
		info_log("new client accepted");
		// Only support one client connection
		multiplexer()->unregister_fd(this, POLLIN);
	}
}

void ClientManager::process_client_disconn(ClientHandler* client)
{
	ll_remove(m_clients, client);
	delete client;
	err_log("client disconnected");
	if (m_clients.empty()) {
		add_events(POLLIN);
	}
}
