
#include "common.h"
#include "AprData.h"
#include "XmlStorage.h"
#include "AnrThread.h"
#include "ModemThread.h"

int main(int argc, char *argv[])
{
	AprData aprData;
	ModemThread modemThread;
	AnrThread anrThread;

	XmlStorage xmlStorage(&aprData, (char*)"/data/sprdinfo", (char*)"apr.xml");
	aprData.addObserver(&xmlStorage);

	modemThread.Start(&aprData);
	anrThread.Start(&aprData);
	while(1)
	{
		// waiting 60 seconds.
		sleep(60);
		// invoke XmlStorage::UpdateEndTime().
		APR_LOGD("xmlStorage.UpdateEndTime()\n");
		xmlStorage.UpdateEndTime();
	}

	aprData.deleteObserver(&xmlStorage);
	return 0;
}

