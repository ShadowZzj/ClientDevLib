#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace zzj
{
// Owned service child. No shell, no pipe IPC, no console window. Destruction reaps the child.
class ChildProcess
{
  public:
    ChildProcess(const std::string &executable, const std::vector<std::string> &arguments);
    ~ChildProcess();
    ChildProcess(const ChildProcess &) = delete;
    ChildProcess &operator=(const ChildProcess &) = delete;
    uint32_t Id() const;
    bool Alive();
    bool WaitFor(std::chrono::milliseconds timeout);
    void Terminate();
    static uint32_t CurrentId();
  private:
    struct State;
    std::unique_ptr<State> state_;
};
// Atomically create a NEW owner-only directory, including inherited file permissions on Windows.
void CreatePrivateDirectory(const std::filesystem::path &path);
}
