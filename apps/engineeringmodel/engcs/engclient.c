#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
//#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include "stdlib.h"
#include "unistd.h"
#include <errno.h>

#include "engclient.h"
#include <cutils/sockets.h>
#include <cutils/log.h>


int eng_client(char *name, int type)
{
	int fd;
	fd = socket_local_client(name, ANDROID_SOCKET_NAMESPACE_RESERVED, type);
	if (fd < 0) {
		ALOGD("eng_client Unable to connect socket errno:%d[%s]", errno, strerror(errno));
	}

    return fd;
}
