/***** BEGIN LICENSE BLOCK *****
    FlashGot - a Firefox extension for external download managers integration
    Copyright (C) 2004-2013 Giorgio Maone - g.maone@informaction.com
    
    contributors:
    Max Velasques (wxDownload Fast support)
    Zhang Ji (BitComet support)
    YuanHongYe (new Thunder support)

    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

***** END LICENSE BLOCK *****/

#include "stdafx.h"
#include "FlashGot.h"

FGCOMGuard *FGCOMGuard::instance = NULL;
int FGCOMGuard::refCount = 0;


#define BUF_SIZE 16384

char g_buf[BUF_SIZE];
wchar_t g_wbuf[BUF_SIZE];
char *g_private = NULL;

#define SHRED_PASSES 3
#define SHRED_ERR 64


// DOD 5220.22-M
bool file_overwrite(HANDLE FileHandle, ULONGLONG Length)
{
    #define CLEANBUFSIZE 65536
	static PBYTE	cleanBuffer[3];
	static BOOLEAN	buffersAllocated = false;

	DWORD		i, j, passes;
	ULONGLONG	totalWritten;
	ULONG		bytesWritten, bytesToWrite;
	LONG		seekLength;
	BOOLEAN		status;

	if( !buffersAllocated ) 
	{
		srand( (unsigned)time( NULL ) );
	
		for( i = 0; i < 3; i++ ) {

			cleanBuffer[i] = (PBYTE) VirtualAlloc( NULL, CLEANBUFSIZE, MEM_COMMIT, PAGE_READWRITE );
			if( !cleanBuffer[i] ) {

				for( j = 0; j < i; j++ ) {

					VirtualFree( cleanBuffer[j], 0, MEM_RELEASE );
				}
				return FALSE;
			}

			switch( i ) 
			{

				case 0:
				break;
				case 1:
					memset( cleanBuffer[i], 0xFF, CLEANBUFSIZE );
				break;
				case 2:
					for( j = 0; j < CLEANBUFSIZE; j++ ) cleanBuffer[i][j] = (BYTE) rand();
				break;
			}
		}	
		buffersAllocated = true;
	}

	seekLength = (LONG) Length;
	for (passes = 0; passes < SHRED_PASSES; passes++) 
	{
		if (passes != 0) 
		{
			SetFilePointer( FileHandle, -seekLength, NULL, FILE_CURRENT );
		}

		for (i = 0; i < 3; i++) 
		{
			if (i != 0) 
			{
				SetFilePointer( FileHandle, -seekLength, NULL, FILE_CURRENT );
			}

			totalWritten = 0;
			while (totalWritten < Length) 
			{

				if (Length - totalWritten > 1024*1024) 
				{
					bytesToWrite = 1024*1024;
				} 
				else 
				{
					bytesToWrite = (ULONG) (Length - totalWritten );
				}
				if (bytesToWrite > CLEANBUFSIZE) bytesToWrite = CLEANBUFSIZE;

				status = WriteFile( FileHandle, cleanBuffer[i], bytesToWrite, &bytesWritten, NULL );
				if( !status ) return FALSE;
				totalWritten += bytesWritten;
			}
		}
	}
	return true;
}

int file_shred(const char* FileName) 
{
	HANDLE	hFile;
	ULONGLONG bytesToWrite, bytesWritten;
	ULARGE_INTEGER fileLength;
	
    DWORD FileLengthHi, FileLengthLo; 

	hFile = CreateFile(FileName, GENERIC_WRITE, 
						FILE_SHARE_READ|FILE_SHARE_WRITE,
						NULL, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH, NULL);
	if( hFile == INVALID_HANDLE_VALUE ) {
		return SHRED_ERR;
	}
	
	FileLengthLo = GetFileSize(hFile, &FileLengthHi);
	
	if (FileLengthLo || FileLengthHi) 
	{
		FileLengthLo--;
		if (FileLengthLo == (DWORD) -1 && FileLengthHi) FileLengthHi--;
		
		SetFilePointer( hFile, FileLengthLo, (PLONG)&FileLengthHi, FILE_BEGIN );

		
		if (!file_overwrite(hFile, 1)) 
		{
			CloseHandle( hFile );
			return SHRED_ERR;
		}

		
		SetFilePointer (hFile, 0, NULL, FILE_BEGIN);
		fileLength.LowPart = FileLengthLo;
		fileLength.HighPart = FileLengthHi;
		bytesWritten = 0;
		while (bytesWritten < fileLength.QuadPart) 
		{

			bytesToWrite = min(fileLength.QuadPart - bytesWritten, 65536 );
			if (!file_overwrite(hFile, (DWORD) bytesToWrite)) 
			{
				CloseHandle(hFile);
				return SHRED_ERR;
			}
			bytesWritten += bytesToWrite;
		}
	}

	CloseHandle(hFile);
		
	return DeleteFile(FileName) ? 0 : SHRED_ERR;
}

extern void fail(char *msg, int code) 
{
	if (g_private) file_shred(g_private);

	MessageBox(NULL, msg,
					  "FlashGot Error",
					  MB_OK | MB_ICONERROR);
	exit(code);
}


using namespace std;





class DMSAddUrlFamily :
	public DMSupportCOM
{
protected:
	void dispatch(const DownloadInfo *downloadInfo)
	{
		CookieManager cm(downloadInfo);
		HELPER(h);
		int linksCount=downloadInfo->linksCount;
		if(linksCount>0)
		{
			if(linksCount<2) {
				LinkInfo link=downloadInfo->links[0];
				VARIANT v[3];
				v[2].vt=v[1].vt=v[0].vt=VT_BSTR;
				v[2].bstrVal=link.url;
				v[1].bstrVal=link.comment;
				v[0].bstrVal=downloadInfo->referer;
				h.invoke("AddUrl", v, 3);
			} 
			else 
			{
				FGArray fgArray(downloadInfo);
				h.invoke("AddUrlList", fgArray.asVariant(), 1);
			}
		}
	}
};




class DMSFlashGet :
	public DMSAddUrlFamily
{

protected:
	
	const char * getProgId() { return "JetCar.Netscape"; }
	
public:
	
	const char * getName() { return "FlashGet"; }

};

class DMSFlashGet2X :
	public DMSupportCOM
{

protected:
	
	const char * getProgId() { return "BHO.IFlashGetNetscapeEx"; }
	
public:
	
	const char * getName() { return "FlashGet 2.x"; }
	
	void dispatch(const DownloadInfo *downloadInfo)
	{
		CookieManager cm(downloadInfo);
		HELPER(h);
		int linksCount=downloadInfo->linksCount;
		if( linksCount >0 ) try
		{
			
			if(linksCount<2) {
				LinkInfo link = downloadInfo->links[0];
				VARIANT v[7];
				try
				{
					v[6].vt = v[5].vt = v[4].vt = v[3].vt = v[2].vt = v[1].vt = v[0].vt = VT_BSTR;
					v[6].bstrVal = link.url;
					v[5].bstrVal = link.comment.length() > 0 ? link.url : link.comment;
					v[4].bstrVal = downloadInfo->referer;
					v[3].bstrVal = SysAllocString(L"FlashGet3");
					v[2].bstrVal = link.cookie;
					v[1].bstrVal = SysAllocString(L"0");
					v[0].bstrVal = SysAllocString(L"3");
					try 
					{
						h.invoke("AddUrlEx", v, 7); // v >= 3.7
					} catch(...)
					{
						// shift left and cut at 6
						for (size_t j = 0; j < 6;) v[j].bstrVal = v[++j].bstrVal; 
						h.invoke("AddUrlEx", v, 6); // v < 3.7
					}
				} catch(...)
				{
                    v[2].bstrVal = SysAllocString(L"FlashGet"); // v < 3
					try
					{
						h.invoke("AddUrlEx", v, 6);
					} catch(...)
					{
						VARIANT v[5];
						v[4].vt = v[3].vt = v[2].vt = v[1].vt = v[0].vt = VT_BSTR;
						v[4].bstrVal = link.url;
						v[3].bstrVal = link.comment.length() > 0 ? link.url : link.comment;
						v[2].bstrVal = downloadInfo->referer;
						v[1].bstrVal = SysAllocString(L"FlashGet");
						v[0].bstrVal =  SysAllocString(L"0");
						h.invoke("AddUrlEx", v, 5);
					}
				}
			} 
			else 
			{
				FGArray fgArray(downloadInfo);
				VARIANT v[5];
				try
				{
					fgArray.asVariant(&v[4]);
					v[3].vt = v[2].vt = v[1].vt = v[0].vt = VT_BSTR;
					v[3].bstrVal = downloadInfo->referer;
					v[2].bstrVal = SysAllocString(L"FlashGet3");
					v[1].bstrVal = SysAllocString(L"");
					v[0].bstrVal = SysAllocString(L"0");
					h.invoke("AddAll", v, 5);
					
				} catch(...)
				{
					v[2].bstrVal = SysAllocString(L"FlashGet");
					v[1].bstrVal = downloadInfo->extras[1]; // document.cookie
					try
					{
						h.invoke("AddAll", v, 5);
					} catch(...)
					{
						VARIANT v[4];
						
						fgArray.asVariant(&v[3]);
						v[2].vt = v[1].vt = v[0].vt = VT_BSTR;
						v[2].bstrVal = downloadInfo->referer;
						v[1].bstrVal = SysAllocString(L"FlashGet");
						v[0].bstrVal = SysAllocString(L"0");
						
						h.invoke("AddAll", v, 4);
					}
			}
			}
		} catch(...)
		{
			LinkInfo link = downloadInfo->links[0];
			char buf[4096];
			sprintf_s(buf, 4096, "%s\n%s\n%s", (char *)link.url, (char *)link.comment, (char *)downloadInfo->referer);
			fail(buf, 1);
		}
	}
};

class DMSFlashGet2 :
	public DMSAddUrlFamily
{

protected:
	
	const char * getProgId() { return "FG2CatchUrl.Netscape"; }
	
public:
	
	const char * getName() { return "FlashGet 2"; }

	void dispatch(const DownloadInfo *downloadInfo)
	{
		try
		{
		  DMSAddUrlFamily::dispatch(downloadInfo);
		} catch(...)
		{
			DMSFlashGet2X dms;
			dms.dispatch(downloadInfo);
		}
	}
};


class DMSFreeDownloadManager :
	public DMSupportCOM
{

protected:

	const char * getProgId() { return "WG.WGUrlListReceiver"; }

public:
	
	const char * getName() { return "Free Download Manager"; }

	void dispatch(const DownloadInfo *downloadInfo )
	{
		
		int linksCount=downloadInfo->linksCount;
		if(linksCount>0)
		{
			CookieManager cm(downloadInfo);
			const char *progId;
			char *methodName;

			if(linksCount<2)
			{
				progId="WG.WGUrlReceiver";
				methodName="AddDownload";
			} else {
				progId=getProgId();
				methodName="AddURLToList";
			}

			FGCOMHelper fdm(progId);
			fdm.set("Referer",downloadInfo->referer);
			LinkInfo *links=downloadInfo->links;
			for(int j=0; j< linksCount; j++) {
				LinkInfo l=links[j];
				fdm.set("Url",l.url);
				fdm.set("Comment",l.comment);
				fdm.invoke(methodName);
			}
			if(linksCount>1) fdm.invoke("ShowAddUrlListDialog");
			
		}
	}
	
};

class DMSBitComet :
	public DMSupportCOM
{
	protected:
		const char * getProgId() { return "BitCometAgent.BcAgent.1"; }

	public:
		const char * getName() { return "BitComet"; }

		void dispatch(const DownloadInfo *downloadInfo)
		{
			HELPER(h);

			int linksCount = downloadInfo->linksCount;
			if (linksCount > 0)
			{
				if (linksCount < 2)
				{
					VARIANT v[5];

					// write title to param array
					v[0].vt = VT_NULL;
					
					// write ref URL to param array
					v[1].vt = VT_BSTR;
					v[1].bstrVal = downloadInfo->referer;

					// write URL title to param array
					v[2].vt = VT_BSTR;
					v[2].bstrVal = downloadInfo->links[0].comment;

					// write target URL to param array
					v[3].vt = VT_BSTR;
					v[3].bstrVal = downloadInfo->links[0].url;

					// write html content to param array
					v[4].vt = VT_NULL;

					h.invoke("AddLink", v, 5);
				}
				else
				{
					VARIANT v[5];

					HRESULT hResult = S_OK;

					SAFEARRAY			*pSA_URL = NULL;
					SAFEARRAY			*pSA_FLASH = NULL;
					SAFEARRAYBOUND		bound_url[1];
					SAFEARRAYBOUND		bound_flash[1];

					bound_url[0].lLbound = 0;
					bound_url[0].cElements = 2 * linksCount;

					bound_flash[0].lLbound = 0;
					bound_flash[0].cElements = 0;

					// write Flash URL link list to param array
					if ( NULL == (pSA_FLASH = SafeArrayCreate(VT_VARIANT, 1, bound_flash)) )
					{
						return;
					}

					v[0].vt = VT_ARRAY | VT_BYREF | VT_VARIANT;
					v[0].pparray = &pSA_FLASH;

					// write <URL, link title> pair list to param array
					if ( NULL == (pSA_URL = SafeArrayCreate(VT_VARIANT, 1, bound_url)) )
					{
						return;
					}
					long index[1];
					index[0] = 0;

					VARIANT  varStr;

					varStr.vt = VT_BSTR;

					for (int i=0; i < linksCount; i++)
					{
						varStr.bstrVal = downloadInfo->links[i].url;
						hResult = SafeArrayPutElement(pSA_URL, index, &varStr);
						if (hResult != S_OK)
						{
							return;
						}
						index[0]++;

						varStr.bstrVal = downloadInfo->links[i].comment;
						hResult = SafeArrayPutElement(pSA_URL, index, &varStr);
						if (hResult != S_OK)
						{
							return;
						}
						index[0]++;
					}

					v[1].vt = VT_ARRAY | VT_BYREF | VT_VARIANT;
					v[1].pparray = &pSA_URL;

					// write page title to param array
					BSTR page_title = SysAllocString(L"title");
					v[2].vt = VT_BSTR;
					v[2].bstrVal = page_title;

					// write refer url to param array
					BSTR refer_url = SysAllocString(L"unknown");
					v[3].vt = VT_BSTR;
					v[3].bstrVal = refer_url;//downloadInfo->referer;

					// write html content to param array
					BSTR html_content = SysAllocString(L"unkown");
					v[4].vt = VT_BSTR;
					v[4].bstrVal = html_content;

					// invoke AddLinkList()
					h.invoke("AddLinkList", v, 5);

					// cleanup
					SafeArrayDestroy(pSA_URL);
					SafeArrayDestroy(pSA_FLASH);
					SysFreeString(page_title);
					SysFreeString(refer_url);
					SysFreeString(html_content);
				}
			}
		}
};



class DMSFreshDownload :
	public DMSupportCOM
{


protected:
	
	const char * getProgId() { return "fdcatch.fdnscatcher"; }
	
public:
	
	const char * getName() { return "FreshDownload"; }

	void dispatch(const DownloadInfo *downloadInfo)
	{
		HELPER(h);
		CookieManager cm(downloadInfo);
		if(downloadInfo->opType==OP_ONE) 
		{
			LinkInfo l=downloadInfo->links[0];
			VARIANT v[3];
			v[2].vt=v[1].vt=VT_BSTR;
			v[2].bstrVal=l.url; // URL
			v[1].bstrVal=downloadInfo->referer; // referer
			v[0].vt=VT_INT;
			v[0].intVal=0; // quiet (0 means "show dialog")
			h.invoke("AddURL",v,3);
		} 
		else
		{
			h.invoke("AddUrlList",FGArray(downloadInfo).asVariant(),1);
		}
	}
};


class DMSGetRight :
	public DMSupport
{

private:
	static char *findGetRight(HKEY baseKey, char *leafPath, char *exeName) {
		long res;
		HKEY hk;
		char *path=NULL;
		

		if( (res=RegOpenKeyEx(baseKey,leafPath,0,KEY_QUERY_VALUE,&hk))==ERROR_SUCCESS)
		{
		
		
			char *installKey="InstallDir";
			long pathLen=0;	
			if((res=RegQueryValueEx(hk,installKey,0,NULL,NULL,(LPDWORD)&pathLen))==ERROR_SUCCESS) 
			{
				BOOL lastAttempt;
				do {
					lastAttempt=exeName==NULL;
					if(lastAttempt) exeName="getright.exe";
					size_t fullLen = pathLen + strlen(exeName) + 2;
					path = new char[fullLen];
					BOOL fileExists=FALSE;
					if((res=RegQueryValueEx(hk,installKey,0,NULL,(LPBYTE)path,(LPDWORD)&pathLen))==ERROR_SUCCESS)
					{
						struct stat statbuf;
						if(sprintf_s(path, fullLen, "%s\\%s", path, exeName) > 0)
							fileExists = !stat(path, &statbuf);  
					}
					if(!fileExists) {
						delete path;
						path = NULL;
						if(!lastAttempt) exeName=NULL;
					}
				} while(! (path || lastAttempt) );
			}
			RegCloseKey(hk);	
		}
		
		return path;
		
	}
	
	static char *findGetRight(char *exeName) 
	{
		char *path;
		if(	( path=findGetRight(HKEY_CURRENT_USER,"Software\\Headlight\\GetRight\\Config", exeName) )
			 || ( path=findGetRight(HKEY_LOCAL_MACHINE,"Software\\Headlight\\GetRight",exeName) ) )
		{
			return path;
		}
		throw "Can't find GetRight executable";
	}

	static char *createCmdLine(char *path, char *opts, char *arg) {
		size_t len = strlen(path)+strlen(opts)+strlen(arg)+6;
		char *cmdLine=new char[len];
		sprintf_s(cmdLine, len, "\"%s\" %s %s",path,opts,arg);
		return cmdLine;
	}

public:
	
	const char * getName() { return "GetRight"; }

	void check() 
	{
		delete findGetRight(NULL);
	}
	void dispatch(const DownloadInfo *downloadInfo) 
	{
		char *cmdLine=NULL;
		char *path=NULL;
		char *exeName;
		char *arg;
		char *tgargs=NULL;
		if(downloadInfo->linksCount) {
			exeName="togetright.exe";
			if( strlen(downloadInfo->extras[2]) 
				&& (strcmp("old",downloadInfo->extras[2])==0))
			{
				arg = downloadInfo->links[0].url; // CHECK ME!!!
			} else
			{
				char *pattern = "/referer=%s /cookie=%s /url=%s";
				char *cookie = downloadInfo->links[0].cookie;
				char *referer = downloadInfo->referer;
				char *url = downloadInfo->links[0].url;
				
				size_t tgargsLen = strlen(referer)+
					strlen(cookie)+
					strlen(url)+
					strlen(pattern);
				arg = tgargs=new char[tgargsLen];

				sprintf_s(tgargs, tgargsLen, pattern,	referer, cookie, url);
			}
		} else {
			exeName=NULL;
			arg=(char *)downloadInfo->referer;
		}
		/*
		// GetRight doesn't work if not already started... needs more investigation

		if(!FindWindow(NULL, "GetRight "))
		{
			path = findGetRight(NULL);
			if(createProcess(cmdLine = createCmdLine(path, "", ""), NULL))
			{
				for(int j = 100; j-- > 0;) // 10 secs for GetRight to show up
				{
					if(FindWindow(NULL, "GetRight ")) break;
					sleep(100);
					if(j == 50) createProcess(cmdLine, NULL);
				}
			}
			if(cmdLine) delete[] cmdLine;
		}
		*/
		if(exeName || !path) path = findGetRight(exeName);
		
		BOOL ret=createProcess(cmdLine=createCmdLine(path = findGetRight(exeName),
			downloadInfo->extras[0],
			arg),NULL);
		if(tgargs) delete [] tgargs;
		// supplementary post-command
		if( ret && (!exeName) && strlen(downloadInfo->extras[1])) 
		{
			if(cmdLine) delete[] cmdLine;
			Sleep(500);
			createProcess(cmdLine = createCmdLine(path,
				downloadInfo->extras[1],
				""),NULL);
		}
		if(cmdLine) delete [] cmdLine;
		if(path) delete [] path;
		if(!ret) throw "Can't launch GetRight";
	}
};

class DMSHiDownload :
	public DMSupportCOM
{


protected:
	
	const char * getProgId() { return "NetMoles.NetMoles"; }
	
public:
	
	const char * getName() { return "HiDownload"; }

	
	void dispatch(const DownloadInfo *downloadInfo)
	{
		CookieManager cm(downloadInfo);
		
		
		HELPER(h);
		
		
		if(downloadInfo->linksCount == 1 && !FindWindow("HiDownload", NULL))
		{
			VARIANT v[2];
			v[1].vt = v[0].vt = VT_BSTR;
			LinkInfo l = downloadInfo->links[0];
			v[1].bstrVal = l.url;
			v[0].bstrVal = l.comment;
			h.invoke("NMAddUrl", v, 2);
		} else
		{
			FGArray fgArray(downloadInfo);
			VARIANT v;
			v.vt = VT_BYREF | VT_VARIANT;
			v.pvarVal = fgArray.asVariant();
			h.invoke("NMAddAllUrl", &v, 1);
		}
		
	}
};

class DMSInstantGet :
	public DMSAddUrlFamily
{
protected:
	const char * getProgId() { return "InstantGet.AddUrl"; }
public:
	const char * getName() { return "InstantGet"; }
};


#import "IDManTypeInfo.tlb" 
#include "IDManTypeInfo.h"
#include "IDManTypeInfo_i.c"  

class DMSInternetDownloadManager :
	public DMSupportCOM
{


protected:
	
	const char * getProgId() { return "IDMGetAll.IDMAllLinksProcessor"; }
	
public:
	
	const char * getName() { return "Internet Download Manager"; }

	void check()
	{
		char *path = DMSupport::findProgram(HKEY_CLASSES_ROOT, 
			"CLSID\\{AC746233-E9D3-49CD-862F-068F7B7CCCA4}\\LocalServer32");
		if(!path) throw "Can't find Internet Download Manager executable";
	}

	void dispatch(const DownloadInfo *downloadInfo)
	{
		long lc = downloadInfo->linksCount;
		if(lc<1) return;

		LinkInfo *links=downloadInfo->links;
		LinkInfo *l;

		ICIDMLinkTransmitter2* pIDM;
		HRESULT hr = CoCreateInstance(CLSID_CIDMLinkTransmitter, NULL, CLSCTX_LOCAL_SERVER,
					IID_ICIDMLinkTransmitter2, (void**)&pIDM);
		if (S_OK != hr) return;

		if(lc < 2)
		{
			VARIANT reserved;
			reserved.vt=VT_EMPTY;
			l = &links[0];
			
			pIDM->SendLinkToIDM2(l->url, downloadInfo->referer, l->cookie, l->postdata,
					NULL, NULL, NULL, NULL, 0,
					reserved, reserved);
		} 
		else 
		{
			
			SAFEARRAY *pSA = NULL;
			SAFEARRAYBOUND bound[2];
			bound[0].lLbound = 0;
			bound[0].cElements = lc;
			bound[1].lLbound = 0;
			bound[1].cElements = 4;
			if ( pSA = SafeArrayCreate(VT_BSTR, 2, bound) )
			{
				long index[2];
				CComBSTR url, cookie, comment;

				for(long j = 0; j < lc; j++)
				{
					index[0] = j;
					l = &links[j];
					
					index[1] = 0;

					url = (LPCOLESTR)l->url;
					cookie = (LPCOLESTR)l->cookie;
					comment = (LPCOLESTR)l->comment;

					SafeArrayPutElement(pSA, index, url);
			
					index[1] = 1;
					SafeArrayPutElement(pSA, index, cookie);
			
					index[1] = 2;
					SafeArrayPutElement(pSA, index, comment);
			
					index[1] = 3;
					SafeArrayPutElement(pSA, index, NULL);
				}
				VARIANT array;
			    VariantInit(&array);
				array.vt = VT_ARRAY | VT_BSTR;
			    array.parray = pSA;
				pIDM->SendLinksArray(downloadInfo->referer, &array);
				SafeArrayDestroy(pSA);
			}
		}
		pIDM->Release();
	}
};

class DMSFreeDownloadManager5plus :
	public DMSSupportNativeHost
{

protected:

	const char * getHostId(){ 
		return "org.freedownloadmanager.fdm5.cnh"; 
	}

public:

	const char * getName() { 
		return "Free Download Manager 5+"; 
	}

	void dispatch(const DownloadInfo *downloadInfo){

		using namespace ggicci;

		long lc = downloadInfo->linksCount;
		if(lc<1) return;

		std::string referer = utf8::narrow(downloadInfo->referer);

		Json msg = Json::Parse("{}");
		msg.AddProperty("id", Json("4"));
		msg.AddProperty("type", Json("create_downloads"));
		Json create_downloads = Json::Parse("{}");
		Json downloads = Json::Parse("[]");
		for(int i=0; i<lc; i++){
			//todo: add post data
			//todo: add user agent
			std::string cookie = utf8::narrow(downloadInfo->links[i].cookie);
			std::string url = utf8::narrow(downloadInfo->links[i].url);
			Json dl = Json::Parse("{}");
			dl.AddProperty("url", Json(url));
			dl.AddProperty("originalUrl", Json(url));
			dl.AddProperty("httpReferer", Json(referer));
			dl.AddProperty("userAgent", Json("Mozilla/5.0 (Windows NT 6.1; rv:56.0) Gecko/20100101 Firefox/56.0"));
			dl.AddProperty("httpCookies", Json(cookie));
			//todo: what's this?
			dl.AddProperty("youtubeChannelVideosDownload", Json(0));
			downloads.Push(dl);
		}
		create_downloads.AddProperty("downloads", downloads);
		msg.AddProperty("create_downloads", create_downloads);
		
		std::string jsonStr = msg.ToString();

		const char* hs = "{\"id\":\"1\",\"type\":\"handshake\",\"handshake\":{\"api_version\":\"1\",\"browser\":\"Firefox\"}}";
		const char* ui = "{\"id\":\"2\",\"type\":\"ui_strings\"}";
		const char* set = "{\"id\":\"3\",\"type\":\"query_settings\"}";
		const char* dl = "{\"id\":\"4\",\"type\":\"create_downloads\",\"create_downloads\":{\"downloads\":[{\"url\":\"https://puria.bad.mn/dl/سلام.bin\",\"originalUrl\":\"https://puria.bad.mn/dl/سلام.bin\",\"httpReferer\":\"https://puria.bad.mn/dl/\",\"userAgent\":\"Mozilla/5.0 (Windows NT 6.1; rv:56.0) Gecko/20100101 Firefox/56.0\",\"httpCookies\":\"\",\"youtubeChannelVideosDownload\":0}]}}";
		const char* dlm = "{\"id\":\"4\",\"type\":\"create_downloads\",\"create_downloads\":{\"downloads\":[{\"url\":\"https://puria.bad.mn/dl/سلام.bin\",\"originalUrl\":\"https://puria.bad.mn/dl/سلام.bin\",\"httpReferer\":\"https://puria.bad.mn/dl/\",\"userAgent\":\"Mozilla/5.0 (Windows NT 6.1; rv:56.0) Gecko/20100101 Firefox/56.0\",\"httpCookies\":\"\",\"youtubeChannelVideosDownload\":0},{\"url\":\"https://puria.bad.mn/dl/3M.bin\",\"originalUrl\":\"https://puria.bad.mn/dl/3M.bin\",\"httpReferer\":\"https://puria.bad.mn/dl/\",\"userAgent\":\"Mozilla/5.0 (Windows NT 6.1; rv:56.0) Gecko/20100101 Firefox/56.0\",\"httpCookies\":\"\",\"youtubeChannelVideosDownload\":0},{\"url\":\"https://puria.bad.mn/dl/4M.bin\",\"originalUrl\":\"https://puria.bad.mn/dl/4M.bin\",\"httpReferer\":\"https://puria.bad.mn/dl/\",\"userAgent\":\"Mozilla/5.0 (Windows NT 6.1; rv:56.0) Gecko/20100101 Firefox/56.0\",\"httpCookies\":\"\",\"youtubeChannelVideosDownload\":0}]}}";
		const char* c = jsonStr.c_str();

		NativeHost host(getManifestPath(), "fdm_ffext2@freedownloadmanager.org");
		if(host.init()){
			//sending the init messages appears to have solved the crash issues but just for safety
			//let's add a 200ms wait
			Sleep(200);
			host.sendMessage(hs);
			host.sendMessage(ui);
			host.sendMessage(set);
			host.sendMessage(c);
		}
		host.close();

	}

};



class DMSLeechGetBase :
	public DMSupportCOM
{

public:
	
	
	void dispatch(const DownloadInfo *downloadInfo) 
	{	
		HELPER(h);
		CookieManager cm(downloadInfo);
		if(downloadInfo->linksCount>0) 
		{
			h.invoke(downloadInfo->opType==OP_ONE?"AddURL":"Wizard", downloadInfo->links[0].url);
		} 
		else 
		{
			h.invoke("Parse",downloadInfo->referer);
		}
	}
};

class DMSLeechGet2002 :
	public DMSLeechGetBase
{
	
protected:
	const char * getProgId() 
	{
		return "LeechGetIE.AddURL";
	}
public:
	const char * getName() { return "LeechGet 2002"; }
};

class DMSLeechGet :
	public DMSLeechGetBase
{
protected:
	const char * getProgId() 
	{
		return "LeechGetIE.LeechIE";
	}
public:
	const char * getName() { return "LeechGet"; }
};

class DMSMass_Downloader :
	public DMSupportCOM
{


protected:
	
	const char * getProgId() { return "MassDown.AddUrl.1"; }
	
public:
	
	const char * getName() { return "Mass Downloader"; }

	void dispatch(const DownloadInfo *downloadInfo)
	{
		
		
		int linksCount=downloadInfo->linksCount;
		if(linksCount<1) return;
		{
			VARIANT v[3];
			v[2].vt=v[1].vt=v[0].vt=VT_BSTR;
			HELPER(h);
			CookieManager cg(downloadInfo);
			DISPID di_AddUrlWithReferer;
			h.getMemberID("AddUrlWithReferer",&di_AddUrlWithReferer);
			
			if(linksCount>1) {
				v[1].bstrVal=v[0].bstrVal=bstr_t("Begin.");
				h.invoke("AddUrl",v,2);
			}
			v[0].bstrVal=downloadInfo->referer;
			LinkInfo *links=downloadInfo->links;
			for(int j=0; j<linksCount; j++) {
				LinkInfo l=links[j];
				v[2].bstrVal=l.url;
				v[1].bstrVal=l.comment;
				h.invoke(&di_AddUrlWithReferer,v,3);
			}
			if(linksCount>1) {
				v[1].bstrVal=v[0].bstrVal=bstr_t("End.");
				h.invoke("AddUrl",v,2);
			}
		}
	}
};

class DMSNetAnts :
	public DMSAddUrlFamily
{
protected:
	const char * getProgId() { return "NetAnts.API"; }
public:
	const char * getName() { return "NetAnts"; }
};


class DMSNet_Transport :
	public DMSupportCOM
{

protected:
	
	const char * getProgId() { return "NTIEHelper.NTIEAddUrl"; }

public:
	const char * getName() { return "Net Transport"; }

	void dispatch(const DownloadInfo *downloadInfo)
	{
		CookieManager cm(downloadInfo);
		
		int linksCount=downloadInfo->linksCount;
		bstr_t *parms=downloadInfo->rawParms;
		
		SAFEARRAY *psaa[2]=
		{
			FGArray::createSafeArray(linksCount),
			FGArray::createSafeArray(linksCount)
		};

		VARIANT vstr;
		vstr.vt = VT_BSTR;
		long ix[]={0};
		for(int j=1; ix[0]<linksCount; ix[0]++) 
		{
			for(int a=0; a<2; a++) 
			{
				vstr.bstrVal=parms[j++];
				SafeArrayPutElement(psaa[a], ix, &vstr);
			}
			j+=2; // skipping cookie & postData;
		}
		
		//AddList(referrer,urls, remarks);
		VARIANT v[3];
		v[2].vt=VT_BSTR;
		v[2].bstrVal=downloadInfo->referer;
		v[1].vt=v[0].vt=VT_VARIANT | VT_BYREF ;
		SafeArrayAccessData(psaa[0], (void **)&(v[1].pvarVal));
		SafeArrayAccessData(psaa[1], (void **)&(v[0].pparray));
		HELPER(h);
		h.invoke("AddList",v,3);

		SafeArrayUnlock(psaa[1]);
		SafeArrayUnlock(psaa[0]);
		SafeArrayDestroy(psaa[0]);
		SafeArrayDestroy(psaa[1]);
	}
};



class DMSNet_Transport2 :
	public DMSNet_Transport
{

protected:
	
	const char * getProgId() { return "NXIEHelper.NXIEAddURL"; }

public:
	const char * getName() { return "Net Transport 2"; }

};

class DMSWestByte :
	public DMSupportCOM
{

public:


	void dispatch(const DownloadInfo *downloadInfo)
	{
		int linksCount=downloadInfo->linksCount;
		bstr_t *parms=downloadInfo->rawParms;
		
		if(linksCount>0)
		{
			
			VARIANT v[2];
			v[0].vt = VT_BSTR ;
			v[0].bstrVal=downloadInfo->referer; // referer
			HELPER(h);
			CookieManager cm(downloadInfo);
			if(linksCount==1) 
			{
				v[1].vt = VT_BSTR;
				v[1].bstrVal=downloadInfo->links[0].url;
				
				h.invoke("AddURL",v,2);
			} 
			else 
			{ 

				FGArray fgArray(linksCount * 2);
				LinkInfo *links=downloadInfo->links;
				for (int j=0; j < linksCount; j++) {
					LinkInfo l=links[j];
					fgArray.addString(l.url);
					fgArray.addString(l.comment);
				}
				
				fgArray.asVariant(&v[1]);
				
				h.invoke("AddURLs",v,2);

			}
		}
	}
	
};



class DMSOrbit :
	public DMSupportCOM
{
protected:
	const char * getProgId() { return "Orbitmxt.Orbit"; }
	
public:
	const char * getName() { return "Orbit"; }

	void dispatch(const DownloadInfo *downloadInfo)
	{
		CookieManager cm(downloadInfo);

		HELPER(h);
		int linksCount=downloadInfo->linksCount;
		if(linksCount>0)
		{
			if(linksCount<2)
			{
				LinkInfo link=downloadInfo->links[0];
				VARIANT v[4];
				
				v[3].vt=v[2].vt=v[1].vt=v[0].vt=VT_BSTR;
				
				v[0].bstrVal=link.cookie;
				v[1].bstrVal=downloadInfo->referer;
				v[2].bstrVal=link.comment;
				v[3].bstrVal=link.url;

				h.invoke("download",v,4);
			}
			else 
			{
				VARIANT v[4];
				FGArray urlArray(linksCount);
				FGArray txtArray(linksCount);
				for(int i = 0; i < linksCount; i++) 
				{
					urlArray.addString( downloadInfo->links[i].url );
					txtArray.addString( downloadInfo->links[i].comment );
				}

				urlArray.asVariant(&v[3]);
				txtArray.asVariant(&v[2]);
				v[1].vt=v[0].vt=VT_BSTR;
				v[1].bstrVal=downloadInfo->referer;
				v[0].bstrVal=downloadInfo->links[0].cookie;
				
				h.invoke("downloadList",v, 4);
			}
		}
	}
};


class DMSwxDownloadFast :
	public DMSupport
{

private:
	
	
	static char *findProgram() 
	{
		char *path;
		char *leafPath = "Software\\wxWidgets Program\\wxDownload Fast";
		char *leafName = "ExePath";
		if(	(path = DMSupport::findProgram(HKEY_CURRENT_USER, leafPath, leafName)) ||
			(path = DMSupport::findProgram(HKEY_LOCAL_MACHINE, leafPath, leafName)) // Ma1 20061510
		) return path;
		
		throw "Can't find wxDownload Fast executable";
	}

	static char *createCmdLine(char *path, char *arg) {
		size_t len = strlen(path)+strlen(arg)+8;
		char *cmdLine=new char[len];
		sprintf_s(cmdLine, len, "\"%s\" %s ", path, arg);
		return cmdLine;
	}

public:
	
	const char * getName() { return "wxDownload Fast"; }

	void check() 
	{
		delete findProgram();
	}
	void dispatch(const DownloadInfo *downloadInfo) 
	{
		char *cmdLine=NULL;
		char *path=NULL;
		char *arg;
		char *tgargs=NULL;
		BOOL ret = false;
		if(downloadInfo->linksCount)
		{
			char *referer=downloadInfo->referer;

			if (downloadInfo->linksCount == 1)
			{
				char *pattern="--reference=%s \"%s\"";
				char *url=downloadInfo->links[0].url;
				size_t tgargsLen = strlen(referer)+
					strlen(url)+
					strlen(pattern);
				arg = tgargs = new char[tgargsLen];
				sprintf_s(tgargs, tgargsLen, pattern, referer, url);
			}
			else
			{
				char *pattern="--reference=%s --list=\"%s\"";
				FILE * fp; 
				char tmppath[MAX_PATH];
				if (GetTempPath(MAX_PATH, tmppath) == 0)
				{
					strcpy_s(tmppath, MAX_PATH, "c:");
				}
				strcat_s(tmppath, MAX_PATH, "TEMPURL.LST");
				
				if(fopen_s(&fp, tmppath, "w") != 0) 
				{ 
					throw "FlashGot can't create file TEMPURL.LST for wxDownload Fast";
				} 
				for(int j=0,count=downloadInfo->linksCount; j<count; j++)
				{
					fputs(downloadInfo->links[j].url,fp);
					fputc('\n',fp);
				}
				fclose(fp);
				size_t tgargsLen = strlen(referer)+
					strlen(tmppath)+
					strlen(pattern);
				arg=tgargs=new char[tgargsLen];

				sprintf_s(tgargs, tgargsLen, pattern,referer,tmppath);
			}
			ret=createProcess(cmdLine=createCmdLine(path=findProgram(),
				arg),NULL);
		}

		if(tgargs) delete [] tgargs;
		if(cmdLine)	delete [] cmdLine;
		if(path) delete [] path;

		if(!ret) throw "Can't launch wxDownload Fast";
	}
};


class DMSDownloadMaster :
	public DMSWestByte
{
protected:
	const char * getProgId() { return "DMIE.MoveURL"; }
public:
	const char * getName() { return "Download Master"; }
};

class DMSInternetDownloadAccelerator :
	public DMSWestByte
{
protected:
	const char * getProgId() { return "IDAIE.MoveURLIDA"; }
public:
	const char * getName() { return "Internet Download Accelerator"; }
};

class DMSReGet :
	public DMSupportCOM
{
	
	protected:
	const char * getProgId() 
	{
		return "ClickCatcher.DownloadAllFromContextMenu";
	}
public:
	const char * getName() { return "ReGet"; }
	
	void dispatch(const DownloadInfo *downloadInfo) 
	{	
		int linksCount=downloadInfo->linksCount;
		
		VARIANT v[5];
		v[0].vt=v[1].vt=v[2].vt=v[3].vt=v[4].vt=VT_BSTR;
		bstr_t referer=downloadInfo->referer; // referer
		
		FGCOMHelper *regetAll=NULL;
		DISPID di_AddDownload;
	
		if(linksCount>1) {
			regetAll=new FGCOMHelper(getProgId());
			regetAll->getMemberID("AddDownloadToList",&di_AddDownload);
		} else if(linksCount==1) {
			LinkInfo l=downloadInfo->links[0];
			if(strlen(l.postdata)) {
				try 
				{
					FGCOMHelper dl("ReGetDx.RegetDownloadApi");
					dl.set("Url",l.url);
					dl.set("Referer",referer);
					dl.set("Cookie",l.cookie);
					dl.set("Info",l.comment);
					dl.set("PostData",l.postdata);
					dl.invoke("AddDownload");
					return;
				} catch(...) {}
			}
		}
		CookieManager cm(downloadInfo);
		LinkInfo *links=downloadInfo->links;
		for(int j=0; j<linksCount; j++) 
		{	
			LinkInfo l=links[j];
			
			for(int attempts=120; attempts-->0;) 
			{ // we give ReGet 2mins to wake up
				try 
				{
					FGCOMHelper dl("ClickCatcher.DownloadFromContextMenu");
					
					dl.set("Url",l.url);
					dl.set("Referer",referer);
					dl.set("Info",l.comment);
					dl.set("Cookie",l.cookie); // -- it seems a dummy property, we use CookieManager which works
					if(regetAll) {
						VARIANT vdl;
						vdl.vt=VT_DISPATCH;
						vdl.pdispVal=dl.getIDispatch();
						regetAll->invoke(&di_AddDownload,&vdl,1);
					} else {
						dl.invoke("AddDownload");
					}

					break;
				} 
				catch(_com_error ce)
				{
					Sleep(1000);
					if(attempts==0) throw "ReGet timeout";
				}
			}
		}

		if(regetAll) try 
		{ 
			regetAll->invoke("UrlList",NULL,0);
			delete regetAll;
		} catch(...) {}
	}
};


class DMSStarDownloader :
	public DMSupportCOM
{


protected:
	
	const char * getProgId() { return "SDExt.StarDownExt"; }
	
public:
	
	const char * getName() { return "Star Downloader"; }

	void dispatch(const DownloadInfo *downloadInfo)
	{
		
		HELPER(downloader);
		DISPID di_DownloadURL;
		downloader.getMemberID("DownloadURL",&di_DownloadURL);
		
		VARIANT v[2];
		v[0].vt = v[1].vt = VT_BSTR;
		v[0].bstrVal = downloadInfo->referer; // referrer
		LinkInfo *links=downloadInfo->links;
		for (long j=0, len=downloadInfo->linksCount; j < len ; j++) { 
			v[1].bstrVal=links[j].url;
			downloader.invoke(&di_DownloadURL,v,2);
		}
	}
};

class DMSThunder :
	public DMSupportCOM
{


protected:

	const char * getProgId() { return "ThunderAgent.Agent"; }
	
public:
	
	const char * getName() { return "Thunder"; }

	void dispatch(const DownloadInfo *downloadInfo)
	{
		CookieManager cm(downloadInfo);
		HELPER(h);
		VARIANT v[8];
		v[7].vt = v[6].vt = v[5].vt = v[4].vt = v[3].vt = VT_BSTR;
		v[2].vt = v[1].vt = v[0].vt = VT_INT;

		LinkInfo *links=downloadInfo->links;
		for (long j=0, len=downloadInfo->linksCount; j<len ; j++) 
		{ 
			v[7].bstrVal=links[j].url;
			v[6].bstrVal=_bstr_t("");
			v[5].bstrVal=_bstr_t("");
			v[4].bstrVal=links[j].comment;
			v[3].bstrVal=downloadInfo->referer;
			v[2].intVal=-1;
			v[1].intVal=0;
			v[0].intVal=-1;
			h.invoke("AddTask",v,8);
		}

		h.invoke("CommitTasks");
	}
};

class DMSGigaGet : 
	public DMSupportCOM
{
	
protected:
	const char * getProgId() { return "GigaGetBho.CatchRightClick.1"; }
	
public:
	const char * getName() { return "GigaGet"; }

	void dispatch(const DownloadInfo *downloadInfo)
	{
		CookieManager cm(downloadInfo);
		HELPER(h);
		VARIANT v[2];
		v[0].vt= VT_BYREF;

		FGArray fgArray(downloadInfo);
		v[1].vt=VT_BYREF | VT_VARIANT;
		v[1].pvarVal=fgArray.asVariant();
		h.get("AddAllUrl",v,1);
	}
};

class DMSThunderOld :
	public DMSGigaGet
{


protected:

	const char * getProgId() { return "Xunleibho.CatchRightClick.1"; }
	
public:
	
	const char * getName() { return "Thunder (Old)"; }

};


class DMSTrueDownloader :
	public DMSupportCOM
{


protected:
	const char * getProgId() { return "TrueDownloaderProject.TrueDownloader"; }
	
public:
	const char * getName() { return "TrueDownloader"; }

	void dispatch(const DownloadInfo *downloadInfo)
	{
		
		HELPER(downloader);
		DISPID di_MenuURL;
		downloader.getMemberID("MenuURL",&di_MenuURL);
		
		VARIANT v[3];
		v[0].vt = v[1].vt = v[2].vt = VT_BSTR;
		v[1].bstrVal = downloadInfo->referer; // referrer
		LinkInfo *links=downloadInfo->links;
		for (long j=0, len=downloadInfo->linksCount; j < len ; j++) 
		{ 
			v[2].bstrVal=links[j].url;
			v[0].bstrVal=links[j].cookie;
			downloader.invoke(&di_MenuURL,v,3);
		}
	}
};

class DMSWellGet :
	public DMSupport
{
	
	const char * getProgId() { return "NxApi.myComponent"; }
	
public:
	
	const char * getName() { return "WellGet"; }
	
	void check() {
		dispatch(NULL);
	}

	void dispatch(const DownloadInfo *downloadInfo)
	{
		
	
		HKEY hk = NULL;
		BOOL ok = false;
		char app_path[MAX_PATH];
		if(downloadInfo && strlen((char *)(downloadInfo->extras[2]))) 
		{ // explicit path
			strcpy_s(app_path, MAX_PATH, (char *)downloadInfo->extras[2]);
		} else if(RegOpenKeyEx(HKEY_CURRENT_USER,_T("Software\\WellGet"),0,KEY_QUERY_VALUE,&hk)==ERROR_SUCCESS)
		{	
			DWORD dwDisposition = 1023;
			BYTE szpath[1024];
			DWORD type_1=REG_SZ ; 
			if(RegQueryValueEx(hk, NULL, NULL,&type_1, szpath, &dwDisposition)==ERROR_SUCCESS)
			{	
				sprintf_s(app_path, MAX_PATH, "%s\\WellGet.exe", szpath);
			}
			RegCloseKey(hk);
		}
		
		// check WellGet path
		struct stat statbuf;
		if(stat(app_path,&statbuf))  throw "Can't find WellGet executable";
		
		if(downloadInfo && downloadInfo->linksCount > 0)
		{
			CookieManager cm(downloadInfo);
			char cmdLine[32767];

			if(downloadInfo->opType==OP_ONE) 
			{
				try // try COM first, to overcome command line bug in latest (1.25 - 0118) build
				{
					HELPER(h);
	
					LinkInfo l=downloadInfo->links[0];
					VARIANT v[3];
					v[2].vt=v[1].vt=v[0].vt=VT_BSTR;
					v[2].bstrVal=l.url; // URL
					v[1].bstrVal=l.comment;
					v[0].bstrVal=downloadInfo->referer; // referer
					h.invoke("AddURL", v, 3);
					return;
				} catch(...)
				{ // fall back to command line
					LinkInfo l=downloadInfo->links[0];
					
					sprintf_s(cmdLine, sizeof(cmdLine), "%s wget -r:\"%s\" -c:\"%s\" \"%s\"",
						app_path,
						(char *)downloadInfo->referer,
						(char *)l.comment, (char *)l.url);
				} 
			}
			else 
			{
				LinkInfo *links=downloadInfo->links;
				
				FILE * fp; 
				char path[MAX_PATH];
				if (GetTempPath(MAX_PATH, path)==0)
				{
					strcpy_s(path, MAX_PATH, "c:");
				}
				strcat_s(path, MAX_PATH, "TEMPURL.TXT");

				if(fopen_s(&fp, path, "w") != 0) 
				{ 
					throw "FlasGot can't create file TEMPURL.TXT for WellGet";
				} 
				fputs(downloadInfo->referer,fp);
				fputc('\n',fp);
				for(int j=0,count=downloadInfo->linksCount; j<count; j++) {
					LinkInfo l=links[j];
					fputs(l.url,fp);
					fputc('\n',fp);
					fputs(l.comment,fp);
					fputc('\n',fp);
				}
				fclose(fp);
				sprintf_s(cmdLine, sizeof(cmdLine), "%s -u addlist", app_path);
			}

			createProcess(cmdLine, NULL);
		}

	}
		
	
	
};


// [END DOWNLOAD MANAGER SUPPORT CLASSES]

DMSFactory * DMSFactory::instance=NULL;
void DMSFactory::registerAll()
{
	add(new DMSBitComet());
	add(new DMSDownloadAcceleratorPlus());
	add(new DMSDownloadMaster());
	add(new DMSFlashGet());
	add(new DMSFlashGet2());
	add(new DMSFlashGet2X());
	add(new DMSFreeDownloadManager());
	add(new DMSFreshDownload());
	add(new DMSGetRight());
	add(new DMSGigaGet());
	add(new DMSHiDownload());
	add(new DMSInstantGet());
	add(new DMSInternetDownloadAccelerator());
	add(new DMSInternetDownloadManager());
	add(new DMSLeechGet2002());
	add(new DMSLeechGet());
	add(new DMSMass_Downloader());
	add(new DMSNetAnts());
	add(new DMSNet_Transport());
	add(new DMSNet_Transport2());
	add(new DMSOrbit());
	add(new DMSReGet());
	add(new DMSStarDownloader());
	add(new DMSTrueDownloader());
	add(new DMSThunder());
	add(new DMSThunderOld());
	add(new DMSWellGet());
	add(new DMSwxDownloadFast());
	add(new DMSFreeDownloadManager5plus());
}



DMSupport* createDMS(char *name) {
	DMSupport *res=DMSFactory::getInstance()->getDMS(name);
	if(res) return res;
	sprintf_s(g_buf, BUF_SIZE, "Unsupported Download Manager %s", name);
	fail(g_buf, -8000);
	return NULL;
}

wchar_t *UTF8toUnicode(const char *src) {
	return src && (
		MultiByteToWideChar(CP_UTF8,0,src,-1,  g_wbuf, BUF_SIZE) 
		|| MultiByteToWideChar(CP_ACP,0,src,-1,g_wbuf, BUF_SIZE)
		)
		? g_wbuf : L""
		;
}

bstr_t * readLine(FILE *stream, bstr_t *buffer) 
{
	
	bool isLine=false;
	for(char *res; res=fgets(g_buf,BUF_SIZE,stream); )
	{
		size_t lastPos=strlen(res)-1;
		while(lastPos>=0 && 
			(res[lastPos]==0x0a || res[lastPos]==0x0d)
			)
		{
			res[lastPos--]='\0';
			isLine=true;
		}
		
		buffer->Assign(*buffer + UTF8toUnicode((const char*) res));
		if(isLine) break;
	}
	return buffer;
}



int performTest(char *outfname)
{
	FILE *fp;
	if (outfname) 
	{
		freopen_s(&fp, outfname, "w", stdout);
		freopen_s(&fp, outfname,"a", stderr);
	}
	DMSFactory::getInstance()->checkAll();
	exit(0);
}


void parseHeader(DownloadInfo *downloadInfo, char *header_buf)
{
#define HEADER_COUNT 4
	char *header[HEADER_COUNT];
	char *cur=header_buf;
	for(int j=0; j<HEADER_COUNT ; ) 
	{
		if( (header[j++]=cur)  && (cur=strchr(cur,';')) ) 
		{
			cur[0]='\0';
			++cur;
		} 
		else
		{
			if(j!=HEADER_COUNT) fail("Malformed header",17);	
			break;
		}
	}

	downloadInfo->linksCount=atoi(header[0]);
	downloadInfo->dmName=header[1];
	int typeId=atoi(header[2]);
	downloadInfo->opType = (typeId<OP_MIN||typeId>OP_MAX)
		?OP_ALL:(OpType)typeId;
	downloadInfo->folder = bstr_t(UTF8toUnicode((const char *)header[3]));
}

void processJobFile(FILE *f)
{
	DMSupport *dms = NULL;
	const char *errMsg="Download manager not properly installed.\n%s";
	BOOL completed=FALSE;
	try 
	{
		DownloadInfo downloadInfo;

		char header_buf[BUF_SIZE];
		if(fgets(header_buf, BUF_SIZE, f))
		{
			parseHeader(&downloadInfo, header_buf);
			int linksCount=downloadInfo.linksCount;
			int parmsCount = 1 + linksCount * 4 + EXTRAS_COUNT; // referer + (url + info + cookie + postdata) * 4 + referer cookie + referer referer :-)

			bstr_t *parms=downloadInfo.rawParms=new bstr_t[parmsCount];
			
			LinkInfo *links=downloadInfo.links=new LinkInfo[linksCount];
			
			for(int j=0; j<parmsCount; j++) {
				parms[j]="";
				bstr_t *buffer = &parms[j];
				readLine(f, buffer);
			}
			
			downloadInfo.referer = parms[0];
			downloadInfo.links = (LinkInfo *) &parms[1];
			downloadInfo.extras = &parms[parmsCount - EXTRAS_COUNT];

			(dms=createDMS(downloadInfo.dmName))->dispatch(&downloadInfo);
			printf("%s",downloadInfo.folder);
		}
		completed=TRUE;
	} 
	catch(_com_error ce)
	{
		sprintf_s(g_buf, BUF_SIZE, errMsg,ce.ErrorMessage());
	}
	catch(char *ex) {
		sprintf_s(g_buf, BUF_SIZE, errMsg, ex);
	} 
	//catch(...) {
	//	sprintf(g_buf,errMsg,"");
	//}
	
	if(dms) delete dms;
	
	if(!completed) fail(g_buf,0x02000);
}

int performDownload(char *fname)
{
	FILE *f;
	size_t doneLen = strlen(fname) + 6;
	char *done = new char[doneLen];
	
	sprintf_s(done, doneLen, "%s.done", fname);	
	if(fopen_s(&f, fname, "rb") != 0)  
	{
		struct stat statbuf;
		if(stat(done, &statbuf)) // done file doesn't exist  
		{
			sprintf_s(g_buf, BUF_SIZE, "Can't open file %s", fname);
			fail(g_buf, 0x01000);
		} else {
			remove(done);
		}
	}
	else
	{	
		if(!feof(f))	
		{
			processJobFile(f);
		}
		else 
		{
			sprintf_s(g_buf, BUF_SIZE, "Temporary file %s is empty", fname);
			fail(g_buf,0x03000);
		}
		fclose(f);
		
		if(!strstr(done, "test"))
		{
			if (g_private)
			{
				file_shred(fname);
			} else {
				remove(done);
				rename(fname, done);
			}
		}
	}
	delete [] done;
	return 0;
}


int main(int argc, char* argv[])
{

    if(argc < 2 || strcmp(argv[1], "-o") == 0)
	{
		return performTest(argc > 2 ? argv[2] : NULL);
	}
	
	if (argc >= 2 && strcmp(argv[1], "-s") == 0)
	{
		int ret = 0;
		for(int pos = 2; pos < argc; pos++)
		  ret |= file_shred(argv[pos]);

		return ret;
	}
	
	int pos = 1;
    if (argc > 2 && strcmp(argv[1], "-p") == 0)
	{
		g_private = argv[++pos];
	}
	performDownload(argv[pos]);
}

int WINAPI
WinMain(HINSTANCE inst, HINSTANCE previnst, LPSTR cmdline, int cmdshow)
{
    return main(__argc, __argv); 
}
