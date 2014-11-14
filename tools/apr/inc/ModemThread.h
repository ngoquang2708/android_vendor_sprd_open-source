


#ifndef MODEMTHREAD_H
#define MODEMTHREAD_H
#include "Thread.h"

class ModemThread : public Thread
{
protected:
	virtual void Setup();
	virtual void Execute(void* arg);
};

#endif

