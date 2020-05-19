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
#include "jute.h"
#include "JSON.h"
#define BUF1K 1024
#define BUF2K 2048
#define BUF4K 4096


#define VERSION "1.4.1.2"

#define DMS_POLL_DELAY 100
#define DMS_POLL_TIMEOUT 60000
enum OpType { 
		OP_ONE=0, OP_SEL=1, OP_ALL=2, 
		OP_QET=3,	
		OP_MIN=OP_ONE, OP_MAX=OP_QET };

typedef struct _LinkInfo 
{
	bstr_t url;
	bstr_t comment;
	bstr_t cookie;
	bstr_t postdata;
} LinkInfo;

#define EXTRAS_COUNT 5

typedef struct _DownloadInfo 
{
	char *dmName;
	OpType opType;
	bstr_t folder;
    bstr_t *rawParms;
	bstr_t referer;
	LinkInfo *links;
	int linksCount;
	bstr_t *extras;
} DownloadInfo;

void fail(char *msg, int code);

class CookieManager
{
	private: 
		
		const DownloadInfo *downloadInfo;
	public:
		CookieManager() : downloadInfo(NULL) {}
		
		CookieManager(const DownloadInfo *downloadInfo) : downloadInfo(downloadInfo)
		{
			int hoursToLive = atoi(downloadInfo->extras[4]);
			setAll(downloadInfo, 3600 * (hoursToLive == 0 ? 1 : hoursToLive));
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

		static void setAll(const DownloadInfo *downloadInfo, long offsetExpiration)
		{
			char expiration[64];
			makeExpiration(expiration,64,offsetExpiration);
			LinkInfo *links=downloadInfo->links;
			for(int j=downloadInfo->linksCount; j-->0;)
			{
				LinkInfo l=links[j];
				setCookie(&l.url,&l.cookie,expiration);
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
		
		FGArray(const DownloadInfo *downloadInfo)
		{		
			init(downloadInfo->linksCount * 2 + 1); // linksCount * 2 (url,info)
			addString(downloadInfo->referer);
			addLinks(downloadInfo);
			
		}
		
		void addLinks(const DownloadInfo *downloadInfo) {
			LinkInfo *links=downloadInfo->links;
			for (int j=0, linksCount=downloadInfo->linksCount; j < linksCount ; j++) 
			{
				LinkInfo l=links[j];
				addString(l.url);
				addString(l.comment);
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
		long res;
		HKEY hk;
		char *exe=NULL;
		
		if( (res=RegOpenKeyEx(baseKey,leafPath,0,KEY_QUERY_VALUE,&hk))==ERROR_SUCCESS)
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
	virtual void dispatch(const DownloadInfo *downloadInfo) = 0;
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


class DMSSupportNativeHost :
	public DMSupport
{

protected:
	
	virtual const char * getHostId() = 0;

	void getManifestPath(char* manifestPath, int len){
		std::string leafPath = "";
		leafPath += "Software\\Mozilla\\NativeMessagingHosts\\";
		leafPath += getHostId();
		char* path = DMSupport::findProgram(HKEY_CURRENT_USER, (char*)leafPath.c_str());
		if(path){
			strcpy_s(manifestPath, len, path);
		}
		else{
			strcpy_s(manifestPath, len, "");
		}
		delete [] path;
	}
	
public:
	
	void check() 
	{
		char path[BUF1K];
		getManifestPath(path, BUF1K);
		if(!strlen(path)){
			std::string error = "Native client not available for: ";
			error += getHostId();
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
	void dispatch(const DownloadInfo *downloadInfo);
	
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
	
	
	DMSupport *getDMS(char *name) {
		DMSNode *cursor=last;
		for(; cursor && strcmp(cursor->dms->getName(),name); cursor=cursor->prev);
		return cursor 
			?cursor->dms
			:NULL;	
	}
	
	unsigned int checkAll() {
		unsigned int retVal=0;
		fprintf(stdout,"FlashGot Win Bridge %s\r\n",VERSION);
		FGCOMGuard::addClient();
		for(DMSNode *cursor=last; cursor; cursor=cursor->prev) {
			try {
				fprintf(stdout,"%s|",cursor->dms->getName());
				cursor->dms->check();
				fprintf(stdout,"OK\n");
				continue;
			} catch(_com_error ce) {
				fprintf(stdout,"BAD\nCOM error: %s\n", ce.ErrorMessage());
			} catch(...) {
				fprintf(stdout,"BAD\nunexpected unknown error!\n");
			}
			retVal |= cursor->id;
		}
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


//todo: add to own header file
//todo: if FDM is not running sending download message only runs it
class NativeHostConnect
{

private:

	//change all BUFSIZE references to BUF1K, BUF2K, BUF4K
	static const int BUFSIZE = 4096;

	std::string manifestPath;
	std::string hostPath;
	std::string extensionId;
	HANDLE g_hChildStd_IN_Rd;
	HANDLE g_hChildStd_IN_Wr;
	HANDLE g_hChildStd_OUT_Rd;
	HANDLE g_hChildStd_OUT_Wr;

	// Create a child process that uses the previously created pipes for STDIN and STDOUT.
	bool CreateChildProcess(const char* exeName, std::string extensionId)
	{ 
		std::string cmd = exeName;
		cmd.append(" ");
		cmd.append(manifestPath);
		cmd.append(" ");
		cmd.append(extensionId);

		char buf[BUFSIZE];
		strcpy_s(buf, BUFSIZE, cmd.c_str());
		LPSTR commandLine = buf;

		PROCESS_INFORMATION piProcInfo; 
		STARTUPINFO siStartInfo;
		BOOL bSuccess = FALSE; 

		// Set up members of the PROCESS_INFORMATION structure. 
		ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );

		// Set up members of the STARTUPINFO structure. 
		// This structure specifies the STDIN and STDOUT handles for redirection.
		ZeroMemory( &siStartInfo, sizeof(STARTUPINFO) );
		siStartInfo.cb = sizeof(STARTUPINFO); 
		siStartInfo.hStdError = g_hChildStd_OUT_Wr;
		siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
		siStartInfo.hStdInput = g_hChildStd_IN_Rd;
		siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

		// Create the child process. 
		bSuccess = CreateProcess(NULL, 
			commandLine,     // command line 
			NULL,          // process security attributes 
			NULL,          // primary thread security attributes 
			TRUE,          // handles are inherited 
			0,             // creation flags 
			NULL,          // use parent's environment 
			NULL,          // use parent's current directory 
			&siStartInfo,  // STARTUPINFO pointer 
			&piProcInfo);  // receives PROCESS_INFORMATION 

		// If an error occurs, exit the application. 
		if ( ! bSuccess ){
			return false;
		}
		else 
		{
			bool success = true;
			success &= CloseHandle(piProcInfo.hProcess) != 0;
			success &= CloseHandle(piProcInfo.hThread) != 0;
			success &= CloseHandle(g_hChildStd_OUT_Wr) != 0;
			success &= CloseHandle(g_hChildStd_IN_Rd) != 0;

			return success;
		}

	}

	// Read from a file and write its contents to the pipe for the child's STDIN.
	// Stop when there is no more data. 
	bool WriteToPipe(char* json) { 

		DWORD dwWritten; 
		BOOL bSuccess = FALSE;

		int jsonLen = strlen(json);
		int dataLen = jsonLen+4;
		char* data = new char[dataLen];
		// Native messaging protocol requires message length as a 4-byte integer prepended to the JSON string
		data[0] = char(((jsonLen>>0) & 0xFF));
		data[1] = char(((jsonLen>>8) & 0xFF));
		data[2] = char(((jsonLen>>16) & 0xFF));
		data[3] = char(((jsonLen>>24) & 0xFF));
		// Add the JSON after the length
		for(int i=0; i<jsonLen; i++){
			data[i+4] = json[i];
		}

		bSuccess = WriteFile(g_hChildStd_IN_Wr, data, dataLen, &dwWritten, NULL);
		if(!bSuccess){
			return false;
		}
		delete data;

		// Close the pipe handle so the child process stops reading. 
		return CloseHandle(g_hChildStd_IN_Wr) != 0;

	} 

	bool ReadFromPipe(void) { 

		DWORD dwRead, dwWritten; 
		DWORD dwAvail = -1;
		bool doReadPipe = false;
		CHAR chBuf[BUFSIZE]; 
		BOOL bSuccess = FALSE;
		//HANDLE hParentStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

		// wait 100ms for the client app to output something
		// because if we call ReadFile and the read succeeds and the client app doesn't output anything then 
		// we will get stuck forever because read will wait until something is output
		// because of that we use Peek instead of Read
		for(int i=0; i<10; i++){
			bSuccess = PeekNamedPipe(g_hChildStd_OUT_Rd, chBuf, BUFSIZE, &dwRead, &dwAvail, NULL);
			// If read is successful but no data is available then wait
			if(bSuccess && dwAvail==0){
				Sleep(10);
			}
			// If read is not successful (= client closed pipe) or if data is available then break out of the loop
			else{
				if(bSuccess && dwAvail>0){
					doReadPipe = true;
				}
				break;
			}
		}
		
		while (doReadPipe) 
		{ 
			bSuccess = ReadFile( g_hChildStd_OUT_Rd, chBuf, BUFSIZE, &dwRead, NULL);
			if( ! bSuccess || dwRead == 0 ) break; 
			//bSuccess = WriteFile(hParentStdOut, chBuf, dwRead, &dwWritten, NULL);
			//if (! bSuccess ) break; 
		} 

		// Close the pipe handle 
		return CloseHandle(g_hChildStd_OUT_Rd) != 0;

	} 

	void getExePathFromManifest(char* exePath, unsigned int len){

		if(hostPath.length() > 0){
			strcpy_s(exePath, len, hostPath.c_str());
			return;
		}

		std::ifstream file(manifestPath);
		std::string fileStr = "";
		std::string tmp;
		while (std::getline(file, tmp)) fileStr += tmp;
		jute::jValue v = jute::parser::parse(fileStr);

		// if json is bad
		if(v["path"].get_type() != jute::JSTRING){
			strcpy_s(exePath, len, "");
			return;
		}

		std::string path = v["path"].as_string();

		char manifestDrive[_MAX_DRIVE];
		char manifestDir[_MAX_DIR];
		_splitpath_s(manifestPath.c_str(), manifestDrive, _MAX_DRIVE, manifestDir, _MAX_DIR, NULL, 0, NULL, 0);

		char hostDrive[_MAX_DRIVE];
		_splitpath_s(path.c_str(), hostDrive, _MAX_DRIVE, NULL, 0, NULL, 0, NULL, 0);

		// if path is relative
		if(strcmp(hostDrive, "") == 0){
			hostPath = "";
			hostPath.append(manifestDrive);
			hostPath.append(manifestDir);
			hostPath.append(path);
		}
		// if path is absolute
		else{
			hostPath = path;
		}

		strcpy_s(exePath, len, hostPath.c_str());

	}


public:

	NativeHostConnect(char* manifPath, char* extId){
		g_hChildStd_IN_Rd = NULL;
		g_hChildStd_IN_Wr = NULL;
		g_hChildStd_OUT_Rd = NULL;
		g_hChildStd_OUT_Wr = NULL;
		manifestPath = manifPath;
		hostPath = "";
		extensionId = extId;
	}

	//todo: make protected
	bool sendMessage(char* msg){

		SECURITY_ATTRIBUTES saAttr; 

		// Set the bInheritHandle flag so pipe handles are inherited. 
		saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
		saAttr.bInheritHandle = TRUE; 
		saAttr.lpSecurityDescriptor = NULL; 

		// Create a pipe for the child process's STDOUT. 
		if ( ! CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0) ){
			return false;
		}

		// Ensure the read handle to the pipe for STDOUT is not inherited.
		if ( ! SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0) ){
			return false;
		}

		// Create a pipe for the child process's STDIN. 
		if (! CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0) ){
			return false;
		}

		// Ensure the write handle to the pipe for STDIN is not inherited. 
		if ( ! SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0) ){
			return false;
		}

		// Create the child process. 
		char exePath[BUFSIZE];
		getExePathFromManifest(exePath, BUFSIZE);
		if(!CreateChildProcess(exePath, extensionId)){
			return false;
		}

		// Write to the pipe that is the standard input for a child process. 
		// Data is written to the pipe's buffers, so it is not necessary to wait
		// until the child process is running before writing data.
		if(!WriteToPipe(msg)){
			return false;
		}

		if(!ReadFromPipe()){
			return false;
		}

		return true;

	}

};
