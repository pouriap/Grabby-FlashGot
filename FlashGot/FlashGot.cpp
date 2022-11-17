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
#include "base64.hpp"
#include <sstream>

using namespace std;
using namespace ggicci;
using namespace base64;

FGCOMGuard *FGCOMGuard::instance = NULL;
int FGCOMGuard::refCount = 0;


#define BUF_SIZE 16384

char g_buf[BUF_SIZE];
wchar_t g_wbuf[BUF_SIZE];


extern void fail(char *msg, int code) 
{
	printf(msg);
	ExitProcess(code);
}

class DMSAddUrlFamily :
	public DMSupportCOM
{
protected:
	void dispatch(const JobInfo *jobInfo)
	{
		CookieManager cm(jobInfo);
		HELPER(h);
		int linksCount=jobInfo->dlcount;
		if(linksCount>0)
		{
			if(linksCount<2) {
				LinkInfo link=jobInfo->links[0];
				VARIANT v[3];
				v[2].vt=v[1].vt=v[0].vt=VT_BSTR;
				v[2].bstrVal=link.url;
				v[1].bstrVal=link.desc;
				v[0].bstrVal=jobInfo->referer;
				h.invoke("AddUrl", v, 3);
			} 
			else 
			{
				FGArray fgArray(jobInfo);
				h.invoke("AddUrlList", fgArray.asVariant(), 1);
			}
		}
	}
};


class DMSEagleGet : 
	public DMSupportNativeHost
{
protected:

	char * getRegPath(){ 
		return "Software\\Mozilla\\NativeMessagingHosts\\eagleget"; 
	}

public:

	const char * getName() { 
		return "EagleGet"; 
	}

	void dispatch(const JobInfo *jobInfo)
	{
		int lc = jobInfo->dlcount;
		std::string referer = utf8::narrow(jobInfo->referer);
		std::string refCookies = utf8::narrow(jobInfo->dlpageCookies);
		std::string useragent = utf8::narrow(jobInfo->useragent);

		Json params = Json::Parse("{}");

		params.AddProperty("agent", Json(useragent));
		params.AddProperty("contentLength", Json(""));
		params.AddProperty("contentType", Json(""));
		params.AddProperty("disposition", Json(""));
		params.AddProperty("reffer", Json(referer));

		if(lc == 1)
		{
			LinkInfo link = jobInfo->links[0];
			params.AddProperty("cookie", Json(utf8::narrow(link.cookies)));
			params.AddProperty("fileName", Json(utf8::narrow(link.filename)));
			params.AddProperty("postData", Json(utf8::narrow(link.postdata)));
			params.AddProperty("url", Json(utf8::narrow(link.url)));
		}
		else if(lc > 1)
		{
			params.AddProperty("cookie", Json(refCookies));
			params.AddProperty("fileName", Json(""));
			params.AddProperty("postData", Json(""));

			std::string urls = "\\\\F ";
			for(int i=0; i<lc; i++)
			{
				urls.append(utf8::narrow(jobInfo->links[i].url));
				urls.append("|");
				urls.append(utf8::narrow(jobInfo->links[i].filename));
				urls.append("\\n");
			}
			urls.append("|");
			urls.append(referer);
			urls.append("|");
			params.AddProperty("url", Json(urls));
		}

		Json jsonMsg = Json::Parse("{}");
		jsonMsg.AddProperty("name", Json("download"));
		jsonMsg.AddProperty("paramters", params);

		NativeHost host(getManifestPath(), "eagleget_ffext@eagleget.com");

		if(host.init())
		{
			const char* exit = "{\"name\":\"exit\",\"paramters\":[]}";
			host.sendMessage(jsonMsg.ToString().c_str(), 100);
			host.sendMessage(exit, 100);
		}

		host.close();

	}
};


class DMSFlareGet : 
	public DMSupportNativeHost
{
protected:

	char * getRegPath(){ 
		return "SOFTWARE\\Mozilla\\NativeMessagingHosts\\com.flareget.flareget"; 
	}

public:

	const char * getName() { 
		return "FlareGet"; 
	}

	void dispatch(const JobInfo *jobInfo){


		long lc = jobInfo->dlcount;

		//doesn't not support more than one link
		if(lc>1)
		{ 
			return; 
		}

		std::string referer = utf8::narrow(jobInfo->referer);
		std::string refCookies = utf8::narrow(jobInfo->dlpageCookies);
		std::string useragent = utf8::narrow(jobInfo->useragent);

		Json *dlMessages = new Json[lc];

		for(int i=0; i<lc; i++){
			Json jsonMsg = Json::Parse("{}");
			jsonMsg.AddProperty("url", Json(utf8::narrow(jobInfo->links[i].url)));
			jsonMsg.AddProperty("cookies", Json(utf8::narrow(jobInfo->links[i].cookies)));
			jsonMsg.AddProperty("useragent", Json(useragent));
			jsonMsg.AddProperty("filename", Json(utf8::narrow(jobInfo->links[i].filename)));
			jsonMsg.AddProperty("filesize", Json(""));
			jsonMsg.AddProperty("referrer", Json(referer));
			jsonMsg.AddProperty("postdata", Json(utf8::narrow(jobInfo->links[i].postdata)));
			jsonMsg.AddProperty("vid", Json(""));
			jsonMsg.AddProperty("status", Json("OK"));
			dlMessages[i] = jsonMsg;
		}

		NativeHost host(getManifestPath(), "support@flareget.com");

		for(int i=0; i<lc; i++)
		{
			if(host.init()){
				std::string msg = dlMessages[i].ToStringOrderedTrimmed();
				host.sendMessage(msg.c_str());
			}
			host.close();
		}

		delete [] dlMessages;

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
	
	void dispatch(const JobInfo *jobInfo)
	{
		CookieManager cm(jobInfo);
		HELPER(h);
		int linksCount=jobInfo->dlcount;
		if( linksCount >0 ) try
		{
			
			if(linksCount<2) {
				LinkInfo link = jobInfo->links[0];
				VARIANT v[7];
				try
				{
					v[6].vt = v[5].vt = v[4].vt = v[3].vt = v[2].vt = v[1].vt = v[0].vt = VT_BSTR;
					v[6].bstrVal = link.url;
					v[5].bstrVal = link.desc.length() > 0 ? link.url : link.desc;
					v[4].bstrVal = jobInfo->referer;
					v[3].bstrVal = SysAllocString(L"FlashGet3");
					v[2].bstrVal = link.cookies;
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
						v[3].bstrVal = link.desc.length() > 0 ? link.url : link.desc;
						v[2].bstrVal = jobInfo->referer;
						v[1].bstrVal = SysAllocString(L"FlashGet");
						v[0].bstrVal =  SysAllocString(L"0");
						h.invoke("AddUrlEx", v, 5);
					}
				}
			} 
			else 
			{
				FGArray fgArray(jobInfo);
				VARIANT v[5];
				try
				{
					fgArray.asVariant(&v[4]);
					v[3].vt = v[2].vt = v[1].vt = v[0].vt = VT_BSTR;
					v[3].bstrVal = jobInfo->referer;
					v[2].bstrVal = SysAllocString(L"FlashGet3");
					v[1].bstrVal = SysAllocString(L"");
					v[0].bstrVal = SysAllocString(L"0");
					h.invoke("AddAll", v, 5);
					
				} catch(...)
				{
					v[2].bstrVal = SysAllocString(L"FlashGet");
					v[1].bstrVal = jobInfo->dlpageCookies;
					try
					{
						h.invoke("AddAll", v, 5);
					} catch(...)
					{
						VARIANT v[4];
						
						fgArray.asVariant(&v[3]);
						v[2].vt = v[1].vt = v[0].vt = VT_BSTR;
						v[2].bstrVal = jobInfo->referer;
						v[1].bstrVal = SysAllocString(L"FlashGet");
						v[0].bstrVal = SysAllocString(L"0");
						
						h.invoke("AddAll", v, 4);
					}
			}
			}
		} catch(...)
		{
			LinkInfo link = jobInfo->links[0];
			char buf[4096];
			sprintf_s(buf, 4096, "%s\n%s\n%s", (char *)link.url, (char *)link.desc, (char *)jobInfo->referer);
			fail(buf, ERR_GENERAL);
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

	void dispatch(const JobInfo *jobInfo)
	{
		try
		{
		  DMSAddUrlFamily::dispatch(jobInfo);
		} catch(...)
		{
			DMSFlashGet2X dms;
			dms.dispatch(jobInfo);
		}
	}
};


class DMSFreeDownloadManager :
	public DMSupportNativeHost
{

protected:

	char * getRegPath(){ 
		return "Software\\Mozilla\\NativeMessagingHosts\\org.freedownloadmanager.fdm5.cnh"; 
	}

public:

	const char * getName() { 
		return "Free Download Manager"; 
	}

	void dispatch(const JobInfo *jobInfo){

		long lc = jobInfo->dlcount;
		if(lc<1) return;

		std::string referer = utf8::narrow(jobInfo->referer);
		std::string useragent = utf8::narrow(jobInfo->useragent);

		Json jsonMsg = Json::Parse("{}");
		jsonMsg.AddProperty("id", Json("4"));
		jsonMsg.AddProperty("type", Json("create_downloads"));
		Json create_downloads = Json::Parse("{}");
		Json downloads = Json::Parse("[]");
		for(int i=0; i<lc; i++){
			//todo: add post data
			std::string cookie = utf8::narrow(jobInfo->links[i].cookies);
			std::string url = utf8::narrow(jobInfo->links[i].url);
			Json dl = Json::Parse("{}");
			dl.AddProperty("url", Json(url));
			dl.AddProperty("originalUrl", Json(url));
			dl.AddProperty("httpReferer", Json(referer));
			dl.AddProperty("userAgent", Json(useragent));
			dl.AddProperty("httpCookies", Json(cookie));
			dl.AddProperty("youtubeChannelVideosDownload", Json(0));
			downloads.Push(dl);
		}
		create_downloads.AddProperty("downloads", downloads);
		jsonMsg.AddProperty("create_downloads", create_downloads);

		NativeHost host(getManifestPath(), "fdm_ffext2@freedownloadmanager.org");

		if(host.init())
		{
			const char* init1 = "{\"id\":\"1\",\"type\":\"handshake\",\"handshake\":{\"api_version\":\"1\",\"browser\":\"Firefox\"}}";
			const char* init2 = "{\"id\":\"2\",\"type\":\"ui_strings\"}";
			const char* init3 = "{\"id\":\"3\",\"type\":\"query_settings\"}";

			//sending the init messages appears to have solved the crash issues 
			//but just for safety...
			Sleep(200);

			//don't attempt to send the message if any of the inits failed
			if(
				host.sendMessage(init1) &&
				host.sendMessage(init2) &&
				host.sendMessage(init3)
			){
				host.sendMessage(jsonMsg.ToString().c_str(), 20000);
			}

		}

		host.close();

	}

};


class DMSFreeDownloadManager3 :
	public DMSupportCOM
{

protected:

	const char * getProgId() { return "WG.WGUrlListReceiver"; }

public:
	
	const char * getName() { return "Free Download Manager 3"; }

	void dispatch(const JobInfo *jobInfo )
	{
		
		int linksCount=jobInfo->dlcount;
		if(linksCount>0)
		{
			CookieManager cm(jobInfo);
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
			fdm.set("Referer",jobInfo->referer);
			for(int j=0; j< linksCount; j++) {
				LinkInfo l = jobInfo->links[j];
				fdm.set("Url",l.url);
				fdm.set("Comment",l.desc);
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

		void dispatch(const JobInfo *jobInfo)
		{
			HELPER(h);

			int linksCount = jobInfo->dlcount;
			if (linksCount > 0)
			{
				if (linksCount < 2)
				{
					VARIANT v[5];

					// write title to param array
					v[0].vt = VT_NULL;
					
					// write ref URL to param array
					v[1].vt = VT_BSTR;
					v[1].bstrVal = jobInfo->referer;

					// write URL title to param array
					v[2].vt = VT_BSTR;
					v[2].bstrVal = jobInfo->links[0].desc;

					// write target URL to param array
					v[3].vt = VT_BSTR;
					v[3].bstrVal = jobInfo->links[0].url;

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
						varStr.bstrVal = jobInfo->links[i].url;
						hResult = SafeArrayPutElement(pSA_URL, index, &varStr);
						if (hResult != S_OK)
						{
							return;
						}
						index[0]++;

						varStr.bstrVal = jobInfo->links[i].desc;
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
					v[3].bstrVal = refer_url;//jobInfo->referer;

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

	void dispatch(const JobInfo *jobInfo)
	{
		HELPER(h);
		CookieManager cm(jobInfo);
		if(jobInfo->optype==OP_ONE) 
		{
			LinkInfo l=jobInfo->links[0];
			VARIANT v[3];
			v[2].vt=v[1].vt=VT_BSTR;
			v[2].bstrVal=l.url; // URL
			v[1].bstrVal=jobInfo->referer; // referer
			v[0].vt=VT_INT;
			v[0].intVal=0; // quiet (0 means "show dialog")
			h.invoke("AddURL",v,3);
		} 
		else
		{
			h.invoke("AddUrlList",FGArray(jobInfo).asVariant(),1);
		}
	}
};


class DMSGetGo : 
	public DMSupportNativeHost
{
protected:

	char * getRegPath(){ 
		return "SOFTWARE\\Mozilla\\NativeMessagingHosts\\GetGoExtensionNative"; 
	}

public:

	const char * getName() { 
		return "GetGo"; 
	}

	void dispatch(const JobInfo *jobInfo)
	{
		int lc = jobInfo->dlcount;
		std::string data = "type:batchDownload||data:[";
		for(int i=0; i<lc; i++)
		{
			//todo: file name when called from browser has additional gibberish characters
			//this download manager is insane and needs to be fed the name part of the file and extension part of the file separately
			std::string fullname = utf8::narrow(jobInfo->links[i].filename);
			std::string name = fullname.substr(0, fullname.find_last_of("."));
			data.append("{\"url\":\"");
			data.append(utf8::narrow(jobInfo->links[i].url));
			data.append("\",\"type\":\"");
			data.append(utf8::narrow(jobInfo->links[i].extension));
			data.append("\",\"size\":\"\",\"name\":\"");
			data.append(name);
			data.append("\"}");
			if(lc-i>1){
				data.append(",");
			}
		}
		data.append("]||");

		NativeHost host(getManifestPath(), "dev@getgosoft.com");

		if(host.init())
		{
			host.sendMessage(data.c_str());
		}

		host.close();
	}
};

class DMSHiDownload :
	public DMSupportCOM
{


protected:
	
	const char * getProgId() { return "NetMoles.NetMoles"; }
	
public:
	
	const char * getName() { return "HiDownload"; }

	
	void dispatch(const JobInfo *jobInfo)
	{
		CookieManager cm(jobInfo);
		
		
		HELPER(h);
		
		
		if(jobInfo->dlcount == 1 && !FindWindow("HiDownload", NULL))
		{
			VARIANT v[2];
			v[1].vt = v[0].vt = VT_BSTR;
			LinkInfo l = jobInfo->links[0];
			v[1].bstrVal = l.url;
			v[0].bstrVal = l.desc;
			h.invoke("NMAddUrl", v, 2);
		} else
		{
			FGArray fgArray(jobInfo);
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

	void dispatch(const JobInfo *jobInfo)
	{
		long lc = jobInfo->dlcount;
		if(lc<1) return;

		LinkInfo l;

		ICIDMLinkTransmitter2* pIDM;
		HRESULT hr = CoCreateInstance(CLSID_CIDMLinkTransmitter, NULL, CLSCTX_LOCAL_SERVER,
					IID_ICIDMLinkTransmitter2, (void**)&pIDM);
		if (S_OK != hr) return;

		if(lc < 2)
		{
			VARIANT reserved;
			reserved.vt=VT_EMPTY;
			l = jobInfo->links[0];
			
			pIDM->SendLinkToIDM2(l.url, jobInfo->referer, l.cookies, l.postdata,
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
					l = jobInfo->links[j];
					
					index[1] = 0;

					url = (LPCOLESTR)l.url;
					cookie = (LPCOLESTR)l.cookies;
					comment = (LPCOLESTR)l.desc;

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
				pIDM->SendLinksArray(jobInfo->referer, &array);
				SafeArrayDestroy(pSA);
			}
		}
		pIDM->Release();
	}
};


class DMSLeechGetBase :
	public DMSupportCOM
{

public:
	
	
	void dispatch(const JobInfo *jobInfo) 
	{	
		HELPER(h);
		CookieManager cm(jobInfo);
		if(jobInfo->dlcount>0) 
		{
			h.invoke(jobInfo->optype==OP_ONE?"AddURL":"Wizard", jobInfo->links[0].url);
		} 
		else 
		{
			h.invoke("Parse",jobInfo->referer);
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

	void dispatch(const JobInfo *jobInfo)
	{
		
		
		int linksCount=jobInfo->dlcount;
		if(linksCount<1) return;
		{
			VARIANT v[3];
			v[2].vt=v[1].vt=v[0].vt=VT_BSTR;
			HELPER(h);
			CookieManager cg(jobInfo);
			DISPID di_AddUrlWithReferer;
			h.getMemberID("AddUrlWithReferer",&di_AddUrlWithReferer);
			
			if(linksCount>1) {
				v[1].bstrVal=v[0].bstrVal=bstr_t("Begin.");
				h.invoke("AddUrl",v,2);
			}
			v[0].bstrVal=jobInfo->referer;
			for(int j=0; j<linksCount; j++) {
				LinkInfo l = jobInfo->links[j];
				v[2].bstrVal=l.url;
				v[1].bstrVal=l.desc;
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

class DMSWestByte :
	public DMSupportCOM
{

public:


	void dispatch(const JobInfo *jobInfo)
	{
		int linksCount=jobInfo->dlcount;
		
		if(linksCount>0)
		{
			
			VARIANT v[2];
			v[0].vt = VT_BSTR ;
			v[0].bstrVal=jobInfo->referer; // referer
			HELPER(h);
			CookieManager cm(jobInfo);
			if(linksCount==1) 
			{
				v[1].vt = VT_BSTR;
				v[1].bstrVal=jobInfo->links[0].url;
				
				h.invoke("AddURL",v,2);
			} 
			else 
			{ 

				FGArray fgArray(linksCount * 2);
				for (int j=0; j < linksCount; j++) {
					LinkInfo l = jobInfo->links[j];
					fgArray.addString(l.url);
					fgArray.addString(l.desc);
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

	void dispatch(const JobInfo *jobInfo)
	{
		CookieManager cm(jobInfo);

		HELPER(h);
		int linksCount=jobInfo->dlcount;
		if(linksCount>0)
		{
			if(linksCount<2)
			{
				LinkInfo link=jobInfo->links[0];
				VARIANT v[4];
				
				v[3].vt=v[2].vt=v[1].vt=v[0].vt=VT_BSTR;
				
				v[0].bstrVal=link.cookies;
				v[1].bstrVal=jobInfo->referer;
				v[2].bstrVal=link.desc;
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
					urlArray.addString( jobInfo->links[i].url );
					txtArray.addString( jobInfo->links[i].desc );
				}

				urlArray.asVariant(&v[3]);
				txtArray.asVariant(&v[2]);
				v[1].vt=v[0].vt=VT_BSTR;
				v[1].bstrVal=jobInfo->referer;
				v[0].bstrVal=jobInfo->links[0].cookies;
				
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
	void dispatch(const JobInfo *jobInfo) 
	{
		char *cmdLine=NULL;
		char *path=NULL;
		char *arg;
		char *tgargs=NULL;
		BOOL ret = false;
		if(jobInfo->dlcount)
		{
			char *referer=jobInfo->referer;

			if (jobInfo->dlcount == 1)
			{
				char *pattern="--reference=%s \"%s\"";
				char *url=jobInfo->links[0].url;
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
				for(int j=0,count=jobInfo->dlcount; j<count; j++)
				{
					fputs(jobInfo->links[j].url,fp);
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



class DMSDownloadAcceleratorManager : 
	public DMSupportNativeHost
{
protected:

	char * getRegPath(){ 
		return "Software\\Mozilla\\NativeMessagingHosts\\com.tensons.dam"; 
	}

public:

	const char * getName() { 
		return "Download Accelerator Manager"; 
	}

	void dispatch(const JobInfo *jobInfo){

		long lc = jobInfo->dlcount;

		std::string referer = utf8::narrow(jobInfo->referer);
		std::string refCookies = utf8::narrow(jobInfo->dlpageCookies);
		std::string useragent = utf8::narrow(jobInfo->useragent);

		Json jsonMsg = Json::Parse("{}");

		if(lc == 1)
		{
			jsonMsg.AddProperty("t", Json(2));
			jsonMsg.AddProperty("u", Json(utf8::narrow(jobInfo->links[0].url)));
			jsonMsg.AddProperty("c", Json(refCookies));
			jsonMsg.AddProperty("r", Json(referer));
			jsonMsg.AddProperty("p", Json(utf8::narrow(jobInfo->links[0].postdata)));
			jsonMsg.AddProperty("i", Json(utf8::narrow(jobInfo->links[0].desc)));
			jsonMsg.AddProperty("a", Json(""));
		}
		else if(lc > 1)
		{
			jsonMsg.AddProperty("t", Json(3));
			std::string urlsStr = "";
			for(int i=0; i<lc; i++)
			{
				urlsStr.append(utf8::narrow(jobInfo->links[i].url));
				urlsStr.append("\\n");
				urlsStr.append(utf8::narrow(jobInfo->links[i].desc));
				//the last url should not have a new line
				if(lc-i>1){
					urlsStr.append("\\n");
				}
			}
			jsonMsg.AddProperty("u", Json(urlsStr));
			jsonMsg.AddProperty("c", Json(refCookies));
			jsonMsg.AddProperty("r", Json(referer));
			jsonMsg.AddProperty("p", Json(""));
			jsonMsg.AddProperty("a", Json(""));
		}

		NativeHost host(getManifestPath(), "dam@tensons.com");

		if(host.init())
		{
			host.waitForOutput(5000);
			std::string init = "{\"t\":1,\"a\":\"" + useragent + "\"}";
			if(host.sendMessage(init.c_str())){
				host.sendMessage(jsonMsg.ToStringOrderedTrimmed().c_str());
			}
		}

		host.close();

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

	const char * getProgId() { return "ReGetDx.ReGetDownloadApi.1"; }
	
public:
	
	const char * getName() { return "ReGet"; }

	void dispatch(const JobInfo *jobInfo)
	{
		CookieManager cm(jobInfo);
		HELPER(h);

		int linksCount = jobInfo->dlcount;
		if(linksCount == 1){
			LinkInfo l = jobInfo->links[0];
			VARIANT conf;
			conf.vt = VT_BOOL;
			conf.boolVal = VARIANT_TRUE;
			h.set("Url", l.url);
			h.set("Referer", jobInfo->referer);
			h.set("Cookie", l.cookies);
			h.set("Info", l.desc);
			h.set("PostData", l.postdata);
			h.set("Confirmation", &conf);
			h.invoke("AddDownload");
		}
		else
		{
			VARIANT v[5];
			v[4].vt = v[3].vt = v[2].vt = v[1].vt = v[0].vt = VT_BSTR;
			for (long j=0, len=jobInfo->dlcount; j<len ; j++) 
			{ 
				v[4].bstrVal=jobInfo->links[j].url;
				v[3].bstrVal=jobInfo->referer;
				v[2].bstrVal=jobInfo->links[j].cookies;
				v[1].bstrVal=jobInfo->links[j].desc;
				v[0].bstrVal=jobInfo->links[j].postdata;
				h.invoke("AddDownloadToList",v,5);
			}

			h.invoke("FlushDownloadList");
		}
	}
};


class DMSReGet_Legacy :
	public DMSupportCOM
{
	
protected:
	const char * getProgId() 
	{
		return "ClickCatcher.DownloadAllFromContextMenu";
	}
public:
	const char * getName() { return "ReGet(Legacy)"; }
	
	void dispatch(const JobInfo *jobInfo) 
	{	
		int linksCount=jobInfo->dlcount;
		
		VARIANT v[5];
		v[0].vt=v[1].vt=v[2].vt=v[3].vt=v[4].vt=VT_BSTR;
		bstr_t referer=jobInfo->referer; // referer
		
		FGCOMHelper *regetAll=NULL;
		DISPID di_AddDownload;
	
		if(linksCount>1) {
			regetAll=new FGCOMHelper(getProgId());
			regetAll->getMemberID("AddDownloadToList",&di_AddDownload);
		} else if(linksCount==1) {
			LinkInfo l=jobInfo->links[0];
			if(strlen(l.postdata)) {
				try 
				{
					FGCOMHelper dl("ReGetDx.RegetDownloadApi");
					dl.set("Url",l.url);
					dl.set("Referer",referer);
					dl.set("Cookie",l.cookies);
					dl.set("Info",l.desc);
					dl.set("PostData",l.postdata);
					dl.invoke("AddDownload");
					return;
				} catch(...) {}
			}
		}
		CookieManager cm(jobInfo);
		for(int j=0; j<linksCount; j++) 
		{	
			LinkInfo l=jobInfo->links[j];
			
			for(int attempts=120; attempts-->0;) 
			{ // we give ReGet 2mins to wake up
				try 
				{
					FGCOMHelper dl("ClickCatcher.DownloadFromContextMenu");
					
					dl.set("Url",l.url);
					dl.set("Referer",referer);
					dl.set("Info",l.desc);
					dl.set("Cookie",l.cookies); // -- it seems a dummy property, we use CookieManager which works
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

	void dispatch(const JobInfo *jobInfo)
	{
		
		HELPER(downloader);
		DISPID di_DownloadURL;
		downloader.getMemberID("DownloadURL",&di_DownloadURL);
		
		VARIANT v[2];
		v[0].vt = v[1].vt = VT_BSTR;
		v[0].bstrVal = jobInfo->referer; // referrer
		for (long j=0, len=jobInfo->dlcount; j < len ; j++) { 
			v[1].bstrVal=jobInfo->links[j].url;
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

	void dispatch(const JobInfo *jobInfo)
	{
		CookieManager cm(jobInfo);
		HELPER(h);
		VARIANT v[8];
		v[7].vt = v[6].vt = v[5].vt = v[4].vt = v[3].vt = VT_BSTR;
		v[2].vt = v[1].vt = v[0].vt = VT_INT;

		for (long j=0, len=jobInfo->dlcount; j<len ; j++) 
		{ 
			v[7].bstrVal=jobInfo->links[j].url;
			v[6].bstrVal=_bstr_t("");
			v[5].bstrVal=_bstr_t("");
			v[4].bstrVal=jobInfo->links[j].desc;
			v[3].bstrVal=jobInfo->referer;
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

	void dispatch(const JobInfo *jobInfo)
	{
		CookieManager cm(jobInfo);
		HELPER(h);
		VARIANT v[2];
		v[0].vt= VT_BYREF;

		FGArray fgArray(jobInfo);
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

	void dispatch(const JobInfo *jobInfo)
	{
		
		HELPER(downloader);
		DISPID di_MenuURL;
		downloader.getMemberID("MenuURL",&di_MenuURL);
		
		VARIANT v[3];
		v[0].vt = v[1].vt = v[2].vt = VT_BSTR;
		v[1].bstrVal = jobInfo->referer; // referrer
		for (long j=0, len=jobInfo->dlcount; j < len ; j++) 
		{ 
			v[2].bstrVal=jobInfo->links[j].url;
			v[0].bstrVal=jobInfo->links[j].cookies;
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

	void dispatch(const JobInfo *jobInfo)
	{
		
	
		HKEY hk = NULL;
		BOOL ok = false;
		char app_path[MAX_PATH];
		if(RegOpenKeyEx(HKEY_CURRENT_USER,_T("Software\\WellGet"),0,KEY_QUERY_VALUE,&hk)==ERROR_SUCCESS)
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
		
		if(jobInfo && jobInfo->dlcount > 0)
		{
			CookieManager cm(jobInfo);
			char cmdLine[32767];
			//todo: what is this op_one? it doesn't work with dlgrab
			if(jobInfo->optype==OP_ONE) 
			{
				try // try COM first, to overcome command line bug in latest (1.25 - 0118) build
				{
					HELPER(h);
	
					LinkInfo l=jobInfo->links[0];
					VARIANT v[3];
					v[2].vt=v[1].vt=v[0].vt=VT_BSTR;
					v[2].bstrVal=l.url; // URL
					v[1].bstrVal=l.desc;
					v[0].bstrVal=jobInfo->referer; // referer
					h.invoke("AddURL", v, 3);
					return;
				} catch(...)
				{ // fall back to command line
					LinkInfo l=jobInfo->links[0];
					
					sprintf_s(cmdLine, sizeof(cmdLine), "%s wget -r:\"%s\" -c:\"%s\" \"%s\"",
						app_path,
						(char *)jobInfo->referer,
						(char *)l.desc, (char *)l.url);
				} 
			}
			else 
			{				
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
				fputs(jobInfo->referer,fp);
				fputc('\n',fp);
				for(int j=0,count=jobInfo->dlcount; j<count; j++) {
					LinkInfo l = jobInfo->links[j];
					fputs(l.url,fp);
					fputc('\n',fp);
					fputs(l.desc,fp);
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
	add(new DMSDownloadAcceleratorManager());
	add(new DMSDownloadMaster());
	add(new DMSEagleGet());
	add(new DMSFlareGet());
	add(new DMSFlashGet());
	add(new DMSFlashGet2());
	add(new DMSFlashGet2X());
	add(new DMSFreeDownloadManager());
	add(new DMSFreeDownloadManager3());
	add(new DMSFreshDownload());
	add(new DMSGetGo());
	//add(new DMSGetRight());
	add(new DMSGigaGet());
	//add(new DMSHiDownload());
	add(new DMSInstantGet());
	add(new DMSInternetDownloadAccelerator());
	add(new DMSInternetDownloadManager());
	add(new DMSLeechGet2002());
	add(new DMSLeechGet());
	add(new DMSMass_Downloader());
	add(new DMSNetAnts());
	//add(new DMSNet_Transport());
	//add(new DMSNet_Transport2());
	//add(new DMSOrbit());
	add(new DMSReGet());
	add(new DMSReGet_Legacy());
	add(new DMSStarDownloader());
	add(new DMSTrueDownloader());
	add(new DMSThunder());
	add(new DMSThunderOld());
	//add(new DMSWellGet());
	add(new DMSwxDownloadFast());
}



DMSupport* createDMS(const char *name) {
	DMSupport *res=DMSFactory::getInstance()->getDMS(name);
	if(res) return res;
	sprintf_s(g_buf, BUF_SIZE, "Unsupported Download Manager %s", name);
	fail(g_buf, ERR_UNSUPPORTED_DM);
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

void performTest()
{
	DMSFactory::getInstance()->checkAll();
}

string readStdin()
{
	const int BUFSIZE = 1024;
	char buf[BUFSIZE];
	unsigned long bytesRead = 0;
	unsigned long totalRead = 0;
	BOOL bSuccess = FALSE;
	vector<char> data;

	while(true)
	{
		data.reserve(BUFSIZE);
		bytesRead = fread(buf, sizeof(char), BUFSIZE, stdin);
		if(bytesRead <= 0)
		{
			break;
		}
		data.insert(data.end(), buf, buf+bytesRead);
		totalRead += bytesRead;
	}

	string input = "";

	if(totalRead > 0)
	{
		input = string(data.begin(), data.end());
	}

	return input;
}

void performJob(const Json &job)
{
	DMSupport *dms = NULL;
	BOOL completed = FALSE;
	string error("");

	try 
	{
		JobInfo jobInfo;

		jobInfo.dlcount = job["dlcount"].AsInt();
		jobInfo.dmName = job["dmName"].AsString();
		int typeId = job["optype"].AsInt();
		jobInfo.optype = (typeId<OP_MIN||typeId>OP_MAX)? OP_ALL : (OpType) typeId;
		jobInfo.referer = utf8::widen(job["referer"].AsString()).c_str();
		jobInfo.dlpageReferer = utf8::widen(job["dlpageReferer"].AsString()).c_str();
		jobInfo.dlpageCookies = utf8::widen(job["dlpageCookies"].AsString()).c_str();
		jobInfo.useragent = utf8::widen(job["useragent"].AsString()).c_str();
			
		for(int i=0; i<jobInfo.dlcount; i++)
		{
			LinkInfo info;
			info.url = utf8::widen(job["links"][i]["url"].AsString()).c_str();
			info.desc = utf8::widen(job["links"][i]["desc"].AsString()).c_str();
			info.cookies = utf8::widen(job["links"][i]["cookies"].AsString()).c_str();
			info.postdata = utf8::widen(job["links"][i]["postdata"].AsString()).c_str();
			info.filename = utf8::widen(job["links"][i]["filename"].AsString()).c_str();
			info.extension = utf8::widen(job["links"][i]["extension"].AsString()).c_str();
			jobInfo.links.push_back(info);
		}

		(dms=createDMS(jobInfo.dmName.c_str()))->dispatch(&jobInfo);
		completed=TRUE;
	} 
	catch(_com_error ce)
	{
		sprintf_s(g_buf, BUF_SIZE, "COM Error%s", ce.ErrorMessage());
	}
	catch(char *ex) {
		sprintf_s(g_buf, BUF_SIZE, "An exception occured%s", ex);
	} 
	catch(...) {
		sprintf_s(g_buf, BUF_SIZE, "An unknown error occured%s", "");
	}
	
	if(dms) delete dms;
	
	if(!completed) fail(g_buf, ERR_JOB_FAILED);
}

int main(int argc, char* argv[])
{
	//if we don't have any arguments just do the test (check for all download managers)
    if(argc == 1)
	{
		performTest();
		exit(0);
	}

	else if(argc == 2)
	{
		try
		{
			string jsonStr = readStdin();
			Json job = Json::Parse(jsonStr.c_str());
			performJob(job);
		}
		catch(...)
		{
			printf("failed to read download data");
			exit(1);
		}
	}
	
}

int WINAPI
WinMain(HINSTANCE inst, HINSTANCE previnst, LPSTR cmdline, int cmdshow)
{
    return main(__argc, __argv); 
}
