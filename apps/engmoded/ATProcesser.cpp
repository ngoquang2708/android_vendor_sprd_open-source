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
    }
    else if(m_cmd == "showbinfile"){
   	string binfilepath = HTTPRequest::URL::getParameter(m_url, "binfile");
	return this->showbinfile(binfilepath);
    }
    else if (m_cmd == "property_get"){
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
    //const char* atrsp = NULL;
    char atrsp[MAX_RESPONSE_LEN]={0};
    size_t ret_val = -1;
    ret_val = sendAt(atrsp,MAX_RESPONSE_LEN, sim, (char *)request);
    content.append(atrsp);   
#endif
    cout << "response.content=" << content << endl;
    return content;
}

/*shiwei added*/
bool ATProcesser::isAscii(char b) 
{
        if (b >= 0 && b <= 127) {
            return true;
        }
        return false;
}


 bool ATProcesser::checkPhaseCheck(string stream)
 {
    if((stream[0] == '9'||stream[0] == '5')
            &&stream[1] == '0'
            &&stream[2] == 'P'
            &&stream[3] == 'S'){
        return true;
    }

    return false;
}

string ATProcesser::getSn1(string stream)
{
    if (stream.empty()) {
        return "Invalid Sn1!";
    }
    if (!isAscii(stream[SN1_START_INDEX])) {
        return "Invalid Sn1!";
    }

    string sn1 = stream.substr(SN1_START_INDEX, SP09_MAX_SN_LEN);

    return sn1;
}

string ATProcesser::getSn2(string stream)
{
    if (stream.empty()) {
        return "Invalid Sn2!";
    }
    if (!isAscii(stream[SN2_START_INDEX])) {
        return "Invalid Sn2!";
    }

    string sn2 = stream.substr(SN2_START_INDEX, SP09_MAX_SN_LEN);

    return sn2;
}

bool ATProcesser::isStationTest(int station, string stream) 
{
    int flag = 1;
    if (station < 8) {
        return (0 == ((flag << station) & stream[TESTFLAG_START_INDEX]));
    } else if (station >= 8 && station < 16) {
        return (0 == ((flag << (station - 8)) & stream[TESTFLAG_START_INDEX + 1]));
    }
    return false;
}


bool ATProcesser::isStationPass(int station, string stream) 
{
    int flag = 1;
    if (station < 8) {
        return (0 == ((flag << station) & stream[RESULT_START_INDEX]));
    } else if (station >= 8 && station < 16) {
        return (0 == ((flag << (station - 8)) & stream[RESULT_START_INDEX + 1]));
    }
    return false;
}


string ATProcesser::getTestsAndResult(string stream) 
{

    string testResult ;
    string allResult;
    int flag = 1;
    if (stream.empty()) {
        return "Invalid Phase check!";
    }

    if (!isAscii(stream[STATION_START_INDEX])) {
        return "Invalid Phase check!";
    }

    for (int i = 0; i < SP09_MAX_STATION_NUM; i++) {
        if (0 == stream[STATION_START_INDEX + i * SP09_MAX_STATION_NAME_LEN]) {
            break;
        }
        testResult = stream.substr(STATION_START_INDEX + i * SP09_MAX_STATION_NAME_LEN,
                SP09_MAX_STATION_NAME_LEN);
        if (!isStationTest(i, stream)) {
            testResult += " Not test";
        } else if (isStationPass(i, stream)) {
            testResult += " Pass";
        } else {
            testResult += " Failed";
        }
        flag = flag << 1;
        allResult += testResult + "\n";
    }
    return allResult;
}

string ATProcesser::showbinfile(string binfilepath)
{
    string content;
    string result;
    FILE *fp1 = NULL;
    unsigned char buf1[300] = {0};
    int i;
    if((fp1 = fopen(binfilepath.c_str(), "rb"))==NULL) 
    {
        result.append("open file error.");
        result.append("\n");
        return result;
    }
    else
    {
        for(i=0;i<300;i++)
        {
            fread(&buf1[i], sizeof(char), 1, fp1);
            if(buf1[i] > 0xff)
            {
                buf1[i]= buf1[i]&0xff;
            }
            //printf("0x%x, ", buf1[i]);
            content += buf1[i];
        }
    }

    fclose(fp1);

    if(!checkPhaseCheck(content))
    {
            result.append("Check Phase Failed.");
            result.append("\n");
            return result;
    }

    result.append("SN1:");
    result.append("\n");
    result.append(getSn1(content));
    result.append("\n");
    result.append("SN2:");
    result.append("\n");
    result.append("\n");
    result.append(getSn2(content));
    result.append("\n");
    //cout<<getSn1(content)<<endl;
    //cout<<getSn2(content)<<endl;
    result.append(getTestsAndResult(content));
    result.append("\n");

    return result;
}

/*shiwei added over*/

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
