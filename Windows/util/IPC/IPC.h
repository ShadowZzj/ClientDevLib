#pragma once
#include <General/util/BaseUtil.hpp>
#include <General/util/StrUtil.h>
#include <Windows/util/log.h>
#include <Windows.h>
namespace zzj
{
namespace IPC
{
class SharedMemory
{
  public:
    SharedMemory(const wchar_t *name, unsigned int bufSize = 0);
    unsigned int Get(void *);
    bool Set(void *, unsigned int len);
};

class ReadEndPipe
{
  public:
    ReadEndPipe(HANDLE read)
    {
        SetHandle(read);
    };
    ~ReadEndPipe()
    {
    }
    int SyncRead(void *buf, size_t bufSize, size_t noReturnBytes = 1);
    bool SetHandle(HANDLE);

  private:
    HANDLE readHandle = INVALID_HANDLE_VALUE;
};

class WriteEndPipe
{
  public:
    WriteEndPipe(HANDLE write)
    {
        SetHandle(write);
    };
    ~WriteEndPipe()
    {
    }
    int SyncWrite(void *buf, size_t bufSize, size_t noReturnBytes = 0);
    bool SetHandle(HANDLE write);

  private:
    HANDLE writeHandle = INVALID_HANDLE_VALUE;
};
class PipeServer
{
  public:
    PipeServer(std::string name, LPSECURITY_ATTRIBUTES pSecu = nullptr)
    {
        this->name           = name;
        std::string pipeName = "\\\\.\\pipe\\";
        pipeName += name;

        pipe = CreateNamedPipeA(pipeName.c_str(),         // pipe name
                                PIPE_ACCESS_DUPLEX,       // read/write access
                                PIPE_TYPE_MESSAGE |       // message type pipe
                                    PIPE_WAIT,            // blocking mode
                                PIPE_UNLIMITED_INSTANCES, // max. instances
                                512,                      // output buffer size
                                512,                      // input buffer size
                                0,                        // client time-out
                                pSecu);                   // default security attribute

        if (pipe == INVALID_HANDLE_VALUE)
            CrashMe();
    }
    bool Listen()
    {
        bool connected = ConnectNamedPipe(pipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        return connected;
    }
    int Write(const char *str, UINT size)
    {
        DWORD bytesWrite;
        bool success = WriteFile(pipe, str, size, &bytesWrite, NULL);
        if (success)
            return bytesWrite;
        else
            return -1;
    }
    int Read(char *buffer, UINT size)
    {
        DWORD bytesRead;
        bool success = ReadFile(pipe, buffer, size, &bytesRead, NULL);
        if (success)
            return bytesRead;
        else
            return -1;
    }
    void Close()
    {
        CloseHandle(pipe);
    }

  private:
    HANDLE pipe;
    std::string name;
};
class PipeClient
{
  public:
    PipeClient(std::string name)
    {
        this->name           = name;
        std::string pipeName = "\\\\.\\pipe\\";
        pipeName += name;
        while (1)
        {
            pipe = CreateFileA(pipeName.c_str(), GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);

            if (pipe != INVALID_HANDLE_VALUE)
            {
                file_logger->info("PipeClient connects success");
                break;
            }
            // file_logger->info("PipeClient connects fails. Try another connect.");
            Sleep(2000);
        }
    }
    int Write(const char *str, UINT size)
    {
        DWORD bytesWrite;
        bool success = WriteFile(pipe, str, size, &bytesWrite, NULL);
        if (success)
            return bytesWrite;
        else
            return -1;
    }
    int Read(char *buffer, UINT size)
    {
        DWORD bytesRead;
        bool success = ReadFile(pipe, buffer, size, &bytesRead, NULL);
        if (success)
            return bytesRead;
        else
            return -1;
    }
    void Close()
    {
        CloseHandle(pipe);
    }

  private:
    HANDLE pipe;
    std::string name;
};
}; // namespace IPC
}; // namespace zzj