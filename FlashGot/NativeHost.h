#include <windows.h>
#include <string>
#include <fstream>

#ifndef NATIVEHOST_H_INCLUDED
#define NATIVEHOST_H_INCLUDED

#define BUF1K 1024
#define BUF2K 2048
#define BUF4K 4096

#define MSG_RESPONSE_TIMEOUT 10000

using namespace std;

class Pipe{
protected:
    SECURITY_ATTRIBUTES saAttr;
    DWORD readFlags, writeFlags;
    string name;
    //protected constructor because we don't want Pipe to be directrly instantiated
    Pipe(char* nm);
public:
    HANDLE readHandle, writeHandle;
    virtual ~Pipe(){close();}
    bool init();
    void close();
};

class OutputPipe: public Pipe{
public:
    OutputPipe(char* nm);
    bool write(const char* data, DWORD dataLen);
};

class InputPipe: public Pipe{
public:
    InputPipe(char* nm);
    bool dataAvailable();
    bool read(CHAR* readBuf, int bufLen, DWORD& dwRead);
};

class Process{
private:
    PROCESS_INFORMATION procInfo;
public:
    virtual ~Process(){close();}
    bool create(HANDLE hStdIN, HANDLE hStdOUT, string cmd, string args, string workDir);
    void close();
};

class NativeHost{
private:
    string manifPath, hostPath, hostDir, extId;
    Process process;
    OutputPipe hostStdIN;
    InputPipe hostStdOUT;
    void initHostPath();
public:
    NativeHost(string manifestPath, string extensionId);
    virtual ~NativeHost(){close();}
    bool init();
    bool sendMessage(const char* json);
    void close();
};

#endif // NATIVEHOST_H_INCLUDED
