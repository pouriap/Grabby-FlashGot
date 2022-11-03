#include "stdafx.h"
#include <sys/stat.h>
#include <comdef.h>

#include <atlbase.h>
extern CComModule _Module;

#include <atlcom.h>

#include <objbase.h>
#include <wininet.h>
#include <time.h>
#include <string>

//for dlgrab
#include <fstream>
#include <stdlib.h>
#include "jsonla.h"
#include "NativeHost.h"
#include "utf8.h"
#define BUF1K 1024
#define BUF2K 2048
#define BUF4K 4096

#define STATUS_SUCC 1
#define STATUS_FAIL 2

#define ERR_MARFORMED_HEADER 17
#define ERR_JOB_FAILED 0x02000
#define ERR_UNSUPPORTED_DM -8000
#define ERR_GENERAL 1

#define DMS_POLL_DELAY 100
#define DMS_POLL_TIMEOUT 60000
enum OpType { 
		OP_ONE=0, OP_SEL=1, OP_ALL=2, 
		OP_QET=3,	
		OP_MIN=OP_ONE, OP_MAX=OP_QET };

typedef struct _LinkInfo 
{
	bstr_t url;
	bstr_t desc;
	bstr_t cookies;
	bstr_t postdata;
	bstr_t filename;
	bstr_t extension;
} LinkInfo;

#define EXTRAS_COUNT 6

typedef struct _JobInfo 
{
	std::string dmName;
	OpType optype;
	bstr_t referer;
	std::vector<LinkInfo> links;
	int dlcount;
	bstr_t dlpageCookies;
	bstr_t dlpageReferer;
	bstr_t useragent;
} JobInfo;

void fail(char *msg, int code);

class CookieManager
{
	private: 
		
		const JobInfo *jobInfo;
	public:
		CookieManager() : jobInfo(NULL) {}
		
		CookieManager(const JobInfo *jobInfo) : jobInfo(jobInfo)
		{
			//todo: this used to be extras[4]
			//since grabby doesn't specify cookie lifetime currently i changed this to one year
			int hoursToLive = 24*365;
			setAll(jobInfo, 3600 * (hoursToLive == 0 ? 1 : hoursToLive));
		}

		static void makeExpiration(char *expiration, size_t buflen, int offset)
		{
			time_t now;
			time(&now);
			now += offset;
			tm mytm;
			if(gmtime_s(&mytm, &now) == 0)
				strftime(expiration, buflen,
					"%a, %d-%b-%Y %H:%M:%S GMT", &mytm);
		}

		static void setCookie(const bstr_t *url, const bstr_t *cookie, char *expiration) {
			int cookieLen;
			if(cookie  && (cookieLen = cookie->length()))
			{
				
				char *lpszUrl = *url;
				size_t buflen = cookieLen + 16 + strlen(expiration);
				char *lpszCookie = new char[buflen];
				BOOL ok;
				for(char *nextPiece, *cookiePiece = strtok_s(*cookie, "; ", &nextPiece); 
					cookiePiece != NULL; cookiePiece = strtok_s(NULL, "; ", &nextPiece))
				{
					if (cookiePiece[0] && sprintf_s(lpszCookie, buflen, "%s; expires = %s", cookiePiece, expiration) > 0)
						ok = InternetSetCookie(lpszUrl, NULL, lpszCookie);
				}

				
				delete [] lpszCookie;
			} 
			
		}

		static void setAll(const JobInfo *jobInfo, long offsetExpiration)
		{
			char expiration[64];
			makeExpiration(expiration,64,offsetExpiration);
			for(int j=jobInfo->dlcount; j-->0;)
			{
				LinkInfo l = jobInfo->links[j];
				setCookie(&l.url,&l.cookies,expiration);
			}
		}
	
	
};

class FGArray
{
	private:
		VARIANT varStr,array;
		SAFEARRAY *psa;
		int elemCount;
		long ix[1];
		
	
		void init(int elemCount)
		{
			this->psa=createSafeArray(elemCount);
			this->elemCount=elemCount;
			ix[0]=0;
			varStr.vt=VT_BSTR;
		}

	public:

	static SAFEARRAY *createSafeArray(int elemCount) 
		{
			SAFEARRAYBOUND bounds [1]; 
			bounds[0].cElements=elemCount;
			bounds[0].lLbound=0;
			return SafeArrayCreate(VT_VARIANT, 1, bounds);
		}


		FGArray(int elemCount)
		{
			init(elemCount);
		}
		
		FGArray(const JobInfo *jobInfo)
		{		
			init(jobInfo->dlcount * 2 + 1); // linksCount * 2 (url,info)
			addString(jobInfo->referer);
			addLinks(jobInfo);
			
		}
		
		void addLinks(const JobInfo *jobInfo) {
			for (int j=0, linksCount=jobInfo->dlcount; j < linksCount ; j++) 
			{
				LinkInfo l = jobInfo->links[j];
				addString(l.url);
				addString(l.desc);
			}
		}

		BOOL addString(bstr_t s) 
		{
			if(ix[0]<elemCount) 
			{
				varStr.bstrVal=s; 
				SafeArrayPutElement(psa, ix, &varStr); 
				ix[0]++;
				return TRUE;
			}
			return FALSE;
		}

		BOOL putString(int idx, bstr_t s)
		{
			setIdx(idx);
			return addString(s);
		}
		
		void setIdx(int idx)
		{
			ix[0]=idx;
		}
		
		VARIANT *asVariant(VARIANT *array)
		{
			array->vt = VT_ARRAY | VT_VARIANT | VT_BYREF; // VBScript Array
			array->pparray=&psa;
			return array;
		}

		VARIANT *asVariant()
		{
			return asVariant(&this->array);
		}

		~FGArray()
		{
			SafeArrayDestroy(psa);
		}
	
};


class FGCOMGuard
{
private:
	static FGCOMGuard *instance;
    static int refCount;

	FGCOMGuard()
	{
		CoInitialize(NULL);
	}
	~FGCOMGuard()
	{
		CoUninitialize();
	}
public:
	
	static void addClient()
	{
		if(!instance) instance=new FGCOMGuard();
		refCount++;
	}
	
	static void removeClient()
	{
		if(--refCount<0 || !instance) throw "FGGuard Illegal state";
		if(refCount == 0)
		{
			delete instance;
		}
		
	}


};


// [START DOWNLOAD MANAGER SUPPORT CLASSES]

class DMSupport
{

protected:
	
	static char *findProgram(HKEY baseKey, char *leafPath, char *leafName = "") {
		HKEY hk;
		char *exe=NULL;
		long res = RegOpenKeyEx(baseKey,leafPath,0,KEY_QUERY_VALUE,&hk);
		//if failed try looking in 64bit registry
		if(res != ERROR_SUCCESS)
		{
			res = RegOpenKeyEx(baseKey,leafPath,0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&hk);
		}
		if(res == ERROR_SUCCESS)
		{
			char *exepath = leafName;
			long pathLen=0;	
			if((res=RegQueryValueEx(hk,exepath,0,NULL,NULL,(LPDWORD)&pathLen))==ERROR_SUCCESS) 
			{
				exe=new char[pathLen+1];
				BOOL fileExists=FALSE;
				if((res=RegQueryValueEx(hk,exepath,0,NULL,(LPBYTE)exe,(LPDWORD)&pathLen))==ERROR_SUCCESS)
				{
					struct stat statbuf;
					fileExists=!stat(exe,&statbuf);  
				}
				if(!fileExists) {
					delete exe;
					exe=NULL;
				}
			}
			RegCloseKey(hk);	
		}
		
		return exe;
		
	}

	static BOOL createProcess(char *commandLine, PROCESS_INFORMATION *pi) {
		STARTUPINFO si;
		ZeroMemory( &si, sizeof(si) );
		si.cb = sizeof(si);
		
		PROCESS_INFORMATION *mypi=NULL;
		if(!pi) {
			pi=mypi=new PROCESS_INFORMATION;
		}
		ZeroMemory(pi, sizeof(pi) );

		
		BOOL ret=CreateProcess( NULL, 
				commandLine, // Command line. 
				NULL,             // Process handle not inheritable. 
				NULL,             // Thread handle not inheritable. 
				FALSE,            // Set handle inheritance to FALSE. 
				0,                // No creation flags. 
				NULL,             // Use parent's environment block. 
				NULL,             // Use parent's starting directory. 
				&si,              // Pointer to STARTUPINFO structure.
				pi )             // Pointer to PROCESS_INFORMATION structure.
			;
		
		if(mypi) {
			closeProcess(mypi);
			delete mypi;
		}
		
		return ret;
	}

	static void closeProcess(PROCESS_INFORMATION *pi) {
		CloseHandle( pi->hProcess );
		CloseHandle( pi->hThread );
	}


public:
	
	
	virtual void check(void) = 0;
	virtual void dispatch(const JobInfo *jobInfo) = 0;
	virtual const char *getName(void) = 0;

};

#define COMCALL(Call) if(hr=FAILED(Call)) throw _com_error(hr)
#define HELPER(name) FGCOMHelper name(this->getProgId())
class FGCOMHelper
{
private:
	HRESULT hr;
	CComDispatchDriver  comObj;
	CComPtr<IDispatch>  lpTDispatch;
	
	VARIANT tmpVarStr;

	const char *className;

	
	void prepareCOMObj() {
		
		COMCALL(lpTDispatch.CoCreateInstance(_bstr_t(className),NULL));
		comObj=lpTDispatch;
	}

public:
	
	void getMemberID(char *memberName, DISPID *dispid) {
		USES_CONVERSION;
		OLECHAR FAR* oleMember=A2OLE(memberName);
		COMCALL(comObj->GetIDsOfNames(IID_NULL, &oleMember, 1, LOCALE_SYSTEM_DEFAULT, dispid));
	}

	void invoke(DISPID *dispid, VARIANT *parms, const unsigned int parmsCount) {	
		COMCALL(comObj.InvokeN(*dispid,parms,parmsCount,NULL));
	}
	
	void invoke(DISPID *dispid) {	
		COMCALL(comObj.Invoke0(*dispid,NULL));
	}

	void invoke(char *memberName, VARIANT *parms, const unsigned int parmsCount) {
		DISPID dispid;
		getMemberID(memberName,&dispid);
		invoke(&dispid,parms,parmsCount);
	}

	void invoke(char *memberName, _bstr_t parm) {
		tmpVarStr.bstrVal=parm;
		invoke(memberName,&tmpVarStr,1);
	}
	
	void invoke(char *memberName) {
		invoke(memberName,&tmpVarStr,0);
	}

	IDispatch *getIDispatch() {
		return lpTDispatch;
	}
	void set(char *propertyName, VARIANT *val) {
		USES_CONVERSION;
		LPCOLESTR oleMember=A2COLE(propertyName);
		COMCALL(comObj.PutPropertyByName(oleMember, val));
	}
	
	void set(char *propertyName, bstr_t val) {
		tmpVarStr.bstrVal=val;
		set(propertyName,&tmpVarStr);
	}

	void get(char *propertyName, VARIANT *val, UINT indexCount) {
		DISPPARAMS dispparams = {indexCount==0?NULL:&(val[1]), NULL, indexCount, 0};
		DISPID dispid;
		getMemberID(propertyName,&dispid);
		COMCALL(lpTDispatch->Invoke(dispid, IID_NULL,
				LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET,
				&dispparams, val, NULL, NULL));
	}
	
	


	FGCOMHelper(const char *className) {
	
		this->className=className;
		prepareCOMObj();
		tmpVarStr.vt=VT_BSTR;
	}

	
	~FGCOMHelper()
	{
		
	}

};


class DMSupportCOM :
	public DMSupport
{

protected:
	
	
	virtual const char * getProgId() = 0;
	
public:
	
	DMSupportCOM() {
		FGCOMGuard::addClient();
	}
	void check() 
	{
		HRESULT hr;
		CLSID clsid;
		COMCALL(CLSIDFromProgID(_bstr_t(getProgId()),&clsid));
	}
	
	

	virtual ~DMSupportCOM() {
		FGCOMGuard::removeClient();
	}
	
};

//TODO: should this be inline?
class DMSupportNativeHost :
	public DMSupport
{

protected:
	
	virtual char* getRegPath() = 0;

	std::string getManifPath_(HKEY baseKey)
	{
		std::string val = "";
		char* path = DMSupport::findProgram(baseKey, getRegPath());
		if(path){
			val = path;
		}
		delete [] path;
		return val;
	}

	std::string getManifestPath()
	{
		std::string manifestPath = "";
		manifestPath = getManifPath_(HKEY_CURRENT_USER);
		if(manifestPath.size() == 0){
			manifestPath = getManifPath_(HKEY_LOCAL_MACHINE);
		}
		return manifestPath;
	}

public:
	
	void check() 
	{
		std::string path = getManifestPath();
		if(path.length() == 0){
			std::string error = "Native client not available for: ";
			error += getRegPath();
			throw error.c_str();
		}
	}
	
};


class DMSDownloadAcceleratorPlus:
	public DMSupportCOM
{
protected:
	
	const char * getProgId() 
	{ 
		return "dapie.catcher"; 
	}

public:
	const char * getName() 
	{ 
		return "Download Accelerator Plus";  
	}
	void dispatch(const JobInfo *jobInfo);
	
};




typedef struct _DMSNode {
	unsigned int id;
	DMSupport *dms;
	_DMSNode *prev;
} DMSNode;



class DMSFactory 
{
private:
	static DMSFactory *instance;
	DMSNode *last;
	DMSupport *add(DMSupport *dms) {
		if(!last) {
			(last=new DMSNode())->prev=NULL;
			last->id=1;
		} else {
			DMSNode *newNode=new DMSNode();
			newNode->prev=last;
			newNode->id=last->id << 1;
			last=newNode;
		}
		return last->dms=dms;
	}

	DMSFactory() : last(NULL) 
	{
		this->registerAll();
	}

	 void registerAll();
public:
	
	static DMSFactory *getInstance() {
		return instance?instance:instance=new DMSFactory();		
	}
	
	
	DMSupport *getDMS(const char *name) {
		DMSNode *cursor=last;
		for(; cursor && strcmp(cursor->dms->getName(), name); cursor=cursor->prev);
		return cursor 
			?cursor->dms
			:NULL;	
	}
	
	unsigned int checkAll() {
		unsigned int retVal=0;
		FGCOMGuard::addClient();
		ggicci::Json json = ggicci::Json::Parse("[]");
		for(DMSNode *cursor=last; cursor; cursor=cursor->prev) {
			std::string dmName = "";
			ggicci::Json dm = ggicci::Json::Parse("{}");
			try {
				dmName.append(cursor->dms->getName());
				cursor->dms->check();
				dm.AddProperty("name", ggicci::Json(dmName));
				dm.AddProperty("available", ggicci::Json(true));
				json.Push(dm);
				continue;
			} catch(_com_error ce) {
				std::string msg = "BAD - COM error: ";
				msg.append(ce.ErrorMessage());
				dm.AddProperty("name", ggicci::Json(dmName));
				dm.AddProperty("available", ggicci::Json(false));
				dm.AddProperty("error", ggicci::Json(msg));
				json.Push(dm);
			} catch(...) {
				std::string msg = "BAD - unexpected unknown error!";
				dm.AddProperty("name", ggicci::Json(dmName));
				dm.AddProperty("available", ggicci::Json(false));
				dm.AddProperty("error", ggicci::Json(msg));
				json.Push(dm);
			}
			retVal |= cursor->id;
		}
		fprintf(stdout, json.ToString().c_str());
		FGCOMGuard::removeClient();
		return retVal;
	}



	~DMSFactory() {
		DMSNode *cursor=last;
		while( cursor ) {
			delete cursor->dms;
			last=cursor;
			cursor=last->prev;
			delete last;
		}
	}
};
