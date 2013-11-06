
#include "nvitem_common.h"
#include "nvitem_channel.h"
#include "nvitem_sync.h"
#include "nvitem_packet.h"
#include "nvitem_os.h"
#include "nvitem_buf.h"

static void *pSaveTask(void* ptr)
{
	do
	{
		waiteEvent();
		NVITEM_PRINT("pSaveTask up\n");
		saveToDisk();
	}while(1);
	return 0;
}

extern char* channel_path;
extern char* config_path;
BOOLEAN is_cali_mode;

int main(int argc, char *argv[])
{
#ifndef WIN32
	pthread_t pTheadHandle;
#endif
	if(4 != argc)
	{
		NVITEM_PRINT("Usage:\n");
		NVITEM_PRINT("\tnvitemd channelPath configPath is_cali_mode\n");
		return 0;
	}
	channel_path = argv[1];
	config_path = argv[2];
	NVITEM_PRINT("%s %s %s %s\n",argv[0],argv[1],argv[2],argv[3]);
	is_cali_mode = !strcmp(argv[3], "TRUE");
    NVITEM_PRINT("%s %s %s %s\n",argv[0],argv[1],argv[2],argv[3]);
    NVITEM_PRINT("is_cali_mode %d \n",is_cali_mode);
	initEvent();
	initBuf();

//---------------------------------------------------
#ifndef WIN32
// create another task
	pthread_create(&pTheadHandle, NULL, (void*)pSaveTask, NULL);
#endif
//---------------------------------------------------

	do
	{
		channel_open();
		NVITEM_PRINT("NVITEM:channel open\n");
		_initPacket();
		_syncInit();
		syncAnalyzer();
		NVITEM_PRINT("NVITEM:channel close\n");
		channel_close();
	}while(1);
	return 0;
}


