/* HTTPServer.cpp */

/* An HTTP Server written in C++ */

#include<iostream>
#include<fstream>
#include<string>
#include<cstring>
#include<stdexcept>
#include<sstream>
#include<algorithm>
#include<ctime>

#include<stdlib.h>
#include<errno.h>
#include<fcntl.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include <signal.h>   
#include <unistd.h>   
#include <sys/wait.h>

#include"HTTPServer.h"

#include "ATProcesser.h"

//extern int errno;
#define FORK_PROCESS 1

using namespace std;

HTTPServer::HTTPServer(): svrPort(1380)
{
}

HTTPServer::HTTPServer(int port)
{
	string funcName = "HTTPServer::HTTPServer: ";

	if(setPort(port)){
		cerr<<funcName<<"Failed to set port"<<endl;
	}
}

HTTPServer::~HTTPServer()
{
	close(newsockfd);
	close(sockfd);
}

int HTTPServer::setPort(size_t port)
{
	string funcName = "setPort: ";
	//Validation
	if(port<1024||port>65535){
		cerr<<funcName<<"Invalid port value. Cannot bind. Enter a value between 1024 and 65535"<<endl;
		return -1;
	}

	svrPort = port;
	
	return 0;
}

int HTTPServer::initSocket()
{
	string funcName = "initSocket: ";

	if((sockfd = socket(AF_INET, SOCK_STREAM, 0))<0){
		cerr<<funcName<<"Failed to create socket"<<endl;
		return -1;
	}

	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(svrPort);
	servAddr.sin_addr.s_addr = INADDR_ANY;

	/* Bind */
	if((bind(sockfd, (struct sockaddr *)&servAddr, sizeof(servAddr)))<0){
		cerr<<funcName<<"Failed to bind to port "<<svrPort<<endl;
		return -1;
	}

	/* Set to listen on control socket */
	if(listen(sockfd, 30000)){
		cerr<<funcName<<"Listen on port "<<svrPort<<" failed"<<endl;
		return -1;
	}

	return 0;
}

void handler(int num) {   
    int status;   
    int pid = waitpid(-1, &status, WNOHANG);   
    if (WIFEXITED(status)) {   
        printf("The child %d exit with code %d\n", pid, WEXITSTATUS(status));   
    }   
} 

int HTTPServer::run()
{
	string funcName = "run: ";
	signal(SIGCHLD, handler);
	if(initSocket()){
		cerr<<funcName<<"Failed to initialize socket"<<endl;
		return -1;
	}

	while(1){
		cliLen = sizeof(cliAddr);

		if((newsockfd = accept(sockfd, (struct sockaddr *)&cliAddr, &cliLen))<0){
			cerr<<funcName<<"Accept call failed"<<endl;
			LOG("engmoded: error: Accept call failed");
			return -1;
		}
#ifdef FORK_PROCESS
		if(fork() == 0){
			if(handleRequest()){
				cerr<<funcName<<"Failed handling request"<<endl;
				LOG("engmoded: error: Failed handling request");
				exit(-1);
			}

			exit(0);
			close(newsockfd);
		}else{
			close(newsockfd);
		}
#else
        if(handleRequest()){
            cerr<<funcName<<"Failed handling request"<<endl;
		LOG("engmoded: error: Failed handling request");
        }
#endif
		//close(newsockfd);
	}

	return 0;
}

int HTTPServer::handleRequest()
{
	string funcName = "handleRequest: ";

	m_httpRequest = new HTTPRequest();
	m_httpResponse = new HTTPResponse();
	 
	if(recvRequest()){
		cerr<<funcName<<"Receiving request failed"<<endl;
		LOG("engmoded: error: Receiving request failed");
		return -1;
	}

	m_httpRequest->printRequest();

	if(parseRequest()){
		cerr<<funcName<<"Parsing HTTP Request failed"<<endl;
		LOG("engmoded: error: Parsing HTTP Request failed");
		return -1;
	}

	if(processRequest()){
		cerr<<funcName<<"Processing HTTP Request failed"<<endl;
		LOG("engmoded: Processing HTTP Request failed");
		return -1;
	}

	if(prepareResponse()){
		cerr<<funcName<<"Preparing reply failed"<<endl;
		return -1;
	}

	m_httpResponse->printResponse();

	if(sendResponse()){
		cerr<<funcName<<"Sending reply failed"<<endl;
		LOG("engmoded: error: Sending reply failed");
		return -1;
	}
	
	delete m_httpRequest;
    m_httpRequest = NULL;
	delete m_httpResponse;
    m_httpResponse = NULL;
	return 0;
}

int HTTPServer::recvRequest()
{
	string funcName = "recvRequest: ";
	int recvLength;
	char* buf = new char[buf_sz];
	memset(buf, '\0', buf_sz);

	if(!(recvLength = recv(newsockfd, buf, buf_sz, 0))){
		cerr<<funcName<<"Failed to receive request (blocking)"<<endl;
		return -1;
	}
	m_httpRequest->addData(buf, recvLength);

	while(1){
		memset(buf, '\0', buf_sz);
		recvLength = recv(newsockfd, buf, buf_sz, MSG_DONTWAIT);

		if(recvLength < 0){
			if(errno == EWOULDBLOCK || errno == EAGAIN){
				break;
			} else {
				cerr<<funcName<<"Failed receiving request (nonblocking)"<<endl;
				return -1;
			}
		}
	
		m_httpRequest->addData(buf, recvLength);

		if(recvLength<buf_sz)
			break;
	}

	return 0;
}

int HTTPServer::parseRequest()
{
	string funcName = "parseRequest: ";

	if(m_httpRequest->parseRequest()){
		cerr<<funcName<<"Failed parsing request"<<endl;
		return -1;
	}

	return 0;
}

int HTTPServer::processRequest()
{
	string funcName = "processRequest: ";
	Method method = m_httpRequest->getMethod();

	ifstream ifs, errfs;
	ofstream ofs;
	size_t contentLength;
	ostringstream os;

	if(m_httpRequest->getProtocol() == HTTP_UNSUPPORTED){
		m_httpResponse->setStatusCode(505);
		return 0;
	}
    
    if (m_httpRequest->getURL().find("/eng")>=0){
        return processEngRequest();
    }else{
        return processEngRequest();
    }
	switch(method){
		case GET:
			m_url = SVR_ROOT + m_httpRequest->getURL();
			m_mimeType = getMimeType(m_url);

			ifs.open(m_url.c_str(), ifstream::in);
			if(ifs.is_open()){
				ifs.seekg(0, ifstream::end);
				contentLength = ifs.tellg();
				ifs.seekg(0, ifstream::beg);
				os<<contentLength;

				if(m_httpResponse->copyFromFile(ifs, contentLength)){
					cerr<<funcName<<"Failed to copy file to Response Body"<<endl;
					m_httpResponse->setStatusCode(500);
					return 0;
				}

				m_httpResponse->setHTTPHeader("Content-Length", os.str());
			}else{
				ifs.close();

				string file404 = SVR_ROOT;
				file404 += "/404.html";
				errfs.open(file404.c_str(), ifstream::in);
				if(errfs.is_open()){
					errfs.seekg(0, ifstream::end);
					contentLength = errfs.tellg();
					errfs.seekg(0, ifstream::beg);
					os<<contentLength;

					if(m_httpResponse->copyFromFile(errfs, contentLength)){
						cerr<<funcName<<"Failed to copy file to Response Body"<<endl;
						m_httpResponse->setStatusCode(500);
						return 0;
					}

					m_httpResponse->setHTTPHeader("Content-Length", os.str());
					m_httpResponse->setStatusCode(404);
					return 0;
				}else{
					cerr<<"Critical error. Shutting down"<<endl;
					return -1;
				}
			}

			ifs.close();
			m_httpResponse->setStatusCode(200);
			break;
		case PUT:
			m_url = SVR_ROOT + m_httpRequest->getURL();
			m_mimeType = getMimeType(m_url);

			ofs.open(m_url.c_str(), ofstream::out|ofstream::trunc);

			if(ofs.is_open()){
				if(m_httpRequest->copyToFile(ofs))
					m_httpResponse->setStatusCode(411);
				else
					m_httpResponse->setStatusCode(201);
			}
			else{
				m_httpResponse->setStatusCode(403);
			}
			ofs.close();

			break;
		default:
			m_httpResponse->setStatusCode(501);
			break;
	}

	return 0;
}

int HTTPServer::processEngRequest()
{
    string funcName = "processEngRequest: ";
    Method method = m_httpRequest->getMethod();
    string url = m_httpRequest->getURL();

    ifstream ifs, errfs;
    ofstream ofs;
    size_t contentLength;
    ostringstream os;
    string content="{cmd:response,data:2222}";

    if(m_httpRequest->getProtocol() == HTTP_UNSUPPORTED){
        m_httpResponse->setStatusCode(505);
        return 0;
    }
    m_mimeType = getMimeType(url);
    string cmd = HTTPRequest::URL::getParameter(url, "cmd");
    cout << "engrequest url " << url << ", param="<<cmd<< endl;
    LOG("engmoded: info engrequest(url=%s)", url.c_str());    

    content=ATProcesser(url).response();
    LOG("engmoded: info: response:content=%s",content.c_str());
    os<<content.length();
    m_httpResponse->setResponseBody(&content);
    m_httpResponse->setHTTPHeader("Content-Length", os.str());
    m_httpResponse->setHTTPHeader("Access-Control-Allow-Origin", "*");
    m_httpResponse->setHTTPHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    m_httpResponse->setHTTPHeader("Access-Control-Allow-Headers", "Origin, No-Cache, X-Requested-With, If-Modified-Since, Pragma, Last-Modified, Cache-Control, Expires, Content-Type, X-E4M-With");
    m_httpResponse->setStatusCode(200);
    return 0;
}

int HTTPServer::prepareResponse()
{
	string funcName = "prepareResponse: ";
	time_t curTime;
	time(&curTime);
	string curTimeStr = ctime(&curTime);
	replace(curTimeStr.begin(), curTimeStr.end(), '\n', '\0');

	m_httpResponse->setProtocol(m_httpRequest->getProtocol());
	m_httpResponse->setReasonPhrase();

	m_httpResponse->setHTTPHeader("Date", curTimeStr);
	m_httpResponse->setHTTPHeader("Server", "Awesome HTTP Server");
	m_httpResponse->setHTTPHeader("Accept-Ranges", "bytes");
	m_httpResponse->setHTTPHeader("Content-Type", m_mimeType);
	m_httpResponse->setHTTPHeader("Connection", "close");

	if(m_httpResponse->prepareResponse()){
		cerr<<funcName<<"Failed to prepare response"<<endl;
		return -1;
	}

	return 0;
}

int HTTPServer::sendResponse()
{
	string funcName = "sendResponse: ";

	size_t responseSize = m_httpResponse->getResponseSize();
	string* responseDataPtr = m_httpResponse->getResponseDataPtr();

	char *buf = new char[responseSize];
	memset(buf, '\0', responseSize);
	memcpy(buf, responseDataPtr->data(), responseSize);

	if((send(newsockfd, buf, responseSize, 0))<0){
		cerr<<funcName<<"Sending response failed"<<endl;
	}

	delete buf;

	return 0;
}

string HTTPServer::getMimeType(string fileName)
{
	size_t extPos = fileName.find_last_of(".");
	string extension;
	string mimeType = "text/plain, charset=us-ascii";

	if(extPos == string::npos){
		extension = "";
	}else{
		extension = fileName.substr(extPos+1);
	}

	/* Compare and return mimetype */
	switch(extension[0]){
		case 'b':
			if(extension == "bmp")
				mimeType = "image/bmp";
			if(extension == "bin")
				mimeType = "application/octet-stream";

			break;
		case 'c':
			if(extension == "csh")
				mimeType = "application/csh";
			if(extension == "css")
				mimeType = "text/css";

			break;
		case 'd':
			if(extension == "doc")
				mimeType = "application/msword";
			if(extension == "dtd")
				mimeType = "application/xml-dtd";
			break;
		case 'e':
			if(extension == "exe")
				mimeType = "application/octet-stream";
			break;
		case 'h':
			if(extension == "html" || extension == "htm")
				mimeType = "text/html";
			break;
		case 'i':
			if(extension == "ico")
				mimeType = "image/x-icon";
			break;
		case 'g':
			if(extension == "gif")
				mimeType = "image/gif";
			break;
		case 'j':
			if(extension == "jpeg" || extension == "jpg")
				mimeType = "image/jpeg";
			break;
		case 'l':
			if(extension == "latex")
				mimeType = "application/x-latex";
			break;
		case 'p':
			if(extension == "png")
				mimeType = "image/png";
			if(extension == "pgm")
				mimeType = "image/x-portable-graymap";
			break;	
		case 'r':
			if(extension == "rtf")
				mimeType  = "text/rtf";
			break;
		case 's':
			if(extension == "svg")
				mimeType = "image/svg+xml";
			if(extension == "sh")
				mimeType = "application/x-sh";
			break;
		case 't':
			if(extension == "tar")
				mimeType = "application/x-tar";
			if(extension == "tex")
				mimeType = "application/x-tex";
			if(extension == "tif" || extension == "tiff")
				mimeType = "image/tiff";
			if(extension == "txt")
				mimeType = "text/plain";
			break;
		case 'x':
			if(extension == "xml")
				mimeType = "application/xml";
			break;
		default:
            mimeType = "application/json";
			break;
	}

	return mimeType;
}

void HTTPLog(char *format, ...)
{
	
}
