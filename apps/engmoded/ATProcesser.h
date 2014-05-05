#ifndef __ATPROCESSER_H__
#define __ATPROCESSER_H__
#include<iostream>
#include<fstream>
#include<string>
#include<cstring>
#include<stdlib.h>

using namespace std;
class ATProcesser
{
public:
    ATProcesser(string url);
    virtual ~ATProcesser();

    string response();

private:
    string m_cmd;
    string m_url;
    string process();
    /*shiwei add*/
    static const int MAX_SN_LEN = 24;
    static const int SP09_MAX_SN_LEN = MAX_SN_LEN;
    static const int SP09_MAX_STATION_NUM = 15;
    static const int SP09_MAX_STATION_NAME_LEN = 10;
    static const int SP09_SPPH_MAGIC_NUMBER = 0x53503039;
    static const int SP05_SPPH_MAGIC_NUMBER = 0x53503035;
    static const int SP09_MAX_LAST_DESCRIPTION_LEN = 32;

    static const int SN1_START_INDEX = 4;
    static const int SN2_START_INDEX = SN1_START_INDEX + SP09_MAX_SN_LEN;

    static const int STATION_START_INDEX = 56;
    static const int TESTFLAG_START_INDEX = 252;
    static const int RESULT_START_INDEX = 254;
    bool isAscii(char b);
    bool checkPhaseCheck(string stream);
    string getSn1(string stream);
    string getSn2(string stream);
    bool isStationTest(int station, string stream);
    bool isStationPass(int station, string stream);
    string getTestsAndResult(string stream);

public:
    string readfile(string filepath); 
    string property_get(string key);
    string property_set(string key, string value);
    string shell(string cmd, string rw);
    /*shiwei add*/
    string showbinfile(string binfilepath);
};


#endif
