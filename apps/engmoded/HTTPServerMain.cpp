/* HTTPServerMain.cpp */

#include<iostream>
#include<string>

#include<stdlib.h>

#include"HTTPServer.h"

using namespace std;
int maintest(int argc, char* argv[]);

int main(int argc, char* argv[])
{
	int port;
	HTTPServer* httpServer;

//#define fortest
#ifdef fortest
    return maintest(argc, argv);
#endif
	if(argc == 2){
		port = atoi(argv[1]);
		httpServer = new HTTPServer(port);
	}else{
		httpServer = new HTTPServer();
	}

	if(httpServer->run()){
		cerr<<"Error starting HTTPServer"<<endl;
	}

	free(httpServer);

	return 0;
}

    #include "ATProcesser.h"
int maintest(int argc, char* argv[])
{
    string content;
    content=ATProcesser("").readfile(argv[1]);
    cout << content;
    return 0;
};
