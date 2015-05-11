/*
 *  log_pipe_dev.cpp - The open function for the CP log device.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-3-2 Zhang Ziyi
 *  Initial version.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "log_pipe_hdl.h"

int LogPipeHandler::open_devices()
{
	char prop_val[PROPERTY_VALUE_MAX];
	int err = -1;

	// Get path from property
	switch (m_type) {
	case CT_WCDMA:
		property_get(MODEM_W_DIAG_PROPERTY, prop_val, "");
		if (prop_val[0]) {
			m_fd = open(prop_val, O_RDWR | O_NONBLOCK);
			if (m_fd >= 0) {
				m_dump_fd = m_fd;
				err = 0;
			}
		}
		break;
	case CT_TD:
		property_get(MODEM_TD_LOG_PROPERTY, prop_val, "");
		if (prop_val[0]) {
			m_fd = open(prop_val, O_RDONLY | O_NONBLOCK);
			property_get(MODEM_TD_DIAG_PROPERTY,
				     prop_val, "");
			if (-1 == m_fd) {
				if (prop_val[0]) {
					m_fd = open(prop_val, O_RDWR | O_NONBLOCK);
					if (m_fd >= 0) {
						m_dump_fd = m_fd;
						err = 0;
					}
				}
			} else {
				if (prop_val[0]) {
					m_dump_fd = open(prop_val, O_RDWR | O_NONBLOCK);
					if (m_dump_fd >= 0) {
						err = 0;
					}
				}
				if (-1 == err) {
					close(m_fd);
					m_fd = -1;
				}
			}
		}
		break;
	case CT_3MODE:
		property_get(MODEM_TL_LOG_PROPERTY, prop_val, "");
		if (prop_val[0]) {
			m_fd = open(prop_val, O_RDONLY | O_NONBLOCK);
			property_get(MODEM_TL_DIAG_PROPERTY, prop_val, "");
			if (m_fd >= 0) {
				if (prop_val[0]) {
					m_dump_fd = open(prop_val, O_RDWR | O_NONBLOCK);
				}
				if (m_dump_fd >= 0) {
					err = 0;
				} else {
					close(m_fd);
					m_fd = -1;
				}
			} else if (prop_val[0]) {
				m_fd = open(prop_val, O_RDWR | O_NONBLOCK);
				if (m_fd >= 0) {
					m_dump_fd = m_fd;
					err = 0;
				}
			}
		}
		break;
	case CT_4MODE:
		property_get(MODEM_FL_LOG_PROPERTY, prop_val, "");
		if (prop_val[0]) {
			m_fd = open(prop_val, O_RDONLY | O_NONBLOCK);
			property_get(MODEM_FL_DIAG_PROPERTY, prop_val, "");
			if (m_fd >= 0) {
				if (prop_val[0]) {
					m_dump_fd = open(prop_val, O_RDWR | O_NONBLOCK);
				}
				if (m_dump_fd >= 0) {
					err = 0;
				} else {
					close(m_fd);
					m_fd = -1;
				}
			} else if (prop_val[0]) {
				m_fd = open(prop_val, O_RDWR | O_NONBLOCK);
				if (m_fd >= 0) {
					m_dump_fd = m_fd;
					err = 0;
				}
			}
		}
		break;
	case CT_5MODE:
		property_get(MODEM_L_LOG_PROPERTY, prop_val, "");
		if (prop_val[0]) {
			m_fd = open(prop_val, O_RDONLY | O_NONBLOCK);
			property_get(MODEM_L_DIAG_PROPERTY, prop_val, "");
			if (m_fd >= 0) {
				if (prop_val[0]) {
					m_dump_fd = open(prop_val,
							 O_RDWR | O_NONBLOCK);
				}
				err = 0;
			} else if (prop_val[0]) {
				m_fd = open(prop_val, O_RDWR | O_NONBLOCK);
				if (m_fd >= 0) {
					m_dump_fd = m_fd;
					err = 0;
				}
			}
		} else {
			property_get(MODEM_L_DIAG_PROPERTY, prop_val, "");
			if (prop_val[0]) {
				m_fd = open(prop_val, O_RDWR | O_NONBLOCK);
				if (m_fd >= 0) {
					m_dump_fd = m_fd;
					err = 0;
				}
			}
		}
		break;
	case CT_WCN:
		property_get(MODEM_WCN_DIAG_PROPERTY, prop_val, "");
		if (prop_val[0]) {
			m_fd = open(prop_val, O_RDWR | O_NONBLOCK);
			if (m_fd >= 0) {
				m_dump_fd = m_fd;
				err = 0;
			}
		}
		break;
	default:  // Unknown
		break;
	}

	return err;
}

int LogPipeHandler::open_dump_device()
{
	char prop_val[PROPERTY_VALUE_MAX];
	int err = -1;

	// Get path from property
	prop_val[0] = '\0';
	switch (m_type) {
	case CT_WCDMA:
		property_get(MODEM_W_DIAG_PROPERTY, prop_val, "");
		break;
	case CT_TD:
		property_get(MODEM_TD_DIAG_PROPERTY, prop_val, "");
		break;
	case CT_3MODE:
		property_get(MODEM_TL_DIAG_PROPERTY, prop_val, "");
		break;
	case CT_4MODE:
		property_get(MODEM_FL_DIAG_PROPERTY, prop_val, "");
		break;
	case CT_5MODE:
		property_get(MODEM_L_DIAG_PROPERTY, prop_val, "");
		break;
	case CT_WCN:
		property_get(MODEM_WCN_DIAG_PROPERTY, prop_val, "");
		break;
	default:  // Unknown
		break;
	}

	if (prop_val[0]) {
		m_dump_fd = open(prop_val, O_RDWR | O_NONBLOCK);
		if (m_dump_fd >= 0) {
			err = 0;
		}
	}

	return err;
}
