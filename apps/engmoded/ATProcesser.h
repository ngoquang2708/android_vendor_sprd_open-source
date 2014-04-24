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

public:
    string readfile(string filepath); 
    string property_get(string key);
    string property_set(string key, string value);
    string shell(string cmd, string rw);
};


#endif
