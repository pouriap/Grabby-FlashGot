#ifndef NATIVEHOST_H_INCLUDED
#define NATIVEHOST_H_INCLUDED

#include <windows.h>
#include <string>
#include <fstream>

#define BUF1K 1024
#define BUF2K 2048
#define BUF4K 4096

#define MSG_RESPONSE_TIMEOUT 5000

class Pipe{
protected:
    SECURITY_ATTRIBUTES saAttr;
    DWORD readFlags, writeFlags;
    //protected constructor because we don't want Pipe to be directly instantiated
    Pipe();
public:
    HANDLE readHandle, writeHandle;
    virtual ~Pipe(){close();}
    bool init();
    void close();
};

class OutputPipe: public Pipe{
public:
    OutputPipe();
    bool write(const char* data, DWORD dataLen);
};

class InputPipe: public Pipe{
public:
    InputPipe();
    bool dataAvailable(int timeout);
    bool read(char* readBuf, int bufLen, DWORD& dwRead);
};

class Process{
private:
    PROCESS_INFORMATION procInfo;
public:
    virtual ~Process(){close();}
    bool create(const HANDLE &hStdIN, const HANDLE &hStdOUT, std::string exe, std::string args, std::string workDir);
    void close();
};

class NativeHost{
private:
    std::string manifPath, hostPath, hostDir, extId;
    Process process;
    OutputPipe hostStdIN;
    InputPipe hostStdOUT;
    void initHostPath();
public:
    NativeHost(std::string manifestPath, std::string extensionId);
    virtual ~NativeHost(){close();}
    bool init();
	bool sendMessage(const char* json, int timeout=MSG_RESPONSE_TIMEOUT);
	void waitForOutput(int timeout);
	void readAll();
    void close();
};

#endif // NATIVEHOST_H_INCLUDED
