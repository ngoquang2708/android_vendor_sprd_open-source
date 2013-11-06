#ifndef _VLOG_H
#define _VLOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

    int get_ser_fd(void);
    int restart_gser(void);

#ifdef __cplusplus
}
#endif

#endif
