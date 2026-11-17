#pragma once
#include "Process.h"
#include <memory>
#include <vector>
namespace zzj
{
class ThreadV2
{
    friend class ProcessV2;

  public:
    ThreadV2()
    {
    }

    ThreadV2(int tid);
    static std::vector<ThreadV2> EnumAllThreads();

  private:
    int tid = -1;
    std::shared_ptr<ProcessV2> processInfo;
};
}; // namespace zzj