#pragma once
#include "CommandExecution.h"
#include <algorithm>
#include <filesystem>

namespace zzj::command_detail
{
inline void Validate(const CommandOptions &o)
{
    auto valid = [](const std::string &s) { return s.find('\0') == std::string::npos; };
    if (!valid(o.executable) || !valid(o.command) || !valid(o.workingDirectory) || !valid(o.shell) ||
        !std::all_of(o.arguments.begin(), o.arguments.end(), valid) ||
        o.timeout.count() < 1 || o.maxOutputBytes < 1 || o.maxOutputBytes > 1024 * 1024 ||
        (o.kind == CommandKind::Program && !std::filesystem::u8path(o.executable).is_absolute()) ||
        (o.kind != CommandKind::Program && o.command.empty()) ||
        (!o.workingDirectory.empty() && !std::filesystem::u8path(o.workingDirectory).is_absolute()))
        throw CommandExecutionError("INVALID_ARGUMENT", "Use absolute paths, nonempty commands and bounded output");
}
inline void Append(std::string &target, const char *data, size_t size, size_t limit, bool &truncated)
{
    const auto kept = std::min(size, limit - target.size());
    target.append(data, kept);
    truncated = truncated || kept < size;
}
inline const char *Name(CommandIdentity identity)
{
    switch (identity)
    {
    case CommandIdentity::Service: return "service";
    case CommandIdentity::CurrentUser: return "current_user";
    case CommandIdentity::CurrentUserAdmin: return "current_user_admin";
    }
    throw CommandExecutionError("INVALID_ARGUMENT", "Unknown execution identity");
}
}
