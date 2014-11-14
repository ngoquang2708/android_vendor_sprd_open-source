


#ifndef XMLSTORAGE_H
#define XMLSTORAGE_H

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <pthread.h>
#include "Observable.h"
#include "Observer.h"

class XmlStorage: public Observer
{
public:
	XmlStorage(Observable *o, char* pdir, char* pfname);
	~XmlStorage();
	virtual void handleEvent();
	virtual void handleEvent(void* arg);
	void UpdateEndTime();
	void init();

protected:
	xmlNodePtr _xmlGetContentByName(xmlNodePtr rNode, char* name, char* value);
	int createAprNode(xmlNodePtr *node);
	int createEntryNode(xmlNodePtr *node, char* ts, char* type);
	int _getCurTime(char* strbuf);
	int _fileIsExist();

private:
	xmlDocPtr m_doc;
	xmlNodePtr m_rootNode;	/* root Node, <aprs></aprs> */
	xmlNodePtr m_aprNode; /* <apr></apr> */
	xmlNodePtr m_exceptionsNode; /* exceptions Node, <exceptions></exceptions> */

	xmlNodePtr m_etNode;

	int64_t m_sTime;
	char m_dir[32];
	char m_pathname[48];
	pthread_mutex_t m_mutex;

};

#endif

