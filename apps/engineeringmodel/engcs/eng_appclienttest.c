#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/delay.h>
#include <sys/socket.h>

#include "engopt.h"

extern int eng_appctest(void);
int main(int argc, char *argv[])
{
/*
	pid_t pid;
	
	ENG_LOG("Run Engineer Mode MODEM2SERVER Client!\n");
	
	umask(0);


	// Become a session leader to lose controlling TTY.

	if ((pid = fork()) < 0)
		ENG_LOG("%s: engclient can't fork", __FILE__);
	else if (pid != 0) 
		exit(0);
	setsid();

	if (chdir("/") < 0)
		ENG_LOG("%s: engclient can't change directory to /", __FILE__);	
*/
//	eng_appctest();

	return 0;	
}



