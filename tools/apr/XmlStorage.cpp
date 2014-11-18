
#include "common.h"
#include "XmlStorage.h"

#define MY_ENCODING "utf-8"

XmlStorage::XmlStorage(Observable *o, char* pdir, char* pfname):Observer(o)
{
	APR_LOGD("XmlStorage::XmlStorage()\n");
	strcpy(m_dir, pdir);
	strcpy(m_pathname, pdir);
	strcat(m_pathname, "/");
	strcat(m_pathname, pfname);

	m_sTime = 0;
	m_doc = NULL;
	m_rootNode = NULL;
	m_aprNode = NULL;
	m_exceptionsNode = NULL;
	m_etNode = NULL;

	pthread_mutex_init(&m_mutex, NULL);
	this->init();
}

XmlStorage::~XmlStorage()
{
	APR_LOGD("XmlStorage::~XmlStorage()\n");
	m_dir[0] = '\0';
	m_pathname[0] = '\0';

	xmlFreeDoc(m_doc);
	pthread_mutex_destroy(&m_mutex);
}

void XmlStorage::init()
{
	APR_LOGD("XmlStorage::init()\n");
	int ret;
	char value[PROPERTY_VALUE_MAX];
	char *default_value = (char*)"unknown";

	m_sTime = getdate(value, sizeof(value), "%s");
	// file exist
	if (_fileIsExist())
	{
		xmlKeepBlanksDefault(0); // libxml2 global variable.
		xmlIndentTreeOutput = 1; // indent .with \n
		m_doc = xmlParseFile(m_pathname);//, MY_ENCODING, XML_PARSE_RECOVER);
		if (NULL == m_doc) {
			APR_LOGE("%s not load successfully.\n", m_pathname);
		}
		m_rootNode = xmlDocGetRootElement(m_doc);
		// sysdump ??
		property_get("ro.boot.mode", value, default_value);
		if (strcmp(value, default_value))
		{
			xmlNodePtr aprNode = _xmlGetContentByName(m_rootNode, NULL, NULL);
			if (aprNode != NULL)
			{
				xmlNodePtr excepNode = _xmlGetContentByName(aprNode, (char*)"exceptions", NULL);
				if (NULL == excepNode) {
					excepNode = xmlNewNode(NULL, BAD_CAST"exceptions");
					xmlAddChild(aprNode, excepNode);
				}
				char strbuf[PROPERTY_VALUE_MAX];
				_xmlGetContentByName(aprNode, (char*)"endTime", strbuf);
				// add <entry>...<entry> to m_exceptionsNode
				xmlNodePtr entryNode;
				createEntryNode(&entryNode, strbuf, value);
				xmlAddChild(excepNode, entryNode);
			}
		}

		// add <apr>...</apr> to m_rNode
		createAprNode(&m_aprNode);
		xmlAddChild(m_rootNode, m_aprNode);

		ret = xmlSaveFormatFileEnc(m_pathname, m_doc, MY_ENCODING, 1);
		if (ret != -1) {
			APR_LOGD ("%s file is created\n", m_pathname);
		} else {
			APR_LOGE ("xmlSaveFormatFile failed\n");
		}
	} else
	{
		// document pointer
		m_doc = xmlNewDoc(BAD_CAST"1.0");
		// root node pointer
		m_rootNode = xmlNewNode(NULL, BAD_CAST"aprs");
		xmlDocSetRootElement(m_doc, m_rootNode);

		// add <apr>...</apr> to m_rNode
		createAprNode(&m_aprNode);
		xmlAddChild(m_rootNode, m_aprNode);

		ret = xmlSaveFormatFileEnc(m_pathname, m_doc, MY_ENCODING, 1);
		if (ret != -1) {
			APR_LOGD("%s file is created\n", m_pathname);
		} else {
			APR_LOGE("xmlSaveFormatFile failed\n");
		}
	}
}

int XmlStorage::createAprNode(xmlNodePtr *node)
{
	char value[PROPERTY_VALUE_MAX];
	char *default_value = (char*)"unknown";
	int iter;

	// <apr>
	xmlNodePtr aprNode = xmlNewNode(NULL,BAD_CAST"apr");
	//     <hardwareVersion> </hardwareVersion>
	property_get("ro.product.hardware", value, default_value);
	xmlNewTextChild(aprNode, NULL, BAD_CAST "hardwareVersion", BAD_CAST value);
	//     <SN> </SN>
	property_get("ro.boot.serialno", value, default_value);
	xmlNewTextChild(aprNode, NULL, BAD_CAST "SN", BAD_CAST value);
	//     <buildNumber> </buildNumber>
	property_get("ro.build.description", value, default_value);
	xmlNewTextChild(aprNode, NULL, BAD_CAST "buildNumber", BAD_CAST value);
	//     <CPVersion> </CPVersion>
	for(iter=1; iter <= 10; iter++)
	{
		property_get("gsm.version.baseband", value, default_value);
		if (strcmp(value, default_value))
			break;
		sleep(5);
	}
	xmlNewTextChild(aprNode, NULL, BAD_CAST "CPVersion", BAD_CAST value);
	//     <startTime> </startTime>
	_getCurTime(value);
	xmlNewTextChild(aprNode, NULL, BAD_CAST "startTime", BAD_CAST value);
	//     <endTime> </endTime>
	//     first time, endTime = startTime
	m_etNode = xmlNewTextChild(aprNode, NULL, BAD_CAST "endTime", BAD_CAST value);
	//     <exceptions> </exceptions>
	m_exceptionsNode = NULL;
//	m_exceptionsNode = xmlNewNode(NULL, BAD_CAST"exceptions");
//	xmlAddChild(aprNode, m_eNode);
	// </apr>
	*node = aprNode;
	return 0;
}

int XmlStorage::createEntryNode(xmlNodePtr *node, char* ts, char* type)
{
	// <entry>
	xmlNodePtr entryNode = xmlNewNode(NULL, BAD_CAST "entry");
	//     <timestamp> </timestamp>
	xmlNewTextChild(entryNode, NULL, BAD_CAST "timestamp", BAD_CAST ts);
	//     <type> </type>
	xmlNewTextChild(entryNode, NULL, BAD_CAST "type", BAD_CAST type);
	// </entry>
	*node = entryNode;
	return 0;
}

void XmlStorage::handleEvent()
{

}

void XmlStorage::handleEvent(void* arg)
{
	APR_LOGD("XmlStorage::handleEvent()\n");
	int ret;
	char value[PROPERTY_VALUE_MAX];

	// lock
	pthread_mutex_lock(&m_mutex);

	if (!_fileIsExist()) {
		APR_LOGD("%s isn't exist!\n", m_pathname);
		xmlFreeDoc(m_doc);
		m_doc = NULL;
		m_rootNode = NULL;
		m_aprNode = NULL;
		m_exceptionsNode = NULL;

		// document pointer
		m_doc = xmlNewDoc(BAD_CAST"1.0");
		// root node pointer
		m_rootNode = xmlNewNode(NULL, BAD_CAST"aprs");
		xmlDocSetRootElement(m_doc, m_rootNode);

		// add <apr>...</apr> to m_rNode
		createAprNode(&m_aprNode);
		xmlAddChild(m_rootNode, m_aprNode);
	}

	//     <endTime> </endTime>
	_getCurTime(value);
	xmlNodeSetContent(m_etNode, (const xmlChar*)value);

	if (NULL == m_exceptionsNode ) {
		m_exceptionsNode = xmlNewNode(NULL, BAD_CAST"exceptions");
		xmlAddChild(m_aprNode, m_exceptionsNode);
	}
	// add <entry>...<entry> to m_exceptionsNode
	xmlNodePtr entryNode;
	_getCurTime(value);
	createEntryNode(&entryNode, value, (char*)arg);
	xmlAddChild(m_exceptionsNode, entryNode);

	ret = xmlSaveFormatFileEnc(m_pathname, m_doc, MY_ENCODING, 1);
	if (ret != -1) {
		APR_LOGD("%s file is created\n", m_pathname);
	} else {
		APR_LOGE("xmlSaveFormatFile failed\n");
	}

	// unlock
	pthread_mutex_unlock(&m_mutex);
}

void XmlStorage::UpdateEndTime()
{
	int ret;
	char value[PROPERTY_VALUE_MAX];

	// lock
	pthread_mutex_lock(&m_mutex);

	if (!_fileIsExist()) {
		APR_LOGD("%s isn't exist!\n", m_pathname);
		xmlFreeDoc(m_doc);
		m_doc = NULL;
		m_rootNode = NULL;
		m_aprNode = NULL;
		m_exceptionsNode = NULL;

		// document pointer
		m_doc = xmlNewDoc(BAD_CAST"1.0");
		// root node pointer
		m_rootNode = xmlNewNode(NULL, BAD_CAST"aprs");
		xmlDocSetRootElement(m_doc, m_rootNode);

		// add <apr>...</apr> to m_rNode
		createAprNode(&m_aprNode);
		xmlAddChild(m_rootNode, m_aprNode);
	}
	//     <endTime> </endTime>
	_getCurTime(value);
	xmlNodeSetContent(m_etNode, (const xmlChar*)value);

	ret = xmlSaveFormatFileEnc(m_pathname, m_doc, MY_ENCODING, 1);
	if (ret != -1) {
		APR_LOGD("%s file is created\n", m_pathname);
	} else {
		APR_LOGD("xmlSaveFormatFile failed\n");
	}
	// unlock
	pthread_mutex_unlock(&m_mutex);
}

int XmlStorage::_getCurTime(char* strbuf)
{
	int64_t elapsed;
	int64_t eTime;

	elapsed = elapsedRealtime(NULL);
	eTime = m_sTime + elapsed;
	sprintf(strbuf, "%lld", eTime);

	return 0;
}

int XmlStorage::_fileIsExist()
{
	// If directory is not exist, make dir
	if (access(m_dir, F_OK) < 0) {
		mkdir(m_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	}

	// file not exist
	if (access(m_pathname, F_OK) < 0) {
		return false;
	} else {
		return true;
	}
}

xmlNodePtr XmlStorage::_xmlGetContentByName(xmlNodePtr rNode, char* name, char* value)
{
	xmlChar *szKey;
	xmlNodePtr nextNode = rNode->xmlChildrenNode;
	xmlNodePtr curNode = NULL;
	while (nextNode != NULL) {
		curNode = nextNode;
		nextNode = nextNode->next;
		if (name != NULL) {
			if (!xmlStrcmp(curNode->name, (const xmlChar*)name)) {
				if (value != NULL) {
					szKey = xmlNodeGetContent(curNode);
					strcpy(value, (char*)szKey);
					xmlFree(szKey);
				}
				break;
			}
			curNode = nextNode;
		}
	}

	return curNode;
}
