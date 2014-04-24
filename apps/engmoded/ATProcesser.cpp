#include "ATProcesser.h"
#include <iostream>
#include <sstream>
#include <fstream>
//#define BUILD_ENG 

#ifdef BUILD_ENG
//#include "eng_appclient_lib.h"
#include "AtChannel.h"
#endif
#include "HTTPRequest.h"

ATProcesser::ATProcesser(string url):m_url(url)
{
    m_cmd = HTTPRequest::URL::getParameter(url, "cmd");
}
ATProcesser::~ATProcesser()
{

}

string ATProcesser::response()
{
    if (m_cmd == "readfile"){
        string filepath = HTTPRequest::URL::getParameter(m_url, "file");
        return this->readfile(filepath);
    }else if (m_cmd == "property_get"){
        string key = HTTPRequest::URL::getParameter(m_url, "key");
        return this->property_get(key);
    }else if (m_cmd == "property_set"){
        string key = HTTPRequest::URL::getParameter(m_url, "key");
        string value = HTTPRequest::URL::getParameter(m_url, "value");
        return this->property_set(key, value);
    }else if (m_cmd == "shell" || m_cmd =="shellr"){
        string shellcommand = HTTPRequest::URL::getParameter(m_url, "shellcommand");
        return this->shell(shellcommand, "r");
    }else if (m_cmd == "shellw"){
        string shellcommand = HTTPRequest::URL::getParameter(m_url, "shellcommand");
        return this->shell(shellcommand, "w");
    }else if (m_cmd == ""){
        return "{\"msg\":\"please input cmd param\"}";
    }
    return process();
}

string ATProcesser::process()
{
    string content;
    #define MAX_RESPONSE_LEN 1024
    char response[MAX_RESPONSE_LEN];
    int responselen = 0;
    memset(response,0,MAX_RESPONSE_LEN);
    const char *request = m_cmd.c_str();
    int requestlen = m_cmd.length();
    string sims = HTTPRequest::URL::getParameter(m_url, "sim");
    string modems = HTTPRequest::URL::getParameter(m_url, "modem");
    int sim = 0;
    int modem = 0;
    if (!sims.empty()){
        sim = atoi(sims.c_str());
    }
    if (!modems.empty()){
        modem = atoi(modems.c_str());
    }
    cout << "cmd=" << m_cmd << ",sim=" << sim << ",modem=" << modem << endl;
#ifdef BUILD_ENG
    
    //eng_request((char *)request, requestlen, response, &responselen, sim);
    const char* atrsp = NULL;
    atrsp = sendAt(modem, sim, (char *)request);
    content.append(atrsp);   
#endif
    cout << "response.content=" << content << endl;
    return content;
}

string ATProcesser::readfile(string filepath)
{
    string content;
    char buffer[1024] = {0};
    ifstream file(filepath.c_str());
    if (! file.is_open())
    { return content; }
    while (! file.eof() ) {
        memset(buffer, 0, sizeof(buffer));
        file.getline (buffer,sizeof(buffer));
        //cout << buffer << endl;
        content += buffer;
        content += "\n";
    }
    file.close();
    return content;
}

#ifdef BUILD_ENG
#include "cutils/properties.h"
#else
string property_get(char *key, char *value, char *default_value){return "1";};
string property_set(char *key, char *value){return "1";};
#endif

string ATProcesser::property_get(string key)
{
    char value[256];
    memset(value, 0, sizeof(value));
    ::property_get((char *)key.c_str(), value, "");
    string cmdstring = string("getprop ") + key;
    //return this->shell(cmdstring,"r");
    return string(value);
}
string ATProcesser::property_set(string key, string value)
{
    ::property_set((char *)key.c_str(), (char *)value.c_str());
    string cmdstring = string("setprop ") + key + " " + value;
    //return this->shell(cmdstring,"r");
    return "1";
}

string ATProcesser::shell(string cmd, string rw)
{
    FILE *in;
    char buff[512];
    string result;

    if(!(in = popen(cmd.c_str(), rw.c_str()))){
        return result;
    }

    memset(buff, 0, sizeof(buff));
    while(fgets(buff, sizeof(buff), in)!=NULL){
        result.append(buff);
        memset(buff, 0, sizeof(buff));
    }
    pclose(in);

    return result;
}
