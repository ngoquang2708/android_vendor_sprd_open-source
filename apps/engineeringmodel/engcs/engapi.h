#ifndef _ENG_MSG_H
#define _ENG_MSG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int engapi_open(int type);
void engapi_close(int fd);
int  engapi_read(int  fd, void*  buf, size_t  len);
int  engapi_write(int  fd, const void*  buf, size_t  len);
int engapi_getphasecheck(void* buf, int size);

#ifdef __cplusplus
}
#endif

#endif
