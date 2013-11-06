#ifndef _ENG_APPCLIENT_H
#define _ENG_APPCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

int eng_at_open(char *name,int type);
int eng_at_read(int fd, char *buf, int buflen);
int eng_at_write(int fd, char *buf, int buflen);

#ifdef __cplusplus
}
#endif

#endif
