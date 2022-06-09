#include <General/util/BaseUtil.hpp>
#include <General/util/Thread.h>
#include <Windows.h>
#include <tlhelp32.h>
namespace zzj
{
ThreadV2::ThreadV2(int tid)
{
    this->tid = tid;
    processInfo.reset();
}
std::vector<ThreadV2> ThreadV2::EnumAllThreads()
{
    HANDLE hSnapThread = NULL;
    LPPROCESSENTRY32 ppe32;
    PTHREADENTRY32 pte32;
    std::vector<ThreadV2> ret;

    pte32 = new THREADENTRY32();
    DEFER
    {
        delete pte32;
    };
    pte32->dwSize = sizeof(THREADENTRY32);

    hSnapThread = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    DEFER
    {
        if (hSnapThread)
            CloseHandle(hSnapThread);
    };

    if (!hSnapThread)
        return {};

    if (Thread32First(hSnapThread, pte32))
    {
        do
        {
            ThreadV2 thread;
            thread.tid         = (int)pte32->th32ThreadID;
            thread.processInfo = std::make_shared<ProcessV2>(pte32->th32OwnerProcessID);
            ret.push_back(std::move(thread));
        } while (Thread32Next(hSnapThread, pte32));
    }

    return ret;
}
}; // namespace zzj