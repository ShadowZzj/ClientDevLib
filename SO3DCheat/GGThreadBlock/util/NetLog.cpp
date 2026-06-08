#define WIN32_LEAN_AND_MEAN
#include "NetLog.h"
#include "../entity/CLocalPlayer.h"
#include "DisconnectWatchdog.h"
#include "PatternResolver.h"
#include "RemoteControl.h"
#include "Stage1Trigger.h"
#include "UserConfig.h"

#include <Detours/build/include/detours.h>
#include <json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/pattern_formatter.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <share.h>
#include <string>

#pragma comment(lib, "ws2_32.lib")

// Net__SendPacket_Plaintext @ 0xB1CA60 and Net__RawRecv @ 0xB1C750 are both
// __thiscall. MSVC forbids declaring free-function pointers as __thiscall, but
// x86 thiscall and __fastcall share the same ECX/stack layout — __fastcall
// additionally claims EDX, which the thiscall callsite doesn't set, so the
// extra EDX slot is a free scratch we simply ignore. Stack argument order and
// callee-clean stack cleanup both match. This is the canonical Detours trick
// for hooking __thiscall on x86 MSVC.
//
// TWO-STAGE INSTALL — the exe is packed/encrypted; .text at 0xB1CA60 is a
// zero page at DllMain time and only gets filled in after the unpacker stub
// finishes. If we Detour it at DllMain time, Detours happily writes an E9
// into that zero page, then the unpacker overwrites our jmp with the real
// function body a second later. (Observed: pre-attach bytes = 00 00 00 00,
// post-attach = E9 xx xx xx xx, t+1s = 55 8B EC 83 EC 18 — the real prolog.)
//
// Fix: we subscribe to GGTB::Stage1Trigger, which owns a single
// DeleteFileA("error.txt") detour shared by every stage-2 module. Its callback
// fires once WinMain starts, at which point the unpacker is done and our
// AttachNetHooks can safely Detour the real function bodies.

namespace GGTB::NetLog
{
namespace fs = std::filesystem;

namespace
{
using fnSendPacketPT = int(__fastcall *)(void *ecx, void *edx, void *pkt, int len);
using fnRawRecv     = int(__fastcall *)(void *ecx, void *edx, int timeoutSec, int timeoutUsec);
using fnWsSend      = int(WSAAPI *)(SOCKET s, const char *buf, int len, int flags);
using fnWsRecv      = int(WSAAPI *)(SOCKET s, char *buf, int len, int flags);

fnSendPacketPT g_oSendPacketPT = nullptr;
fnRawRecv      g_oRawRecv      = nullptr;
fnWsSend       g_oWsSend       = nullptr;
fnWsRecv       g_oWsRecv       = nullptr;

std::atomic<bool>   s_attached{false};
std::atomic<bool>   s_wireSendAttached{false};
std::atomic<bool>   s_wireRecvAttached{false};
std::atomic<bool>   s_filesOpen{false};
std::mutex          s_loggerMutex;
std::wstring        s_openNetDir;

// Last time the recv hook saw a proto-521056 (GC_SKILL_CAST_RESULT) packet.
// FireFullPower 稳健模式 reads this to gate per-cast pacing — see NetLog.h
// for the full rationale. uint32_t is fine on x86 even without atomic ops
// (single 32-bit aligned word), but use atomic for the cross-thread visibility
// guarantee (worker thread reads, recv hook on game's net thread writes).
constexpr uint32_t  kProtoSkillCastResult = 521056; // 0x7F360
constexpr uint32_t  kProtoGcPublicChat    = 511004; // 0x7CC1C
constexpr uint32_t  kProtoShopSellMoneyUpdate = 521054; // 0x7F35E
constexpr uint32_t  kProtoNpcDialogSelect = 411026; // 0x645D2 — CG_NPC_DIALOG_SELECT
std::atomic<DWORD>  s_lastSkillResultMs{0};
std::atomic<uint32_t> s_lastSkillResultId{0};

// Auto-trade observers — AutoTradeModule's worker polls these (recv hook on
// the game's net thread writes, worker reads). 两条都在交易 handler 注册表
// Net_RegisterRecvHandlers_07D0xx(0x8a0a90)里注册,但 proto 不是统一
// "base+subIndex":注册时 sub_875550(literalProto) 把字面 proto 映射成 slot,
// 字面值才是线上 proto,且 base 因 handler 而异 —— 请求走 0x07CC00 段、
// 锁定走 0x07D000 段。早先误把所有 handler 当 base=0x07D000+subIndex,导致
// 请求被错算成 0x07D062(从不上线),真实请求 proto 是 0x07CC62(subIndex 98,
// base 0x07CC00)。recvlog 实测:shadowpope 交易 shadowdance 时只出现一条
// 0x07CC62 len=12 body=requesterId,与 GC_OnTradeRequest_07CC62(sub_8A33B0)
// 静态语义一致(读 body[0]→id,忙时 412029 DECLINE,否则 state=0 弹窗,点同意
// 发 412028)。peer-lock 的字面值恰好等于 0x07D050,故该常量无需改。
constexpr uint32_t  kProtoGcTradeRequest  = 0x07CC62; // 511074 — 收到交易请求; body[0]=requesterEntityId
constexpr uint32_t  kProtoGcTradePeerLock = 0x07D050; // 512080 — 对方锁定了自己那一侧
std::atomic<DWORD>    s_lastTradeRequestMs{0};
std::atomic<uint32_t> s_lastTradeRequesterId{0};
std::atomic<DWORD>    s_lastTradePeerLockMs{0};

// One-shot diagnostic flags so we can confirm the code path actually fired
// without spamming the log once per packet.
std::atomic<bool>   s_diagSendFired{false};
std::atomic<bool>   s_diagRecvFired{false};
std::atomic<bool>   s_diagWireSendFired{false};
std::atomic<bool>   s_diagWireRecvFired{false};
std::atomic<bool>   s_diagReadySeen{false};

// Dedicated loggers — one text file per direction under
// GGConfig/<char>/net/{sendlog,recvlog}. spdlog's own mutex inside each sink
// handles serialisation; no more hand-rolled fwrite + mutex pair.
std::shared_ptr<spdlog::logger> s_sendLogger;
std::shared_ptr<spdlog::logger> s_recvLogger;

constexpr uint32_t  kMaxFramePayload = 65536; // sanity cap; real packets are tiny

// Same WideFileSink pattern as UserConfig::SwapLoggerTo — opens via _wfsopen so
// CJK character-name directories are handled correctly, and _SH_DENYNO so
// tail-style viewers can follow the file while we append.
//
// Extended with size-based rotation: when the active file would exceed
// max_size_ bytes, rename sendlog -> sendlog.1 (pushing .1 -> .2 etc, dropping
// the oldest past max_files_) and reopen a fresh sendlog. The hard cap on
// disk usage per direction is (max_files_ + 1) * max_size_.
class WideFileSink final : public spdlog::sinks::base_sink<std::mutex>
{
  public:
    WideFileSink(std::wstring path, uint64_t max_size, size_t max_files)
        : path_(std::move(path)), max_size_(max_size), max_files_(max_files)
    {
        fp_ = ::_wfsopen(path_.c_str(), L"ab", _SH_DENYNO);
        if (fp_)
        {
            // Pick up current size so restarts don't immediately blow past cap.
            ::_fseeki64(fp_, 0, SEEK_END);
            current_size_ = static_cast<uint64_t>(::_ftelli64(fp_));
        }
    }
    ~WideFileSink() override { if (fp_) std::fclose(fp_); }
    bool ok() const { return fp_ != nullptr; }

  protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        if (!fp_) return;
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);

        if (max_size_ > 0 &&
            current_size_ + formatted.size() > max_size_ &&
            current_size_ > 0)
        {
            rotate_();
        }
        if (!fp_) return; // rotate_ may have failed to reopen

        std::fwrite(formatted.data(), 1, formatted.size(), fp_);
        current_size_ += formatted.size();
    }
    void flush_() override { if (fp_) std::fflush(fp_); }

  private:
    std::wstring numberedName_(size_t i) const
    {
        return i == 0 ? path_ : path_ + L"." + std::to_wstring(i);
    }

    void rotate_()
    {
        if (fp_) { std::fclose(fp_); fp_ = nullptr; }

        // Drop the oldest backup, then shift each existing backup up one slot,
        // finally rename the active file into slot 1. _wrename fails silently
        // if the source doesn't exist yet — that's fine, early rotations will
        // only have a few slots populated.
        ::_wremove(numberedName_(max_files_).c_str());
        for (size_t i = max_files_; i > 1; --i)
            ::_wrename(numberedName_(i - 1).c_str(), numberedName_(i).c_str());
        if (max_files_ >= 1)
            ::_wrename(path_.c_str(), numberedName_(1).c_str());

        fp_ = ::_wfsopen(path_.c_str(), L"wb", _SH_DENYNO);
        current_size_ = 0;
    }

    std::wstring path_;
    uint64_t     max_size_     = 0;
    size_t       max_files_    = 0;
    uint64_t     current_size_ = 0;
    FILE        *fp_           = nullptr;
};

// Hard cap on per-direction (send OR recv) disk usage = 50 MB.
// Layout: 1 active file + 1 rotated backup, each 25 MB.
// At 50 MB total per direction, both directions together stay under 100 MB.
constexpr uint64_t kNetLogMaxBytes = 25ull * 1024 * 1024;
constexpr size_t   kNetLogMaxFiles = 1;

// ---- SEH-only helpers (no C++ objects, C2712-safe) ----

bool SafeMemCopy(void *dst, const void *src, uint32_t len)
{
    __try
    {
        std::memcpy(dst, src, len);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SafeReadDwordAt(const void *addr, uint32_t &out)
{
    __try
    {
        out = *reinterpret_cast<const volatile uint32_t *>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- Peer lookup ----
// Called per packet. getpeername is a local syscall (microseconds) and always
// reflects the current binding, so we skip caching — that avoids a stale-fd
// hazard on reconnect where the OS may reuse the same SOCKET number for a
// different peer.

struct PeerAddr { uint32_t ip; uint16_t port; }; // ip = network-order bytes, port = host order

PeerAddr LookupPeer(SOCKET s)
{
    PeerAddr pa{0, 0};
    if (s == 0 || s == INVALID_SOCKET) return pa;

    sockaddr_in sa{};
    int saLen = sizeof(sa);
    if (::getpeername(s, reinterpret_cast<sockaddr *>(&sa), &saLen) == 0 &&
        sa.sin_family == AF_INET)
    {
        pa.ip   = sa.sin_addr.s_addr;
        pa.port = ntohs(sa.sin_port);
    }
    return pa;
}

std::string PeerString(PeerAddr p)
{
    if (p.ip == 0 && p.port == 0) return "?:?";
    const uint8_t *b = reinterpret_cast<const uint8_t *>(&p.ip);
    return fmt::format("{}.{}.{}.{}:{}", b[0], b[1], b[2], b[3], p.port);
}

std::string HexDump(const uint8_t *data, uint32_t len)
{
    // Space-separated uppercase bytes: "AA BB CC ...". No line wrap — one
    // frame per log line so a grep-friendly view is preserved.
    std::string out;
    out.reserve(static_cast<size_t>(len) * 3);
    for (uint32_t i = 0; i < len; ++i)
    {
        if (i) out.push_back(' ');
        const char *hx = "0123456789ABCDEF";
        out.push_back(hx[(data[i] >> 4) & 0xF]);
        out.push_back(hx[data[i] & 0xF]);
    }
    return out;
}

// Decode known SEND protocols field-by-field. Wire layout for every CG_ packet
// is {u32 totalLen, u32 protocolId, body...} — body starts at offset 8.
//
// Returns a "NAME field=X field=Y..." string for protos with confirmed layouts.
// Returns just "NAME" for protos we recognize by ID but whose body layout we
// haven't fully RE'd. Returns empty string for unknown protos.
//
// The caller (WriteSendLine) always logs the raw hex too — decoder output is a
// human-readable PREFIX so a wrong decode never loses the original packet bytes.
std::string DecodeSendPayload(uint32_t proto, const uint8_t *buf, uint32_t len)
{
    auto rd32 = [&](uint32_t off, uint32_t &out) -> bool {
        if (off + 4 > len) return false;
        std::memcpy(&out, buf + off, 4);
        return true;
    };

    uint32_t a = 0, b = 0, c = 0;

    switch (proto)
    {
    case 411013:
        return "CG_HEARTBEAT";
    case 411000:
        // CG_MOVE — body is a route-state blob (variable length). Only label.
        return "CG_MOVE";
    case 411011:
        // CG_PICK_ITEM — routed through Net__SendDialogSelect (4-arg builder),
        // so body is 3 DWORDs: a3=dropId, a4=validatedItemId, a5=0.
        rd32(8, a); rd32(12, b); rd32(16, c);
        return fmt::format("CG_PICK_ITEM dropId={} itemId={} sub={}", a, b, c);
    case 411012:
        // CG_ITEM_DROP — routed through Net__SendTriple (3-arg builder),
        // so body is 2 DWORDs: a3=bagId+13 (wire-encoded slot), a4=count.
        rd32(8, a); rd32(12, b);
        return fmt::format("CG_ITEM_DROP wireBag={} count={}", a, b);
    case 411022:
        return "CG_SKILL_22"; // exact body layout unconfirmed
    case 411024:
        return "CG_SKILL_24"; // exact body layout unconfirmed
    case 411026:
        // CG_NPC_DIALOG_SELECT — Net__SendDialogSelect(buf, 411026, opt, npc, 1).
        // Body is 3 DWORDs in this order: dialogOptionIndex, npcInteractTargetId,
        // subAction(=1). Verified 2026-05-09 by selecting the same NPC twice on
        // the live server: DWORD3 (offset 8) varied, DWORD4 (offset 12) was
        // constant — so the constant one is the NPC. See OnNpcDialogConfirm
        // @ 0x8BBEF0 — a3 is the packet-deserialised v10 (option), a4 is the
        // cached g_NpcInteractTargetId at *(dword_ED347C+1016).
        rd32(8, a); rd32(12, b); rd32(16, c);
        return fmt::format("CG_NPC_DIALOG_SELECT opt={} npc={} sub={}", a, b, c);
    case 411723:
        return "CG_UI_REFRESH"; // 0x647CB
    default:
        return {};
    }
}

std::shared_ptr<spdlog::logger> MakePacketLogger(const char *loggerName,
                                                 const fs::path &netDir,
                                                 const wchar_t *fileName)
{
    auto sink = std::make_shared<WideFileSink>(
        (netDir / fileName).wstring(), kNetLogMaxBytes, kNetLogMaxFiles);
    if (!sink->ok())
    {
        spdlog::error("GGTB::NetLog: failed to open {} sink at {}",
                      loggerName, (netDir / fileName).string());
        return nullptr;
    }

    spdlog::drop(loggerName);
    auto logger = std::make_shared<spdlog::logger>(loggerName, sink);
    logger->set_pattern("%Y-%m-%d %H:%M:%S.%e | %v");
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    return logger;
}

void OpenFilesIfReady()
{
    auto netDir = GGTB::UserConfig::UserDir() / "net";
    auto wanted = netDir.wstring();

    {
        std::lock_guard<std::mutex> lk(s_loggerMutex);
        if (s_filesOpen.load() && s_openNetDir == wanted)
            return;

        std::error_code ec;
        fs::create_directories(netDir, ec);
        if (ec)
        {
            spdlog::error("GGTB::NetLog: create_directories({}) failed: {}",
                          netDir.string(), ec.message());
            return;
        }

        if (s_sendLogger) { s_sendLogger->flush(); s_sendLogger.reset(); }
        if (s_recvLogger) { s_recvLogger->flush(); s_recvLogger.reset(); }
        spdlog::drop("ggtb_net_send");
        spdlog::drop("ggtb_net_recv");

        auto sendLogger = MakePacketLogger("ggtb_net_send", netDir, L"sendlog");
        auto recvLogger = MakePacketLogger("ggtb_net_recv", netDir, L"recvlog");
        if (!sendLogger || !recvLogger)
        {
            s_sendLogger.reset();
            s_recvLogger.reset();
            s_filesOpen.store(false);
            spdlog::error("GGTB::NetLog: one or both net loggers failed to open under {}",
                          netDir.string());
            return;
        }

        s_sendLogger = std::move(sendLogger);
        s_recvLogger = std::move(recvLogger);
        s_openNetDir = std::move(wanted);
        s_filesOpen.store(true);
        spdlog::info("GGTB::NetLog: send/recv logs open at {}", netDir.string());
    }

    if (GGTB::UserConfig::IsReady() && !s_diagReadySeen.exchange(true))
        spdlog::info("GGTB::NetLog: UserConfig is ready, switched net logs to {}",
                     netDir.string());
}

void WriteLine(spdlog::logger *logger, const char *dirTag, PeerAddr peer,
               const void *payload, uint32_t len)
{
    if (!logger || !payload || len == 0 || len > kMaxFramePayload) return;

    // Copy under SEH so a torn-page / stale-buffer read can't crash us, then
    // format the hex outside the __try (hex formatting uses std::string).
    uint8_t  stackBuf[1024];
    uint8_t *heapBuf = nullptr;
    uint8_t *buf     = stackBuf;
    if (len > sizeof(stackBuf))
    {
        heapBuf = static_cast<uint8_t *>(std::malloc(len));
        if (!heapBuf) return;
        buf = heapBuf;
    }

    if (SafeMemCopy(buf, payload, len))
    {
        logger->info("[{}] {} len={} hex: {}",
                     dirTag, PeerString(peer), len, HexDump(buf, len));
    }

    if (heapBuf) std::free(heapBuf);
}

void WriteSendLine(spdlog::logger *logger, PeerAddr peer,
                   const void *payload, uint32_t len)
{
    if (!logger || !payload || len == 0 || len > kMaxFramePayload) return;

    uint8_t  stackBuf[1024];
    uint8_t *heapBuf = nullptr;
    uint8_t *buf     = stackBuf;
    if (len > sizeof(stackBuf))
    {
        heapBuf = static_cast<uint8_t *>(std::malloc(len));
        if (!heapBuf) return;
        buf = heapBuf;
    }

    if (SafeMemCopy(buf, payload, len))
    {
        // Header layout: {u32 totalLen, u32 protocolId, body...}. Read proto
        // from offset 4 if the frame is long enough; otherwise emit hex only.
        uint32_t proto = 0;
        if (len >= 8)
            std::memcpy(&proto, buf + 4, 4);

        std::string decoded = (len >= 8) ? DecodeSendPayload(proto, buf, len)
                                          : std::string{};

        if (!decoded.empty())
        {
            // Known proto: print decoded fields PLUS hex for verification.
            logger->info("[SEND] {} len={} proto={}/{} hex: {}",
                         PeerString(peer), len, proto, decoded,
                         HexDump(buf, len));
        }
        else
        {
            logger->info("[SEND] {} len={} proto={} hex: {}",
                         PeerString(peer), len, proto, HexDump(buf, len));
        }
    }

    if (heapBuf) std::free(heapBuf);
}

void WriteRecvLineWithCurrentLogger(PeerAddr peer, const void *payload, uint32_t len)
{
    OpenFilesIfReady();
    std::shared_ptr<spdlog::logger> logger;
    {
        std::lock_guard<std::mutex> lk(s_loggerMutex);
        logger = s_recvLogger;
    }
    if (s_filesOpen.load() && logger)
        WriteLine(logger.get(), "RECV", peer, payload, len);
}

void WriteWireSendLine(SOCKET s, const void *payload, uint32_t len)
{
    OpenFilesIfReady();
    std::shared_ptr<spdlog::logger> logger;
    {
        std::lock_guard<std::mutex> lk(s_loggerMutex);
        logger = s_sendLogger;
    }
    if (s_filesOpen.load() && logger)
        WriteLine(logger.get(), "WIRE_SEND", LookupPeer(s), payload, len);
}

void WriteWireRecvLine(SOCKET s, const void *payload, uint32_t len)
{
    OpenFilesIfReady();
    std::shared_ptr<spdlog::logger> logger;
    {
        std::lock_guard<std::mutex> lk(s_loggerMutex);
        logger = s_recvLogger;
    }
    if (s_filesOpen.load() && logger)
        WriteLine(logger.get(), "WIRE_RECV", LookupPeer(s), payload, len);
}

// 主角色发出 CG_NPC_DIALOG_SELECT (411026) 时,把 npc/opt/sub 推给 broker,
// 让「同步」功能能把同一个对话动作 fan-out 给副角色。body 三个 DWORD:
//   +8 dialogOptionIndex, +12 npcInteractTargetId, +16 subAction
// 这里只读 POD(SafeReadDwordAt 各自 SEH),JSON 构造放在 __try 之外(C2712)。
static void EmitDialogSelectFrame(uint32_t npc, uint32_t opt, uint32_t sub)
{
    nlohmann::json frame = {
        {"type", "dialogSelect"},
        {"pid", GetCurrentProcessId()},
        {"npc", npc},
        {"opt", opt},
        {"sub", sub},
    };
    const std::string name = GGTB::UserConfig::CurrentName();
    if (!name.empty())
        frame["characterName"] = name;
    GGTB::RemoteControl::EmitFrame(frame);
}

int __fastcall HookSendPacketPT(void *ecx, void *edx, void *pkt, int len)
{
    if (!s_diagSendFired.exchange(true))
        spdlog::info("GGTB::NetLog: HookSendPacketPT first hit (ready={}, pkt={}, len={})",
                     GGTB::UserConfig::IsReady(),
                     static_cast<void *>(pkt), len);

    // Log at ENTRY, before the optional XOR obfuscation (sub_5CA910) rewrites
    // the body in place via memmove(Src+8, Src, Size). pkt is
    // {u32 totalLen, u32 protocolId, body...}, plaintext at this point.
    OpenFilesIfReady();
    std::shared_ptr<spdlog::logger> logger;
    {
        std::lock_guard<std::mutex> lk(s_loggerMutex);
        logger = s_sendLogger;
    }
    if (s_filesOpen.load() && logger && pkt && len > 0)
    {
        uint32_t hdrLen = 0;
        SafeReadDwordAt(pkt, hdrLen);

        // Prefer the header length when it's consistent with `len` — the
        // caller argument can trail unused bytes; the header field is what
        // the game itself considers the packet payload length.
        uint32_t cap = static_cast<uint32_t>(len);
        if (hdrLen >= 8 && hdrLen <= cap) cap = hdrLen;

        uint32_t sockFd = 0;
        SafeReadDwordAt(static_cast<uint8_t *>(ecx) + 0x10, sockFd);
        PeerAddr peer = LookupPeer(static_cast<SOCKET>(sockFd));

        WriteSendLine(logger.get(), peer, pkt, cap);
    }

    // 同步功能:不依赖日志文件,只要连着 broker,主角色每发一次 411026 就上报。
    // body 三个 DWORD:opt@+8, npc@+12, sub@+16。各自 SEH 读,POD only。
    if (pkt && len >= 20)
    {
        uint32_t proto = 0;
        SafeReadDwordAt(static_cast<uint8_t *>(pkt) + 4, proto);
        if (proto == kProtoNpcDialogSelect)
        {
            uint32_t opt = 0, npc = 0, sub = 0;
            SafeReadDwordAt(static_cast<uint8_t *>(pkt) + 8, opt);
            SafeReadDwordAt(static_cast<uint8_t *>(pkt) + 12, npc);
            SafeReadDwordAt(static_cast<uint8_t *>(pkt) + 16, sub);
            EmitDialogSelectFrame(npc, opt, sub);
        }
    }
    return g_oSendPacketPT(ecx, edx, pkt, len);
}

int WSAAPI HookWsSend(SOCKET s, const char *buf, int len, int flags)
{
    if (!s_diagWireSendFired.exchange(true))
        spdlog::info("GGTB::NetLog: HookWsSend first hit (socket={} buf={} len={} flags={})",
                     static_cast<uintptr_t>(s), static_cast<const void *>(buf), len, flags);

    if (buf && len > 0)
        WriteWireSendLine(s, buf, static_cast<uint32_t>(len));
    return g_oWsSend(s, buf, len, flags);
}

int WSAAPI HookWsRecv(SOCKET s, char *buf, int len, int flags)
{
    if (!s_diagWireRecvFired.exchange(true))
        spdlog::info("GGTB::NetLog: HookWsRecv first hit (socket={} buf={} len={} flags={})",
                     static_cast<uintptr_t>(s), static_cast<void *>(buf), len, flags);

    int ret = g_oWsRecv(s, buf, len, flags);
    if (ret > 0 && buf)
        WriteWireRecvLine(s, buf, static_cast<uint32_t>(ret));
    return ret;
}

struct ChatHitPod { uint32_t bodyOffset; uint32_t bodyLen; };
struct MoneyUpdatePod { uint32_t proto; uint32_t itemSlot; uint64_t money; };

// Big5 (CP950) → UTF-8。游戏内字符串都是 Big5 raw bytes;转完才能塞 JSON。
// 失败 (空串或转换失败) 退回原始字节,显示乱码也比丢消息好。
static std::string Big5ToUtf8Local(const char *src, size_t len)
{
    if (!src || !len) return {};
    int wlen = MultiByteToWideChar(950, 0, src, static_cast<int>(len), nullptr, 0);
    if (wlen <= 0) return std::string(src, len);
    std::wstring w(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(950, 0, src, static_cast<int>(len), w.data(), wlen);
    int u8len = WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return std::string(src, len);
    std::string u(static_cast<size_t>(u8len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, u.data(), u8len, nullptr, nullptr);
    return u;
}

// 单独函数:含 std::string / nlohmann::json (有析构),不能跟 __try 共存。
// 调用方 (HookRawRecv) 在 __try 里只填 POD 数组,出来再走这条解析+emit。
//
// 511004 (GC_PUBLIC_CHAT) body 布局 (sub_898000 验证):
//   body+0:  u32 senderUserId       — Net__ReadU32_LE(body, Number)
//   body+4:  u32 channelId/regionId — Net__ReadU32_LE(body+4, &v7)
//   body+8:  char msgText[totalLen-16] — strncpy 进 ED0080,长度 = totalLen - 16
// 包里没有 sender 名字,引擎是用 EntityManager__FindUserById 反查 +0x1AC0 字段拿。
// 我们走 around-player 单链表(稳),AOI 之外的 sender 显示为 User#<id>。
static void EmitChatFrames(uintptr_t baseAddr, const ChatHitPod *hits, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const char *body = reinterpret_cast<const char *>(baseAddr + hits[i].bodyOffset);
        uint32_t bodyLen = hits[i].bodyLen;
        if (bodyLen < 9) continue; // 8 (2 DWORDs) + at least 1 char

        uint32_t senderUserId = 0, channelId = 0;
        std::memcpy(&senderUserId, body, 4);
        std::memcpy(&channelId,    body + 4, 4);

        const char *msgRaw    = body + 8;
        uint32_t    msgRawLen = bodyLen - 8;
        // 末尾可能有 NUL/对齐填充,trim
        const char *msgEnd = static_cast<const char *>(std::memchr(msgRaw, '\0', msgRawLen));
        size_t      effLen = msgEnd ? static_cast<size_t>(msgEnd - msgRaw) : msgRawLen;
        if (effLen == 0) continue;

        std::string msgUtf8 = Big5ToUtf8Local(msgRaw, effLen);
        GGTB::NearbyPlayer senderInfo;
        bool haveSenderInfo = GGTB::LookupAroundPlayerById(senderUserId, senderInfo);
        std::string sender  = haveSenderInfo ? senderInfo.name : std::string{};
        if (sender.empty())
            sender = "User#" + std::to_string(senderUserId);

        nlohmann::json frame = {
            {"type", "chat"},
            {"pid", GetCurrentProcessId()},
            {"sender", sender},
            {"senderUserId", senderUserId},
            {"channelId", channelId},
            {"message", msgUtf8},
        };
        if (haveSenderInfo)
        {
            frame["senderProfession"] = senderInfo.profession;
            frame["senderProfessionName"] = senderInfo.professionName;
        }
        GGTB::RemoteControl::EmitFrame(frame);
    }
}

static void EmitMoneyUpdateFrames(const MoneyUpdatePod *hits, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const uint64_t money = hits[i].money;
        if (money == 0 || money > 100000000000ull) continue;

        nlohmann::json frame = {
            {"type", "moneyUpdate"},
            {"pid", GetCurrentProcessId()},
            {"money", money},
            {"proto", hits[i].proto},
            {"itemSlot", hits[i].itemSlot},
            {"source", "recv-shop-sell"},
        };
        const std::string name = GGTB::UserConfig::CurrentName();
        if (!name.empty())
            frame["characterName"] = name;
        GGTB::RemoteControl::EmitFrame(frame);
    }
}

int __fastcall HookRawRecv(void *ecx, void *edx, int tSec, int tUsec)
{
    if (!s_diagRecvFired.exchange(true))
        spdlog::info("GGTB::NetLog: HookRawRecv first hit (ready={}, this={})",
                     GGTB::UserConfig::IsReady(), ecx);

    uint8_t *self = static_cast<uint8_t *>(ecx);
    uint32_t bufAddr = 0;
    uint32_t oldFill = 0;

    // CGameClient layout (from IDA):
    //   +0x10 = SOCKET fd           (uint32_t)
    //   +0x18 = buffer base pointer (uint8_t *)
    //   +0x24 = current fill level  (uint32_t)
    //   +0x08 = total capacity      (uint32_t)  — not needed here
    bool captured = SafeReadDwordAt(self + 0x18, bufAddr) &&
                    SafeReadDwordAt(self + 0x24, oldFill);

    int ret = g_oRawRecv(ecx, edx, tSec, tUsec);

    if (captured && bufAddr)
    {
        uint32_t newFill = oldFill;
        SafeReadDwordAt(self + 0x24, newFill);

        if (newFill > oldFill)
        {
            uint32_t sockFd = 0;
            SafeReadDwordAt(self + 0x10, sockFd);
            PeerAddr peer  = LookupPeer(static_cast<SOCKET>(sockFd));
            uint32_t bytes = newFill - oldFill;

            // Feed the watchdog even when log files aren't open yet — the
            // disconnect check shouldn't wait for UserConfig to be ready.
            GGTB::DisconnectWatchdog::OnRecv(peer.port, bytes);

            // Scan the freshly arrived chunk for proto-521056 (GC_SKILL_CAST_RESULT)
            // packets and remember the most recent. Done BEFORE log writing so
            // FireFullPower's gate sees the update with minimum latency. Walk is
            // SEH-wrapped because bufAddr+i can race with the next recv if the
            // game pumps two recvs back-to-back on a different thread (rare).
            // Sub_pkt header layout is consistently {u32 totalLen, u32 proto, body...}.
            //
            // Chat/money capture: store POD only inside __try, build std::string
            // + JSON after the SEH block (MSVC C2712: __try can't share a
            // function with unwindable locals).
            ChatHitPod chatHits[8] = {};
            int        chatHitCount = 0;
            MoneyUpdatePod moneyHits[8] = {};
            int            moneyHitCount = 0;
            __try
            {
                const uint8_t *base  = reinterpret_cast<const uint8_t *>(bufAddr);
                const uint8_t *chunk = base;
                uint32_t       remaining = newFill;
                uint32_t       frameOffset = 0;
                while (remaining >= 8)
                {
                    uint32_t pktLen = 0, proto = 0;
                    std::memcpy(&pktLen, chunk, 4);
                    std::memcpy(&proto,  chunk + 4, 4);
                    if (pktLen < 8 || pktLen > remaining) break;

                    const bool newlyCompleted = frameOffset + pktLen > oldFill;

                    if (newlyCompleted && proto == kProtoSkillCastResult && pktLen >= 12)
                    {
                        uint32_t skillId = 0;
                        if (pktLen >= 12)
                            std::memcpy(&skillId, chunk + 8, 4);
                        s_lastSkillResultId.store(skillId, std::memory_order_relaxed);
                        s_lastSkillResultMs.store(GetTickCount(), std::memory_order_release);
                    }

                    // 收到交易请求:body[0] = 发起方 entity id,AutoTrade 回 412028 同意。
                    if (newlyCompleted && proto == kProtoGcTradeRequest && pktLen >= 12)
                    {
                        uint32_t requesterId = 0;
                        std::memcpy(&requesterId, chunk + 8, 4);
                        s_lastTradeRequesterId.store(requesterId, std::memory_order_relaxed);
                        s_lastTradeRequestMs.store(GetTickCount(), std::memory_order_release);
                    }

                    // 对方锁定了交易:AutoTrade 据此发 412033 锁定 + 412035 确认。
                    if (newlyCompleted && proto == kProtoGcTradePeerLock)
                    {
                        s_lastTradePeerLockMs.store(GetTickCount(), std::memory_order_release);
                    }

                    if (newlyCompleted &&
                        proto == kProtoGcPublicChat && pktLen > 8 &&
                        chatHitCount < static_cast<int>(_countof(chatHits)))
                    {
                        chatHits[chatHitCount].bodyOffset =
                            static_cast<uint32_t>((chunk + 8) - base);
                        chatHits[chatHitCount].bodyLen = pktLen - 8;
                        ++chatHitCount;
                    }

                    // 521054 / 0x7F35E is the shop sell result money-sync packet.
                    // Observed layout from recvlog:
                    //   u32 len=0x28, u32 proto=0x7F35E, u32 itemSlot,
                    //   u32 zero, u32 zero, u32 zero, u32 zero,
                    //   u64 currentMoney, u32 zero
                    // The sale gain is the positive delta between consecutive
                    // currentMoney values, computed in broker where character
                    // attribution and persistence already live.
                    if (newlyCompleted &&
                        proto == kProtoShopSellMoneyUpdate &&
                        pktLen >= 36 &&
                        moneyHitCount < static_cast<int>(_countof(moneyHits)))
                    {
                        uint32_t itemSlot = 0;
                        uint64_t money = 0;
                        std::memcpy(&itemSlot, chunk + 8, 4);
                        std::memcpy(&money, chunk + 28, 8);
                        moneyHits[moneyHitCount].proto = proto;
                        moneyHits[moneyHitCount].itemSlot = itemSlot;
                        moneyHits[moneyHitCount].money = money;
                        ++moneyHitCount;
                    }
                    frameOffset += pktLen;
                    chunk     += pktLen;
                    remaining -= pktLen;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                chatHitCount = 0;
                moneyHitCount = 0;
            }

            if (chatHitCount > 0)
                EmitChatFrames(bufAddr, chatHits, chatHitCount);
            if (moneyHitCount > 0)
                EmitMoneyUpdateFrames(moneyHits, moneyHitCount);

            WriteRecvLineWithCurrentLogger(
                peer, reinterpret_cast<void *>(bufAddr + oldFill), bytes);
        }
    }

    return ret;
}

} // anonymous

namespace
{
void AttachNetHooks();

auto DumpBytes = [](uintptr_t addr) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(addr);
    return fmt::format("{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
                       p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
};

void AttachNetHooks()
{
    if (s_attached.load()) return;

    uintptr_t addrSend = GGTB::PatternResolver::Get("NetSendPacketPlaintext");
    uintptr_t addrRecv = GGTB::PatternResolver::Get("NetRawRecv");
    HMODULE ws2 = GetModuleHandleA("ws2_32.dll");
    if (!ws2)
        ws2 = LoadLibraryA("ws2_32.dll");
    auto addrWsSend = reinterpret_cast<fnWsSend>(ws2 ? GetProcAddress(ws2, "send") : nullptr);
    auto addrWsRecv = reinterpret_cast<fnWsRecv>(ws2 ? GetProcAddress(ws2, "recv") : nullptr);
    if (!addrSend || !addrRecv)
    {
        spdlog::error("GGTB::NetLog: missing pattern addresses (send={:x} recv={:x})",
                      addrSend, addrRecv);
        return;
    }

    spdlog::info("GGTB::NetLog: stage-2 pre-attach send[{:x}]={} recv[{:x}]={}",
                 addrSend, DumpBytes(addrSend), addrRecv, DumpBytes(addrRecv));

    g_oSendPacketPT = reinterpret_cast<fnSendPacketPT>(addrSend);
    g_oRawRecv      = reinterpret_cast<fnRawRecv>(addrRecv);
    g_oWsSend       = addrWsSend;
    g_oWsRecv       = addrWsRecv;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG eSend = DetourAttach(reinterpret_cast<PVOID *>(&g_oSendPacketPT), HookSendPacketPT);
    LONG eRecv = DetourAttach(reinterpret_cast<PVOID *>(&g_oRawRecv),      HookRawRecv);
    LONG eWireSend = g_oWsSend ? DetourAttach(reinterpret_cast<PVOID *>(&g_oWsSend), HookWsSend)
                               : ERROR_PROC_NOT_FOUND;
    LONG eWireRecv = g_oWsRecv ? DetourAttach(reinterpret_cast<PVOID *>(&g_oWsRecv), HookWsRecv)
                               : ERROR_PROC_NOT_FOUND;
    LONG err   = DetourTransactionCommit();
    if (err != NO_ERROR || eSend != NO_ERROR || eRecv != NO_ERROR)
    {
        spdlog::error("GGTB::NetLog: stage-2 Detour failed: commit={} attachSend={} attachRecv={} attachWireSend={} attachWireRecv={}",
                      err, eSend, eRecv, eWireSend, eWireRecv);
        return;
    }
    s_wireSendAttached.store(err == NO_ERROR && eWireSend == NO_ERROR);
    s_wireRecvAttached.store(err == NO_ERROR && eWireRecv == NO_ERROR);
    if (!s_wireSendAttached.load() || !s_wireRecvAttached.load())
    {
        spdlog::warn("GGTB::NetLog: wire ws2_32 hooks not fully attached (send={} recv={})",
                     eWireSend, eWireRecv);
    }

    spdlog::info("GGTB::NetLog: stage-2 post-attach send[{:x}]={} recv[{:x}]={} (expect E9)",
                 addrSend, DumpBytes(addrSend), addrRecv, DumpBytes(addrRecv));

    s_attached.store(true);
    spdlog::info("GGTB::NetLog: hooks attached (send={:x}, recv={:x}, wsSend={}, wsRecv={})",
                 addrSend, addrRecv,
                 g_oWsSend ? fmt::format("{}", reinterpret_cast<void *>(g_oWsSend)) : std::string("null"),
                 g_oWsRecv ? fmt::format("{}", reinterpret_cast<void *>(g_oWsRecv)) : std::string("null"));
}
} // anonymous

void Install()
{
    if (s_attached.load()) return;

    // Stage1Trigger owns the DeleteFileA shim; we just subscribe. If the
    // trigger has already fired by the time we register (shouldn't happen
    // normally — Install is called during HackThread bootstrap, well before
    // WinMain), Stage1Trigger::Register runs the callback inline.
    GGTB::Stage1Trigger::Register(&AttachNetHooks);
    spdlog::info("GGTB::NetLog: subscribed to Stage1Trigger");
}

void Uninstall()
{
    auto closeLoggers = []() {
        std::lock_guard<std::mutex> lk(s_loggerMutex);
        if (s_sendLogger) { s_sendLogger->flush(); s_sendLogger.reset(); }
        if (s_recvLogger) { s_recvLogger->flush(); s_recvLogger.reset(); }
        spdlog::drop("ggtb_net_send");
        spdlog::drop("ggtb_net_recv");
        s_openNetDir.clear();
        s_filesOpen.store(false);
    };

    if (!s_attached.load())
    {
        closeLoggers();
        return;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID *>(&g_oSendPacketPT), HookSendPacketPT);
    DetourDetach(reinterpret_cast<PVOID *>(&g_oRawRecv),      HookRawRecv);
    if (s_wireSendAttached.load())
        DetourDetach(reinterpret_cast<PVOID *>(&g_oWsSend), HookWsSend);
    if (s_wireRecvAttached.load())
        DetourDetach(reinterpret_cast<PVOID *>(&g_oWsRecv), HookWsRecv);
    DetourTransactionCommit();
    s_attached.store(false);
    s_wireSendAttached.store(false);
    s_wireRecvAttached.store(false);

    // Close logs AFTER detach — otherwise a still-running hooked call could
    // race with logger shutdown.
    closeLoggers();
    spdlog::info("GGTB::NetLog: hooks detached and logs closed");
}

bool IsActive() { return s_attached.load(); }

DWORD GetLastSkillResultTickMs(uint32_t *outSkillId)
{
    if (outSkillId)
        *outSkillId = s_lastSkillResultId.load(std::memory_order_relaxed);
    return s_lastSkillResultMs.load(std::memory_order_acquire);
}

DWORD GetLastTradeRequestTickMs(uint32_t *outRequesterId)
{
    if (outRequesterId)
        *outRequesterId = s_lastTradeRequesterId.load(std::memory_order_relaxed);
    return s_lastTradeRequestMs.load(std::memory_order_acquire);
}

DWORD GetLastTradePeerLockTickMs()
{
    return s_lastTradePeerLockMs.load(std::memory_order_acquire);
}

} // namespace GGTB::NetLog
