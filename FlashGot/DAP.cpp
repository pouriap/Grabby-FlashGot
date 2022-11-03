#include "stdafx.h"
#include "FlashGot.h"

#import "progid:InternetExplorer.Application.1"
#import "progid:ScriptBridge.ScriptBridge.1" 
using namespace SHDocVw;
using namespace MSHTML;



void DMSDownloadAcceleratorPlus::dispatch(const JobInfo *jobInfo)
{
	HELPER(helper);
	switch(jobInfo->optype) 
	{
		case OP_ONE:
		{
			VARIANT v[4];
			LinkInfo l=jobInfo->links[0];
			v[0].vt=v[1].vt=v[2].vt=v[3].vt=v[3].vt=VT_BSTR;
			v[3].bstrVal= l.url; // URL
			v[2].bstrVal= jobInfo->referer ; // Referer
			v[1].bstrVal= l.cookies ; // cookie
			v[0].bstrVal= l.desc ; // info
			helper.invoke("MenuUrl2",v,4);
			break;
		}
		
		case OP_SEL: case OP_ALL:
		{
			IWebBrowser2Ptr ie(__uuidof(InternetExplorer));	
			InternetSetCookie(jobInfo->referer,NULL,jobInfo->dlpageCookies);
			
			
			ie->PutSilent(VARIANT_TRUE);
			ie->PutOffline(VARIANT_TRUE);
			
			VARIANT v1,v2;
			v1.vt=v2.vt=VT_BSTR;
			v1.bstrVal=jobInfo->referer;
			v2.bstrVal="Referer: "+jobInfo->dlpageReferer;
			
			
			ie->Navigate2(&v1,&vtMissing,&vtMissing,&vtMissing,&v2);
			long timeout=DMS_POLL_TIMEOUT;
			for(int j = 2; j--> 0;)
			{
				while(!ie->ReadyState)
				{ 
					if( (timeout -= DMS_POLL_DELAY) <0) 
					{
						fail("DAP Timeout", 9000);
					}
					Sleep(DMS_POLL_DELAY);
				}
			}
			ie->Stop();
			IHTMLDocument2Ptr doc=NULL;
			
			try { 
				doc=ie->GetDocument();
			} catch(...) {
			}
				
		
			if(doc) {
				
				bstr_t html;
				for(int j=0, linksCount=jobInfo->dlcount; j<linksCount; j++) { // skipping postdata
					LinkInfo l = jobInfo->links[j];
					html+="<a href=\""+l.url+"\">"
						+l.desc+"</a>";
				}
				IHTMLElementPtr body = doc->Getbody();
				if(body)
				{
					body->innerHTML=html;
				
					VARIANT v;
					v.vt=VT_DISPATCH ;
					v.pdispVal = doc;
					
					helper.invoke("LeechLinks",&v,1);
				}
				
			}
		}
	}
}