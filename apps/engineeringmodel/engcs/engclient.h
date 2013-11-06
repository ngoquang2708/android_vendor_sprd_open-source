#ifndef _ENG_CLIENT_H
#define _ENG_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#define ENG_BUF_LEN 		2048

#define ENG_SIMTYPE			"persist.msms.phone_count"
#define ENG_PC_DEV			"/dev/input/event0"
#define ENG_ATAUTO_DEVT		"/dev/CHNPTYT12"
#define ENG_MODEM_DEVT		"/dev/CHNPTYT13"
#define ENG_MODEM_DEVT2		"/dev/CHNPTYT14"
#define ENG_ATAUTO_DEVW		"/dev/CHNPTYW12"
#define ENG_MODEM_DEVW		"/dev/CHNPTYW13"
#define ENG_MODEM_DEVW2		"/dev/CHNPTYW14"
#define ENG_ERR_TIMEOUT     "timeout"
#define ENG_ERR_DATERR     	"daterr"


struct eng_buf_struct {
	int buf_len;
	char buf[ENG_BUF_LEN];
};

int eng_client(char *name, int type);

#ifdef __cplusplus
}
#endif

#endif
