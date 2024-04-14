#include "HandleHelper.h"
#include <Windows/util/Apiloader/ApiTypeDefs.h>
#include <Windows/util/Common.h>
#include <General/util/Process/ThreadPool.hpp>
#include <spdlog/spdlog.h>
using namespace zzj;

bool ScopeKernelHandle::SetInherited(bool isInherited)
{
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    return SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
}

bool ScopeKernelHandle::SetCanClose(bool canClose)
{
    this->canClose = canClose;
    if (canClose)
        return SetHandleInformation(handle, HANDLE_FLAG_PROTECT_FROM_CLOSE, 0);
    else
        return SetHandleInformation(handle, HANDLE_FLAG_PROTECT_FROM_CLOSE, HANDLE_FLAG_PROTECT_FROM_CLOSE);
}
//maybe hang
std::string ScopeKernelHandle::GetHandleType(HANDLE handle)
{
    if (handle == INVALID_HANDLE_VALUE)
        return "";
    UCHAR buffer[sizeof(OBJECT_TYPE_INFORMATION) + 256 * sizeof(WCHAR)];
    ULONG length;
    auto NtQueryObjectFunc =
        (zzj::ApiLoader::pNtQueryObject)GetProcAddress(GetModuleHandle(TEXT("ntdll.dll")), "NtQueryObject");

    if (NtQueryObjectFunc(handle, ObjectTypeInformation, &buffer, sizeof(buffer), &length) == 0)
    {
        OBJECT_TYPE_INFORMATION *typeInfo = (OBJECT_TYPE_INFORMATION *)buffer;
        return zzj::str::w2utf8(std::wstring(typeInfo->TypeName.Buffer, typeInfo->TypeName.Length / sizeof(WCHAR)));
    }
    return "";
}

boost::filesystem::path zzj::ScopeKernelHandle::GetFileHandlePath(HANDLE handle)
{
    boost::filesystem::path path;
    if (!IsHandleValid(handle) || GetFileType(handle) != FILE_TYPE_DISK)
        return path;

    std::vector<char> vec;
    vec.resize(1024);

    auto res = GetFinalPathNameByHandleA(handle, vec.data(), vec.size(), FILE_NAME_NORMALIZED);
    if (res == 0)
        return path;

    std::string strFileName = vec.data();
    if (strFileName.find("\\\\?\\") == 0)
        strFileName = strFileName.substr(4);
    return boost::filesystem::absolute(strFileName);
}

std::vector<std::tuple<SYSTEM_HANDLE_TABLE_ENTRY_INFO, HANDLE>> zzj::ScopeKernelHandle::FindIf(
    std::function<bool(const SYSTEM_HANDLE_TABLE_ENTRY_INFO &info, const HANDLE& duplicateHandle)> func)
{
    HMODULE Ntdll = GetModuleHandleA("ntdll");
    auto NtQuerySystemInformation =
        (zzj::ApiLoader::pNtQuerySystemInformation)GetProcAddress(Ntdll, "NtQuerySystemInformation");

    DWORD size = sizeof(SYSTEM_HANDLE_INFORMATION);

    SYSTEM_HANDLE_INFORMATION *hInfo = (SYSTEM_HANDLE_INFORMATION *)new byte[size]{0};

    // we use this for checking if the Native functions succeed
    NTSTATUS NtRet = NULL;
    do
    {
        // delete the previously allocated memory on the heap because it wasn't large enough to store all the handles
        delete[] hInfo;

        // increase the amount of memory allocated by 50%
        size *= 6;
        hInfo = (PSYSTEM_HANDLE_INFORMATION) new byte[size];
        Sleep(1); // sleep for the cpu

        // we continue this loop until all the handles have been stored
    } while ((NtRet = NtQuerySystemInformation(SystemHandleInformation, hInfo, size, NULL)) ==
             STATUS_INFO_LENGTH_MISMATCH);
    if (!NT_SUCCESS(NtRet))
    {
        throw std::runtime_error("Failed to get all handles");
    }
    DEFER
    {
        if (hInfo)
            delete[] hInfo;
    };
    int threadCount = 4;
    zvpro::ThreadPool<int> threadPool(threadCount);
    std::vector<std::tuple<SYSTEM_HANDLE_TABLE_ENTRY_INFO, HANDLE>> res;
    std::mutex mutex;
    auto handlerLambda = [](std::vector<std::tuple<SYSTEM_HANDLE_TABLE_ENTRY_INFO, HANDLE>> &res, std::mutex &mutex,int start,int end,SYSTEM_HANDLE_INFORMATION* hInfo,
           std::function<bool(const SYSTEM_HANDLE_TABLE_ENTRY_INFO &info, const HANDLE &duplicateHandle)> func) {
        static HMODULE Ntdll = GetModuleHandleA("ntdll");

        static auto NtDuplicateObject = (zzj::ApiLoader::pNtDuplicateObject)GetProcAddress(Ntdll, "NtDuplicateObject");

        static auto NtOpenProcess = (zzj::ApiLoader::pNtOpenProcess)GetProcAddress(Ntdll, "NtOpenProcess");

        static OBJECT_ATTRIBUTES Obj_Attribute = {0};
        Obj_Attribute.Length            = sizeof(OBJECT_ATTRIBUTES);

        spdlog::info("start:{:x} end:{:x}",start,end);
        for (int i = start; i < end; i++)
        {
			if(i % 0x10000 == 0)
				spdlog::info("i:{:x}",i);
            auto tableEnrty = hInfo->Handles[i];
            if (!zzj::ScopeKernelHandle::IsHandleValid((HANDLE)tableEnrty.Handle))
                continue;
            HANDLE procHandle      = NULL;
            CLIENT_ID clientID     = {0};
            clientID.UniqueProcess = (HANDLE)tableEnrty.ProcessId;

            auto NtRet = NtOpenProcess(&procHandle, PROCESS_DUP_HANDLE, &Obj_Attribute, &clientID);
            DEFER
            {
                if (procHandle)
                    CloseHandle(procHandle);
            };
            if (!IsHandleValid(procHandle) || !NT_SUCCESS(NtRet))
                continue;

            HANDLE duplicateHandle;
            NtRet = NtDuplicateObject(procHandle, (HANDLE)tableEnrty.Handle, GetCurrentProcess(), &duplicateHandle, 0,
                                      0, DUPLICATE_SAME_ACCESS);
            if (NtRet != 0 || !IsHandleValid(duplicateHandle))
                continue;
            try
            {
                if (func(tableEnrty, duplicateHandle))
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    res.push_back(std::make_tuple(tableEnrty, duplicateHandle));
                }
                else
                    CloseHandle(duplicateHandle);
            }
            catch (const std::exception &e)
            {
                CloseHandle(duplicateHandle);
            }
        }
        return;
    };
    for (int i = 0; i < threadCount; i++)
	{
		threadPool.enqueue(handlerLambda, std::ref(res), std::ref(mutex), i * hInfo->HandleCount / threadCount,
						   (i + 1) * hInfo->HandleCount / threadCount, hInfo, func);
	}
    threadPool.Stop();
    return res;
}
