#define _WIN32_DCOM
#include <windows.h>
#include "TaskScheduler.h"
#include <General/util/BaseUtil.hpp>
#include <Windows/util/COM/COMHelper.h>
#include <comdef.h>
#include <iostream>
#include <stdio.h>
#include <taskschd.h>
#include <wrl/client.h>
#include <string>
#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsupp.lib")

using namespace std;
using namespace Microsoft::WRL;
using namespace zzj;

std::vector<Task> zzj::TaskScheduler::GetTasks(const std::wstring &taskFolder)
{
    zzj::COMWrapper comWrapper;
    if (comWrapper.IsInitialized() == false)
    {
        throw std::runtime_error("Failed to initialize COM");
    }

    // Create an instance of the Task Service.
    ComPtr<ITaskService> pService;
    auto hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pService));
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to create an instance of the TaskService class with error code " +
                                 std::to_string(hr));
    }

    // Connect to the task service.
    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr))
    {
        throw std::runtime_error("ITaskService::Connect failed with error code " + std::to_string(hr));
    }

    // Get the pointer to the root task folder.
    ComPtr<ITaskFolder> pRootFolder;
    hr = pService->GetFolder(_bstr_t(taskFolder.c_str()), &pRootFolder);
    if (FAILED(hr))
    {
        throw std::runtime_error("ITaskService::GetFolder failed with error code " + std::to_string(hr));
    }

    // Get the registered tasks in the folder.
    ComPtr<IRegisteredTaskCollection> pTaskCollection;
    hr = pRootFolder->GetTasks(NULL, &pTaskCollection);
    if (FAILED(hr))
    {
        throw std::runtime_error("Cannot get the registered tasks in the folder with error code " + std::to_string(hr));
    }

    LONG numTasks = 0;
    hr            = pTaskCollection->get_Count(&numTasks);
    if (FAILED(hr))
    {
        throw std::runtime_error("Cannot get the number of tasks in the folder with error code " + std::to_string(hr));
    }

    std::vector<Task> tasks;
    for (LONG i = 0; i < numTasks; i++)
    {
        ComPtr<IRegisteredTask> pRegisteredTask;
        hr = pTaskCollection->get_Item(_variant_t(i + 1), &pRegisteredTask);
        if (FAILED(hr))
        {
            throw std::runtime_error("Cannot get the registered task item with error code " + std::to_string(hr));
        }
        tasks.push_back(Task(pRegisteredTask));
    }
    return tasks;
}