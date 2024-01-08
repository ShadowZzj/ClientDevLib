#pragma once
#include <taskschd.h>
#include <wrl/client.h>
#include <vector>
#include <string>
namespace zzj
{
    class Task
    {
        public:
        Task(const Microsoft::WRL::ComPtr<IRegisteredTask> &task)
        {
            this->task = task;
        }
        std::wstring GetName()
        {
            BSTR name = NULL;
            task->get_Name(&name);
            std::wstring nameWstr = name;
            SysFreeString(name);
            return name;
        }
        std::wstring GetPath()
        {
            BSTR path = NULL;
            task->get_Path(&path);
            std::wstring pathWstr = path;
            SysFreeString(path);
            return path;
        }
        TASK_STATE GetState()
        {
            TASK_STATE state;
            task->get_State(&state);
            return state;
        }
        bool IsEnabled()
        {
            VARIANT_BOOL enabled;
            task->get_Enabled(&enabled);
            return enabled == VARIANT_TRUE;
        }
        bool SetEnabled(bool enabled)
        {
            VARIANT_BOOL vEnabled = enabled ? VARIANT_TRUE : VARIANT_FALSE;

            if (IsEnabled() == enabled)
            {
                return true;
            }
            return SUCCEEDED(task->put_Enabled(vEnabled));
        }
        
        private:
        Microsoft::WRL::ComPtr<IRegisteredTask> task;
    };
    class TaskScheduler
    {
        public:
        static std::vector<Task> GetTasks(const std::wstring &taskFolder= L"\\");
    };

};
