#include "stdafx.h"
#include <stdio.h>
#include "NativeHost.h"
#include "jsonla.h"
#include "utf8.h"

Pipe::Pipe(char* nm)
{
    name = nm;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;
}
bool Pipe::init()
{
    readHandle = CreateNamedPipeW(
        utf8::widen(name).c_str(),
        readFlags,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        1 /* number of connections */,
        BUF4K /* output buffer size */,
        BUF4K /* input buffer size */,
        0 /* timeout */,
        &saAttr
    );
    if(readHandle == INVALID_HANDLE_VALUE){
        printf("error creating read handle: %d\n", GetLastError());
        return false;
    }

    writeHandle = CreateFileW(
        utf8::widen(name).c_str(),
        GENERIC_WRITE,
        0,
        &saAttr,
        OPEN_EXISTING,
        writeFlags,
        NULL
    );
    if(writeHandle == INVALID_HANDLE_VALUE){
        printf("error creating write handle: %d\n", GetLastError());
        return false;
    }

    return true;
}
void Pipe::close()
{
    CloseHandle(readHandle);
    CloseHandle(writeHandle);
}


OutputPipe::OutputPipe(char* nm) : Pipe(nm)
{
    readFlags = 0 | PIPE_ACCESS_INBOUND;
    writeFlags = FILE_FLAG_OVERLAPPED | FILE_ATTRIBUTE_NORMAL;
}
bool OutputPipe::write(const char* data, DWORD dataLen)
{
    if(writeHandle == INVALID_HANDLE_VALUE){
        printf("cannot write to invalid handle: %d\n", GetLastError());
        return false;
    }

    DWORD dwWritten;
    BOOL bSuccess = WriteFile(writeHandle, data, dataLen, &dwWritten, NULL);
    if(bSuccess && (dwWritten==dataLen)){
        return true;
    }
    else if(!bSuccess){
        printf("write failed: %d\n", GetLastError());
    }
    return false;
}


InputPipe::InputPipe(char* nm) : Pipe(nm)
{
    readFlags = FILE_FLAG_OVERLAPPED | PIPE_ACCESS_INBOUND;
    writeFlags = 0 | FILE_ATTRIBUTE_NORMAL;
}
bool InputPipe::dataAvailable(){
    DWORD dwAvail;
	const int intrvl = 200;
    for(int totalSleep=0; totalSleep < MSG_RESPONSE_TIMEOUT; totalSleep+=intrvl){
        BOOL success = PeekNamedPipe(readHandle, NULL, NULL, NULL, &dwAvail, NULL);
        if( success && dwAvail > 0 ){ return true; }
        //if we didn't get anything then wait
        Sleep(intrvl);
    }
    return false;
}
bool InputPipe::read(CHAR* readBuf, int bufLen, DWORD& dwRead)
{
    if(readHandle == INVALID_HANDLE_VALUE){
        printf("cannot read from invalid handle: %d\n", GetLastError());
        return false;
    }

    BOOL bSuccess = ReadFile(readHandle, readBuf, bufLen, &dwRead, NULL);
    if(!bSuccess){
        printf("read failed: %d\n", GetLastError());
        return false;
    }

    return true;
}


bool Process::create(HANDLE hStdIN, HANDLE hStdOUT, std::string cmd, std::string args, std::string workDir)
{
    STARTUPINFOW startupInfo;

    ZeroMemory( &startupInfo, sizeof(STARTUPINFOW) );
    ZeroMemory( &procInfo, sizeof(PROCESS_INFORMATION) );

    startupInfo.cb = sizeof(STARTUPINFOW);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;

    startupInfo.hStdInput = hStdIN;
    startupInfo.hStdOutput = hStdOUT;
    startupInfo.hStdError = hStdOUT;

	DWORD processFlags = CREATE_NO_WINDOW;

    //todo: support hosts that are scripts like .bat
    BOOL bSuccess = CreateProcessW(
        utf8::widen(cmd).c_str(),
        const_cast<wchar_t *>(utf8::widen(args).c_str()),            // command line
        NULL,								// process security attributes
        NULL,								// primary thread security attributes
        TRUE,								// handles are inherited
        processFlags,						// creation flags
        NULL,								// use parent's environment
        utf8::widen(workDir).c_str(),		// use parent's current directory
        &startupInfo,						// STARTUPINFO pointer
        &procInfo							// receives PROCESS_INFORMATIN
    );
	
	//we don't need these
    CloseHandle(hStdIN);
    CloseHandle(hStdOUT);
    CloseHandle(procInfo.hThread);

    if(!bSuccess){
        printf("process creation failed: %d\n", GetLastError());
        return false;
    }

    return true;
}

void Process::close()
{
    TerminateProcess(procInfo.hProcess, 0);
    CloseHandle(procInfo.hProcess);
}

NativeHost::NativeHost(std::string manifestPath, std::string extensionId) :
    process(),
    hostStdIN("\\\\.\\pipe\\hostStdIN"),
    hostStdOUT("\\\\.\\pipe\\hostStdOUT"),
    manifPath(manifestPath),
    extId(extensionId),
    hostPath("")
{}

bool NativeHost::init()
{
    initHostPath();

    bool success = true;
    success &= hostStdIN.init();
    success &= hostStdOUT.init();

    if(!success){
        return false;
    }

    std::string args = manifPath + " " + extId;
    std::string exe = const_cast<char *>(hostPath.c_str());
    std::string exeArgs = const_cast<char *>(args.c_str());
    std::string workDir = const_cast<char *>(hostDir.c_str());

    success = process.create(hostStdIN.readHandle, hostStdOUT.writeHandle, exe, exeArgs, workDir);

    return success;

}

void NativeHost::initHostPath()
{
	using namespace ggicci;

    hostPath = "";
    hostDir = "";
    std::ifstream file(utf8::widen(manifPath.c_str()));
    std::string fileStr = "";
    std::string tmp;

    while (std::getline(file, tmp)){ fileStr += tmp; }
    if(fileStr.length() == 0){
		return;
    }

	std::string path;

	try{
		Json manifJson = Json::Parse(fileStr.c_str());
		path = manifJson["path"].AsString();
	}catch(...){
		return;
	}

    char mDrive[_MAX_DRIVE], mDir[_MAX_DIR], mFilename[_MAX_FNAME], mExt[_MAX_EXT];
    _splitpath_s(manifPath.c_str(), mDrive, _MAX_DRIVE, mDir, _MAX_DIR, mFilename, _MAX_FNAME, mExt, _MAX_EXT);

    char pDrive[_MAX_DRIVE], pDir[_MAX_DIR], pFilename[_MAX_FNAME], pExt[_MAX_EXT];
    _splitpath_s(path.c_str(), pDrive, _MAX_DRIVE, pDir, _MAX_DIR, pFilename, _MAX_FNAME, pExt, _MAX_EXT);

    // if host path is relative
    if(strcmp(pDrive, "") == 0){
        hostPath.append(mDrive).append(mDir).append(path);
        hostDir.append(mDrive).append(mDir);
    }
    // if path is absolute
    else{
        hostPath.append(path);
        hostDir.append(pDrive).append(pDir);
    }

}
bool NativeHost::sendMessage(const char* json)
{
    int jsonLen = strlen(json);
    int dataLen = jsonLen + 4;
    char* data = new char[dataLen];

    // Native messaging protocol requires message length as a 4-byte integer prepended to the JSON string
    data[0] = char(((jsonLen>>0) & 0xFF));
    data[1] = char(((jsonLen>>8) & 0xFF));
    data[2] = char(((jsonLen>>16) & 0xFF));
    data[3] = char(((jsonLen>>24) & 0xFF));
    // Add the JSON after the length
    int i;
    for(i=0; i<jsonLen; i++){
        data[i+4] = json[i];
    }

    bool success = hostStdIN.write(data, dataLen);
    delete [] data;
    if(!success){
        return false;
    }

    if(!hostStdOUT.dataAvailable()){
        printf("no data available");
        return false;
    }

    DWORD dwRead, dwWritten;
    CHAR chBuf[BUF2K]; 
    success = hostStdOUT.read(chBuf, BUF2K, dwRead);
    if(!success){
        return false;
    }

    //todo: remove in production
    //remove first four bytes of size data
    for(i=0; i<dwRead-4; i++){
        chBuf[i] = chBuf[i+4];
    }
    chBuf[i] = '\n';
    HANDLE hParentStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    success = WriteFile(hParentStdOut, chBuf, dwRead-4+1, &dwWritten, NULL);
    if(!success){
        printf("redirecting failed: %d\n", GetLastError());
        return false;
    }

    return true;
}
void NativeHost::close()
{
    hostStdIN.close();
    hostStdOUT.close();
    process.close();
}

