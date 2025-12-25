#pragma once
#include <taskschd.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace zzj
{
class Task
{
   public:
    explicit Task(const Microsoft::WRL::ComPtr<IRegisteredTask> &task) { this->task = task; }

    std::wstring GetName() const
    {
        BSTR name = NULL;
        task->get_Name(&name);
        std::wstring nameWstr = name ? name : L"";
        if (name)
        {
            SysFreeString(name);
        }
        return nameWstr;
    }

    std::wstring GetPath() const
    {
        BSTR path = NULL;
        task->get_Path(&path);
        std::wstring pathWstr = path ? path : L"";
        if (path)
        {
            SysFreeString(path);
        }
        return pathWstr;
    }

    TASK_STATE GetState() const
    {
        TASK_STATE state = TASK_STATE_UNKNOWN;
        task->get_State(&state);
        return state;
    }

    bool IsEnabled() const
    {
        VARIANT_BOOL enabled = VARIANT_FALSE;
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

    // Get a short description of task triggers
    std::vector<std::wstring> GetTriggers() const
    {
        std::vector<std::wstring> result;
        Microsoft::WRL::ComPtr<ITaskDefinition> definition;
        if (FAILED(task->get_Definition(&definition)) || !definition)
        {
            return result;
        }

        Microsoft::WRL::ComPtr<ITriggerCollection> triggerCollection;
        if (FAILED(definition->get_Triggers(&triggerCollection)) || !triggerCollection)
        {
            return result;
        }

        LONG count = 0;
        if (FAILED(triggerCollection->get_Count(&count)))
        {
            return result;
        }

        for (LONG i = 1; i <= count; ++i)
        {
            Microsoft::WRL::ComPtr<ITrigger> trigger;
            if (FAILED(triggerCollection->get_Item(i, &trigger)) || !trigger)
            {
                continue;
            }

            TASK_TRIGGER_TYPE2 type = TASK_TRIGGER_TIME;
            if (FAILED(trigger->get_Type(&type)))
            {
                continue;
            }

            std::wstring desc;
            switch (type)
            {
                case TASK_TRIGGER_TIME:
                    desc = L"Time";
                    break;
                case TASK_TRIGGER_DAILY:
                    desc = L"Daily";
                    break;
                case TASK_TRIGGER_WEEKLY:
                    desc = L"Weekly";
                    break;
                case TASK_TRIGGER_MONTHLY:
                    desc = L"Monthly";
                    break;
                case TASK_TRIGGER_MONTHLYDOW:
                    desc = L"MonthlyDOW";
                    break;
                case TASK_TRIGGER_EVENT:
                    desc = L"Event";
                    break;
                case TASK_TRIGGER_LOGON:
                    desc = L"Logon";
                    break;
                case TASK_TRIGGER_SESSION_STATE_CHANGE:
                    desc = L"SessionChange";
                    break;
                case TASK_TRIGGER_BOOT:
                    desc = L"Boot";
                    break;
                case TASK_TRIGGER_IDLE:
                    desc = L"Idle";
                    break;
                default:
                    desc = L"Other";
                    break;
            }

            result.push_back(desc);
        }

        return result;
    }

    std::vector<std::wstring> GetActions() const
    {
        std::vector<std::wstring> result;
        Microsoft::WRL::ComPtr<ITaskDefinition> definition;
        if (FAILED(task->get_Definition(&definition)) || !definition)
        {
            return result;
        }

        Microsoft::WRL::ComPtr<IActionCollection> actionCollection;
        if (FAILED(definition->get_Actions(&actionCollection)) || !actionCollection)
        {
            return result;
        }

        LONG count = 0;
        if (FAILED(actionCollection->get_Count(&count)))
        {
            return result;
        }

        for (LONG i = 1; i <= count; ++i)
        {
            Microsoft::WRL::ComPtr<IAction> action;
            if (FAILED(actionCollection->get_Item(i, &action)) || !action)
            {
                continue;
            }

            TASK_ACTION_TYPE type = TASK_ACTION_EXEC;
            if (FAILED(action->get_Type(&type)))
            {
                continue;
            }

            if (type != TASK_ACTION_EXEC)
            {
                // Only care about executable actions
                continue;
            }

            Microsoft::WRL::ComPtr<IExecAction> execAction;
            if (FAILED(action.As(&execAction)) || !execAction)
            {
                continue;
            }

            BSTR path = NULL;
            BSTR args = NULL;
            if (FAILED(execAction->get_Path(&path)))
            {
                if (path) SysFreeString(path);
                if (args) SysFreeString(args);
                continue;
            }
            execAction->get_Arguments(&args);

            std::wstring actionDesc = path ? path : L"";
            if (args && wcslen(args) > 0)
            {
                actionDesc += L" ";
                actionDesc += args;
            }

            if (path)
            {
                SysFreeString(path);
            }
            if (args)
            {
                SysFreeString(args);
            }

            result.push_back(actionDesc);
        }

        return result;
    }

   private:
    Microsoft::WRL::ComPtr<IRegisteredTask> task;
};

class TaskScheduler
{
   public:
    static std::vector<Task> GetTasks(const std::wstring &taskFolder = L"\\");
};

}  // namespace zzj
