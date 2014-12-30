
#include "common.h"
#include "LoggerThread.h"
#include "AprData.h"


#define JAVA_CRASH_KEY_WORDS "Force-killing crashed app"

struct queued_entry_t {
	union {
		unsigned char buf[LOGGER_ENTRY_MAX_LEN + 1] __attribute__((aligned(4)));
		struct logger_entry entry __attribute__((aligned(4)));
	};
	queued_entry_t* next;

	queued_entry_t() {
		next = NULL;
	}
};

static int cmp(queued_entry_t* a, queued_entry_t* b) {
	int n = a->entry.sec - b->entry.sec;
	if (n != 0) {
		return n;
	}
	return a->entry.nsec - b->entry.nsec;
}

struct log_device_t {
	char* device;
	bool binary;
	int fd;
	bool printed;
	char label;

	queued_entry_t* queue;
	log_device_t* next;

	log_device_t(char* d, bool b, char l) {
		device = d;
		binary = b;
		label = l;
		queue = NULL;
		next = NULL;
		printed = false;
	}

	void enqueue(queued_entry_t* entry) {
		if (this->queue == NULL) {
			this->queue = entry;
		} else {
			queued_entry_t** e = &this->queue;
			while (*e && cmp(entry, *e) >= 0) {
				e = &((*e)->next);
			}
			entry->next = *e;
			*e = entry;
		}
	}
};

LoggerThread::LoggerThread(AprData* aprData)
{
	m_aprData = aprData;

	m_devices = NULL;
	m_mode = O_RDONLY;
	m_devCount = 0;
}

LoggerThread::~LoggerThread()
{
	m_aprData = NULL;
}

void LoggerThread::Setup()
{
	APR_LOGD("LoggerThread::Setup()\n");
	int accessmode =
		(m_mode & O_RDONLY) ? R_OK : 0
		| (m_mode & O_WRONLY) ? W_OK : 0;
	// only add this if it's available
	if (0 == access("/dev/"LOGGER_LOG_SYSTEM, accessmode)) {
		m_devices = new log_device_t(strdup("/dev/"LOGGER_LOG_SYSTEM), false, 's');
		m_devCount = 1;
	}

	log_device_t* dev = m_devices;
	while (dev) {
		dev->fd = open(dev->device, m_mode);
		if (dev->fd < 0) {
			APR_LOGE("Unable to open log device '%s': %s\n",
					dev->device, strerror(errno));
			exit(EXIT_FAILURE);
		}
		dev = dev->next;
	}
}

void LoggerThread::Execute(void* arg)
{
	APR_LOGD("LoggerThread::Execute()\n");
	log_device_t* dev;
	int max = 0;
	int ret;
	int queued_lines = 0;
	bool sleep = false;

	int result;
	fd_set readset;

	for (dev = m_devices; dev; dev = dev->next) {
		if (dev->fd > max) {
			max = dev->fd;
		}
	}

	while (1) {
		do {
			// If we oversleep it's ok, i.e. ignore EINTR.
			timeval timeout = { 0, 5000 /* 5ms */ };
			FD_ZERO(&readset);
			for (dev=m_devices; dev; dev = dev->next) {
				FD_SET(dev->fd, &readset);
			}
			result = select(max + 1, &readset, NULL, NULL, sleep ? NULL : &timeout);
		} while (result == -1 && errno == EINTR);

		if (result >= 0) {
			for (dev=m_devices; dev; dev = dev->next) {
				if (FD_ISSET(dev->fd, &readset)) {
					queued_entry_t* entry = new queued_entry_t();
					/* NOTE: driver guarantees we read exactly one full entry */
					ret = read(dev->fd, entry->buf, LOGGER_ENTRY_MAX_LEN);
					if (ret < 0) {
						if (errno == EINTR) {
							delete entry;
							goto next;
						}
						if (errno == EAGAIN) {
							delete entry;
							break;
						}
						perror("logcat read");
						exit(EXIT_FAILURE);
					}
					else if (!ret) {
						fprintf(stderr, "read: Unexpected EOF!\n");
						exit(EXIT_FAILURE);
					}
					else if (entry->entry.len != ret - sizeof(struct logger_entry)) {
						fprintf(stderr, "read: unexpected length. Expected %d, got %d\n",
								entry->entry.len, ret - sizeof(struct logger_entry));
						exit(EXIT_FAILURE);
					}

					entry->entry.msg[entry->entry.len] = '\0';

					dev->enqueue(entry);
					++queued_lines;
				}
			}

			if (result == 0) {
				// we did our short timeout trick and there's nothing new
				// print everything we have and wait for more data
				sleep = true;
				while (true) {
					chooseFirst(m_devices, &dev);
					if (dev == NULL) {
						break;
					}
					printNextEntry(dev);

					--queued_lines;
				}
			} else {
				// print all that aren't the last in their list
				sleep = false;
				while (true) {
					chooseFirst(m_devices, &dev);
					if (dev == NULL || dev->queue->next == NULL) {
						break;
					}
					printNextEntry(dev);

					--queued_lines;
				}
			}
		}
next:
		;
	}
}

void LoggerThread::chooseFirst(log_device_t* dev, log_device_t** firstdev)
{
	for (*firstdev = NULL; dev != NULL; dev = dev->next)
	{
		if (dev->queue != NULL && (*firstdev == NULL || cmp(dev->queue, (*firstdev)->queue) < 0))
		{
			*firstdev = dev;
		}
	}
}

void LoggerThread::printNextEntry(log_device_t* dev)
{
	processBuffer(dev, &dev->queue->entry);
	skipNextEntry(dev);
}

void LoggerThread::skipNextEntry(log_device_t* dev)
{
	queued_entry_t* entry = dev->queue;
	dev->queue = entry->next;
	delete entry;
}

void LoggerThread::processBuffer(log_device_t* dev, struct logger_entry *buf)
{
	if (dev->binary) {

	} else {
		/*
		 * format: <priority:1><tag:N>\0<message:N>\0
		 *
		 * tag str
		 *   starts at buf->msg+1
		 * msg
		 *   starts at buf->msg+1+len(tag)+1
		 *
		 * The message may have been truncated by the kernel log driver.
		 * When that happens, we must null-terminate the message ourselves.
		 */
		if (buf->len < 3) {
			// An well-formed entry must consist of at least a priority
			// and two null characters
			fprintf(stderr, "+++ LOG: entry too small\n");
			return;
		}
		int msgStart = -1;
		int msgEnd = -1;
		int i;
		for (i = 1; i < buf->len; i++) {
			if (buf->msg[i] == '\0') {
				if (msgStart == -1) {
					msgStart = i + 1;
				} else {
					msgEnd = i;
					break;
				}
			}
		}

		if (msgStart == -1) {
			fprintf(stderr, "+++ LOG: malformed log message\n");
			return;
		}
		if (msgEnd == -1) {
			// incoming message not null-terminated; force it
			msgEnd = buf->len - 1;
			buf->msg[msgEnd] = '\0';
		}
		if(strstr(buf->msg + msgStart, JAVA_CRASH_KEY_WORDS)) {
			// JavaCrash
			m_aprData->setChanged();
			m_aprData->notifyObservers((void*)"java crash");
		}
	}
}

