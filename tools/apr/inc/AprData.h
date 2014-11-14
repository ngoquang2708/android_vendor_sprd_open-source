



#ifndef APRDATA_H
#define APRDATA_H

#include "Observable.h"
#include "Thread.h"

class AprData:public Observable
{
public:
	AprData();
	~AprData();
	virtual void getSubjectInfo();

protected:
	void init();

};

#endif
