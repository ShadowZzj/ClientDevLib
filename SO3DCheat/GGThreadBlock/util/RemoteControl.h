#pragma once
#include <Windows.h>
#include <functional>
#include <json.hpp>
#include <string>

namespace GGTB::RemoteControl
{
// Result returned by a command handler. ok=false 触发 broker 侧 ack {ok:false, detail}.
struct CmdResult
{
    bool        ok = false;
    std::string detail;
};

// Handler 收到反序列化后的 args 子对象（command.args），同步返回结果。
// 在 IO 线程上被调用 — handler 内部的引擎调用最好走 SEH 包装（参考
// CallSendMoneyMailSEH）以防 SO3D 的 packet helper 在解包不齐时撞针。
using CommandHandler = std::function<CmdResult(const nlohmann::json &)>;

// 启动 IO 线程，主动连 \\.\pipe\GGTB_BROKER（环境变量 GGTB_BROKER_PIPE 可覆盖）。
// 连不上就 3s 重试；broker 重启自动重连。从 HackThread 调，不要从 DllMain。
void Install();

// 通知 IO 线程退出 + best-effort 发 bye 帧。幂等。在 NetLog::Uninstall 之前调，
// 这样 bye 帧的 send 还能走 NetLog hook 之外的正常路径。
void Uninstall();

// 注册一个命令。重名后注册的覆盖前者。线程安全，但建议在 Install() 前/后立即注册。
void RegisterCommandHandler(const std::string &action, CommandHandler handler);

// 诊断用：当前是否有活动的 broker 连接。
bool IsConnected();

// 向 broker 发送任意 JSON 帧。用于 NetLog 等模块推送事件（如公屏聊天）。
// 线程安全。连接断开时静默丢弃。
bool EmitFrame(const nlohmann::json &frame);
} // namespace GGTB::RemoteControl
