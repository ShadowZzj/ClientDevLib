#pragma once
#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace zzj
{
enum class CommandIdentity { Service, CurrentUser, CurrentUserAdmin };
enum class CommandKind { Program, Shell, PowerShell };
struct CommandOptions
{
    CommandKind kind = CommandKind::Program;
    CommandIdentity identity = CommandIdentity::Service;
    std::string executable, command, shell, workingDirectory;
    std::vector<std::string> arguments;
    std::chrono::milliseconds timeout{10000};
    size_t maxOutputBytes = 4096; // Per stream; continue draining after truncation.
    std::function<bool()> cancelled;
};
struct CommandExecutionError : std::runtime_error
{
    std::string code;
    CommandExecutionError(std::string code, const std::string &message)
        : std::runtime_error(message), code(std::move(code)) {}
};
struct CommandEnvironment
{
    std::string identity, user, session, reason;
    bool available = false, elevated = false;
};

class CommandHelper
{
  public:
    struct CommandResult
    {
        int exitCode = -1;
        std::string stdoutStr, stderrStr;
        bool timedOut = false, cancelled = false, stdoutTruncated = false, stderrTruncated = false;
        unsigned long processId = 0;
        std::string user, session;
        bool elevated = false;
    };
    // Existing entrypoints retain their behavior.
    static CommandResult ExecuteCommand(const std::string &command);
    static CommandResult ExecuteCurrentUserCommand(const std::string &command);
    static CommandResult ExecuteRootCommand(const std::string &command);

    // Headless, bounded execution. Never prompts for credentials or changes identity on failure.
    // Caller owns authorization and any UI confirmation. stdout/stderr are raw bytes.
    static CommandResult ExecuteBounded(const CommandOptions &options);
    static std::vector<CommandEnvironment> GetExecutionEnvironments();
    static std::string PowerShellPath(); // Empty when PowerShell is not installed.
};
}
