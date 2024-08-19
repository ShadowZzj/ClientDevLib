#pragma once
#include <Windows.h>
#include <Psapi.h>
#include <mutex>
namespace zzj
{
namespace AntiDebug
{
class MemoryScanDetector
{
   public:
    static LONG WINAPI PageExceptionHandler(EXCEPTION_POINTERS* ExceptionInfo)
    {
        if (ExceptionInfo->ExceptionRecord->ExceptionCode == STATUS_GUARD_PAGE_VIOLATION)
        {
            isGuardedMemoryTouched = TRUE;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    static MemoryScanDetector* CreateInstance()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_instance) return nullptr;
        isGuardedMemoryTouched = FALSE;
        return new MemoryScanDetector();
    }
    static void DestroyInstance()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_instance)
        {
            delete m_instance;
            m_instance = nullptr;
        }
    }
    bool IsMemoryScanned1()
    {
        if (workingSetMem == nullptr)
        {
            return false;
        }

        PSAPI_WORKING_SET_EX_INFORMATION wsInfo;
        wsInfo.VirtualAddress = (PVOID)workingSetMem;

        if (QueryWorkingSetEx(GetCurrentProcess(), &wsInfo,
                              sizeof(PSAPI_WORKING_SET_EX_INFORMATION)))
        {
            if (wsInfo.VirtualAttributes.Valid)
                return true;
            else
                return false;
        }
        return false;
    }

   private:
    //this detection not work
    bool IsMemoryScanned2()
    {
        if (guardedMem == nullptr)
        {
            return false;
        }
        return isGuardedMemoryTouched;
    }

    MemoryScanDetector()
    {
        workingSetMem = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        guardedMem =
            VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE | PAGE_GUARD);
        handler = AddVectoredExceptionHandler(1, PageExceptionHandler);
    }
    ~MemoryScanDetector()
    {
        if (workingSetMem != nullptr)
        {
            VirtualFree(workingSetMem, 0, MEM_RELEASE);
        }
        if (guardedMem != nullptr)
        {
            VirtualFree(guardedMem, 0, MEM_RELEASE);
        }
        if (handler != nullptr)
        {
            RemoveVectoredExceptionHandler(handler);
        }
    }

   private:
    inline static MemoryScanDetector* m_instance = nullptr;
    inline static std::mutex m_mutex;
    inline static bool isGuardedMemoryTouched = false;
    LPVOID workingSetMem = nullptr;
    LPVOID guardedMem = nullptr;
    PVOID handler = nullptr;
};
};  // namespace AntiDebug
};  // namespace zzj