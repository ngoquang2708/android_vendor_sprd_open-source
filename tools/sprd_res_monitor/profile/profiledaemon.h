#ifndef __OPROFILE_DAEMON__H__
#define __OPROFILE_DAEMON__H__
typedef enum
{
   OPROFILE_START = 0,
   FTRACE_START = 1,
   OPROFILE_CMD_MAX
}profilecmd;

typedef struct profile_info
{
    profilecmd cmd;
    unsigned long profiletime;
}profileinfo;

#ifdef __cplusplus
extern "C"
{
#endif
    void* profile_daemon(void *);
    int start_oprofile(unsigned long time);
    int start_ftrace(unsigned long time);
#ifdef __cplusplus
}
#endif


#define PROFILE_SOCKET_PATH           "/data/local/tmp/profile/"
#define PROFILE_SOCKET_NAME           PROFILE_SOCKET_PATH "socket"
#define OPROFILE_DEBUG_SWITCHER        "debug.oprofile.value"
#define FTRACE_DEBUG_SWITCHER          "debug.ftrace.value"

#endif
