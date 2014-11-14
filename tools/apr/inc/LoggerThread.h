
#ifndef LOGGERTHREAD_H
#define LOGGERTHREAD_H
#include "Thread.h"
#include "AprData.h"

struct log_device_t;

class LoggerThread : public Thread
{
public:
	LoggerThread(AprData* aprData);
	~LoggerThread();

protected:
	log_device_t* m_devices;
	void chooseFirst(log_device_t* dev, log_device_t** firstdev);
	void printNextEntry(log_device_t* dev);
	void skipNextEntry(log_device_t* dev);
	void processBuffer(log_device_t* dev, struct logger_entry *buf);

	int m_mode;
	int m_devCount;
	virtual void Setup();
	virtual void Execute(void* arg);

private:
	AprData* m_aprData;
};

#endif

