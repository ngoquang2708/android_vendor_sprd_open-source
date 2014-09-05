#include "engopt.h"
#include "eng_debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include "private/android_filesystem_config.h"

int eng_file_lock(void)
{
    int fd;
    if ((fd = open("/data/file_lock.test", O_CREAT|O_RDWR, 0666)) != -1) {
        ENG_LOG("open data/file_lock.test fd = %d\n",fd);
        if (flock(fd,LOCK_EX | LOCK_NB) != -1) {
            ENG_LOG("lock /data/file_lock.test \n");
        }
        return fd;
     }
      return -1;
}

int eng_file_unlock(int fd)
{
     //flock(fd,LOCK_UN);
     ENG_LOG("unlock /data/file_lock.test \n");
     if(fd >= 0)
        close(fd);

     return 0;
}

