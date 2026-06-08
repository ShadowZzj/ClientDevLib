#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "LoginBridge.h"

#include <winsock2.h>
#include <Windows.h>
#include <json.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

#ifndef GGTB_LOGIN_BRIDGE_PIPE_PREFIX
#define GGTB_LOGIN_BRIDGE_PIPE_PREFIX "GGTB_LOGIN_BRIDGE_"
#endif

namespace GGTB::LoginBridge
{
namespace
{
constexpr uintptr_t kIdaImageBase = 0x400000;
constexpr uintptr_t kIatHtons = 0xC8D630;
constexpr uintptr_t kIatCloseSocket = 0xC8D648;
constexpr uintptr_t kIatConnect = 0xC8D64C;
constexpr uintptr_t kIatInetAddr = 0xC8D654;
constexpr uintptr_t kIatRecv = 0xC8D658;
constexpr uintptr_t kIatSelect = 0xC8D65C;
constexpr uintptr_t kIatSend = 0xC8D660;
constexpr uintptr_t kIatSocket = 0xC8D668;
constexpr uintptr_t kIatWSAGetLastError = 0xC8D670;
constexpr uintptr_t kNetBaseCtor = 0xB1BE80;
constexpr uintptr_t kNetBaseDtor = 0xB1BF30;
constexpr uintptr_t kNetConnect = 0xB1C020;
constexpr uintptr_t kNetRawRecv = 0xB1C750;
constexpr uintptr_t kNetRawSendAll = 0xB1C9A0;
constexpr uintptr_t kRawRecvCallSite = 0xB1C88A; // call recv inside Net__RawRecv
constexpr uintptr_t kExpected123RecvRva = 0x50A9A;
constexpr size_t kNativeClientObjectSize = 0x88;
constexpr size_t kNativeRecvBufferSize = 0x2800;

struct GameWinsockApi
{
    using HtonsFn = u_short(WSAAPI *)(u_short);
    using CloseSocketFn = int(WSAAPI *)(SOCKET);
    using ConnectFn = int(WSAAPI *)(SOCKET, const sockaddr *, int);
    using InetAddrFn = unsigned long(WSAAPI *)(const char *);
    using RecvFn = int(WSAAPI *)(SOCKET, char *, int, int);
    using SelectFn = int(WSAAPI *)(int, fd_set *, fd_set *, fd_set *, const timeval *);
    using SendFn = int(WSAAPI *)(SOCKET, const char *, int, int);
    using SocketFn = SOCKET(WSAAPI *)(int, int, int);
    using WSAGetLastErrorFn = int(WSAAPI *)();
    using NetBaseCtorFn = void *(__fastcall *)(void *, void *, size_t);
    using NetBaseDtorFn = int(__fastcall *)(void *, void *);
    using NetConnectFn = int(__fastcall *)(void *, void *, const char *, int);
    using NetRawRecvFn = int(__fastcall *)(void *, void *, int, int);
    using NetRawSendAllFn = int(__fastcall *)(void *, void *, const void *, int);

    uintptr_t mainBase = 0;
    HtonsFn htons = nullptr;
    CloseSocketFn closesocket = nullptr;
    ConnectFn connect = nullptr;
    InetAddrFn inet_addr = nullptr;
    RecvFn recv = nullptr;
    SelectFn select = nullptr;
    SendFn send = nullptr;
    SocketFn socket = nullptr;
    WSAGetLastErrorFn WSAGetLastError = nullptr;
    NetBaseCtorFn netBaseCtor = nullptr;
    NetBaseDtorFn netBaseDtor = nullptr;
    NetConnectFn netConnect = nullptr;
    NetRawRecvFn netRawRecv = nullptr;
    NetRawSendAllFn netRawSendAll = nullptr;

    uintptr_t targetHtons = 0;
    uintptr_t targetCloseSocket = 0;
    uintptr_t targetConnect = 0;
    uintptr_t targetInetAddr = 0;
    uintptr_t targetRecv = 0;
    uintptr_t targetSelect = 0;
    uintptr_t targetSend = 0;
    uintptr_t targetSocket = 0;
    uintptr_t targetWSAGetLastError = 0;
    uintptr_t targetNetBaseCtor = 0;
    uintptr_t targetNetBaseDtor = 0;
    uintptr_t targetNetConnect = 0;
    uintptr_t targetNetRawRecv = 0;
    uintptr_t targetNetRawSendAll = 0;
};

struct BridgeSession
{
    SOCKET sock = INVALID_SOCKET;
    bool nativeConnect = false;
    std::vector<unsigned char> nativeObject;
};

std::atomic<bool> g_stop{false};
std::thread g_thread;
DWORD g_pid = 0;
std::string g_pipePath;

std::mutex g_sessionsMutex;
std::unordered_map<uint32_t, std::shared_ptr<BridgeSession>> g_sessions;
uint32_t g_nextSessionId = 1;

std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return s;
}

std::string HexPtr(uintptr_t value)
{
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << value;
    return oss.str();
}

std::string HexDump(const void *data, size_t size)
{
    const auto *bytes = static_cast<const unsigned char *>(data);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; ++i)
    {
        if (i)
            oss << ' ';
        oss << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    }
    return oss.str();
}

std::string BaseName(const char *path)
{
    if (!path || !*path)
        return {};
    const char *slash1 = std::strrchr(path, '\\');
    const char *slash2 = std::strrchr(path, '/');
    const char *slash = slash1 && slash2 ? std::max(slash1, slash2) : (slash1 ? slash1 : slash2);
    return slash ? std::string(slash + 1) : std::string(path);
}

struct RecvPatchStatus
{
    bool ready = false;
    uintptr_t mainBase = 0;
    uintptr_t callSite = 0;
    uintptr_t target = 0;
    uintptr_t moduleBase = 0;
    uintptr_t moduleRva = 0;
    std::string moduleName;
    std::string bytes;
    std::string error;
};

bool ReadCallsiteBytes(uintptr_t address, unsigned char *out, size_t size)
{
    __try
    {
        std::memcpy(out, reinterpret_cast<const void *>(address), size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

RecvPatchStatus GetRecvPatchStatus()
{
    RecvPatchStatus status;
    status.mainBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    status.callSite = status.mainBase ? status.mainBase + (kRawRecvCallSite - kIdaImageBase) : 0;
    if (!status.mainBase || !status.callSite)
    {
        status.error = "main module is not ready";
        return status;
    }

    unsigned char bytes[8]{};
    if (!ReadCallsiteBytes(status.callSite, bytes, sizeof(bytes)))
    {
        status.error = "failed to read recv callsite";
        return status;
    }

    status.bytes = HexDump(bytes, sizeof(bytes));
    if (bytes[0] != 0xE8)
    {
        status.error = "recv callsite is not a near call yet";
        return status;
    }

    int32_t rel = 0;
    std::memcpy(&rel, bytes + 1, sizeof(rel));
    status.target = status.callSite + 5 + rel;

    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(status.target),
                           &module) &&
        module)
    {
        status.moduleBase = reinterpret_cast<uintptr_t>(module);
        status.moduleRva = status.target - status.moduleBase;
        char path[MAX_PATH]{};
        GetModuleFileNameA(module, path, MAX_PATH);
        status.moduleName = BaseName(path);
    }

    const bool moduleOk = ToLower(status.moduleName) == "123.dll";
    const bool rvaOk = status.moduleRva == kExpected123RecvRva;
    status.ready = moduleOk && rvaOk;
    if (!status.ready)
    {
        std::ostringstream oss;
        oss << "recv call target is " << (status.moduleName.empty() ? std::string("<unknown>") : status.moduleName)
            << "+" << HexPtr(status.moduleRva)
            << ", expected 123.dll+" << HexPtr(kExpected123RecvRva);
        status.error = oss.str();
    }
    return status;
}

nlohmann::json RecvPatchStatusJson(const RecvPatchStatus &status)
{
    return {
        {"ready", status.ready},
        {"mainBase", HexPtr(status.mainBase)},
        {"callSite", HexPtr(status.callSite)},
        {"bytes", status.bytes},
        {"target", HexPtr(status.target)},
        {"module", status.moduleName},
        {"moduleBase", HexPtr(status.moduleBase)},
        {"moduleRva", HexPtr(status.moduleRva)},
        {"expected", std::string("123.dll+") + HexPtr(kExpected123RecvRva)},
        {"error", status.error},
    };
}

RecvPatchStatus WaitForRecvPatchReady(int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    std::string lastKey;
    while (!g_stop)
    {
        auto status = GetRecvPatchStatus();
        std::string key = status.bytes + "|" + status.moduleName + "|" + HexPtr(status.moduleRva) + "|" + status.error;
        if (key != lastKey)
        {
            lastKey = key;
            spdlog::info("GGTB::LoginBridge recv patch status ready={} callSite={} bytes='{}' target={} module={} rva={} error={}",
                         status.ready,
                         HexPtr(status.callSite),
                         status.bytes,
                         HexPtr(status.target),
                         status.moduleName.empty() ? "<unknown>" : status.moduleName,
                         HexPtr(status.moduleRva),
                         status.error);
        }
        if (status.ready)
            return status;
        int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count());
        if (elapsed >= timeoutMs)
            return status;
        Sleep(100);
    }
    return GetRecvPatchStatus();
}

bool HexValue(char ch, unsigned char &out)
{
    if (ch >= '0' && ch <= '9')
    {
        out = static_cast<unsigned char>(ch - '0');
        return true;
    }
    if (ch >= 'a' && ch <= 'f')
    {
        out = static_cast<unsigned char>(10 + ch - 'a');
        return true;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        out = static_cast<unsigned char>(10 + ch - 'A');
        return true;
    }
    return false;
}

bool ParseHex(const std::string &text, std::vector<unsigned char> &out, std::string &error)
{
    out.clear();
    int high = -1;
    for (char ch : text)
    {
        if (std::isspace(static_cast<unsigned char>(ch)) || ch == '-' || ch == ':' || ch == ',')
            continue;

        unsigned char value = 0;
        if (!HexValue(ch, value))
        {
            error = "non-hex character in payload";
            return false;
        }
        if (high < 0)
        {
            high = value;
        }
        else
        {
            out.push_back(static_cast<unsigned char>((high << 4) | value));
            high = -1;
        }
    }
    if (high >= 0)
    {
        error = "odd number of hex digits";
        return false;
    }
    return true;
}

template <typename T>
T ReadIat(uintptr_t mainBase, uintptr_t idaVa, uintptr_t *targetOut)
{
    auto slot = reinterpret_cast<uintptr_t *>(mainBase + (idaVa - kIdaImageBase));
    uintptr_t target = slot ? *slot : 0;
    if (targetOut)
        *targetOut = target;
    return reinterpret_cast<T>(target);
}

GameWinsockApi ResolveApi()
{
    GameWinsockApi api;
    api.mainBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!api.mainBase)
        return api;

    api.htons = ReadIat<GameWinsockApi::HtonsFn>(api.mainBase, kIatHtons, &api.targetHtons);
    api.closesocket = ReadIat<GameWinsockApi::CloseSocketFn>(api.mainBase, kIatCloseSocket, &api.targetCloseSocket);
    api.connect = ReadIat<GameWinsockApi::ConnectFn>(api.mainBase, kIatConnect, &api.targetConnect);
    api.inet_addr = ReadIat<GameWinsockApi::InetAddrFn>(api.mainBase, kIatInetAddr, &api.targetInetAddr);
    api.recv = ReadIat<GameWinsockApi::RecvFn>(api.mainBase, kIatRecv, &api.targetRecv);
    api.select = ReadIat<GameWinsockApi::SelectFn>(api.mainBase, kIatSelect, &api.targetSelect);
    api.send = ReadIat<GameWinsockApi::SendFn>(api.mainBase, kIatSend, &api.targetSend);
    api.socket = ReadIat<GameWinsockApi::SocketFn>(api.mainBase, kIatSocket, &api.targetSocket);
    api.WSAGetLastError = ReadIat<GameWinsockApi::WSAGetLastErrorFn>(api.mainBase, kIatWSAGetLastError, &api.targetWSAGetLastError);
    api.targetNetBaseCtor = api.mainBase + (kNetBaseCtor - kIdaImageBase);
    api.netBaseCtor = reinterpret_cast<GameWinsockApi::NetBaseCtorFn>(api.targetNetBaseCtor);
    api.targetNetBaseDtor = api.mainBase + (kNetBaseDtor - kIdaImageBase);
    api.netBaseDtor = reinterpret_cast<GameWinsockApi::NetBaseDtorFn>(api.targetNetBaseDtor);
    api.targetNetConnect = api.mainBase + (kNetConnect - kIdaImageBase);
    api.netConnect = reinterpret_cast<GameWinsockApi::NetConnectFn>(api.targetNetConnect);
    api.targetNetRawRecv = api.mainBase + (kNetRawRecv - kIdaImageBase);
    api.netRawRecv = reinterpret_cast<GameWinsockApi::NetRawRecvFn>(api.targetNetRawRecv);
    api.targetNetRawSendAll = api.mainBase + (kNetRawSendAll - kIdaImageBase);
    api.netRawSendAll = reinterpret_cast<GameWinsockApi::NetRawSendAllFn>(api.targetNetRawSendAll);
    return api;
}

bool ApiReady(const GameWinsockApi &api)
{
    return api.htons && api.closesocket && api.connect && api.inet_addr && api.recv &&
           api.select && api.send && api.socket && api.WSAGetLastError;
}

nlohmann::json ApiSnapshotJson()
{
    auto api = ResolveApi();
    auto recvPatch = GetRecvPatchStatus();
    return {
        {"mainBase", HexPtr(api.mainBase)},
        {"ready", ApiReady(api)},
        {"recvPatch", RecvPatchStatusJson(recvPatch)},
        {"iat", {
            {"htons", {{"slot", HexPtr(api.mainBase + (kIatHtons - kIdaImageBase))}, {"target", HexPtr(api.targetHtons)}}},
            {"closesocket", {{"slot", HexPtr(api.mainBase + (kIatCloseSocket - kIdaImageBase))}, {"target", HexPtr(api.targetCloseSocket)}}},
            {"connect", {{"slot", HexPtr(api.mainBase + (kIatConnect - kIdaImageBase))}, {"target", HexPtr(api.targetConnect)}}},
            {"inet_addr", {{"slot", HexPtr(api.mainBase + (kIatInetAddr - kIdaImageBase))}, {"target", HexPtr(api.targetInetAddr)}}},
            {"recv", {{"slot", HexPtr(api.mainBase + (kIatRecv - kIdaImageBase))}, {"target", HexPtr(api.targetRecv)}}},
            {"select", {{"slot", HexPtr(api.mainBase + (kIatSelect - kIdaImageBase))}, {"target", HexPtr(api.targetSelect)}}},
            {"send", {{"slot", HexPtr(api.mainBase + (kIatSend - kIdaImageBase))}, {"target", HexPtr(api.targetSend)}}},
            {"socket", {{"slot", HexPtr(api.mainBase + (kIatSocket - kIdaImageBase))}, {"target", HexPtr(api.targetSocket)}}},
            {"WSAGetLastError", {{"slot", HexPtr(api.mainBase + (kIatWSAGetLastError - kIdaImageBase))}, {"target", HexPtr(api.targetWSAGetLastError)}}},
        }},
        {"functions", {
            {"NetBaseCtor", HexPtr(api.targetNetBaseCtor)},
            {"NetBaseDtor", HexPtr(api.targetNetBaseDtor)},
            {"NetConnect", HexPtr(api.targetNetConnect)},
            {"NetRawRecv", HexPtr(api.targetNetRawRecv)},
            {"NetRawSendAll", HexPtr(api.targetNetRawSendAll)},
        }},
    };
}

bool GameRawRecvOnce(const GameWinsockApi &api, void *clientObject, SOCKET sock, int timeoutMs,
                     int maxBytes, std::vector<unsigned char> &out,
                     int &ret, int &error)
{
    out.clear();
    ret = 0;
    error = 0;
    if (!api.netRawRecv)
    {
        error = ERROR_PROC_NOT_FOUND;
        return false;
    }

    if (clientObject)
    {
        auto *self = static_cast<unsigned char *>(clientObject);
        const uint32_t before = *reinterpret_cast<uint32_t *>(self + 0x24);
        const int timeoutSec = std::max(0, timeoutMs / 1000);
        const int timeoutUsec = std::max(0, timeoutMs % 1000) * 1000;

        auto callRawRecvSeh = [](GameWinsockApi::NetRawRecvFn fn, unsigned char *self,
                                 int sec, int usec, int *retOut, int *errOut) -> bool {
            __try
            {
                *retOut = fn(self, nullptr, sec, usec);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                *errOut = GetExceptionCode();
                return false;
            }
        };

        if (!callRawRecvSeh(api.netRawRecv, self, timeoutSec, timeoutUsec, &ret, &error))
            return false;

        const uint32_t fill = *reinterpret_cast<uint32_t *>(self + 0x24);
        const auto *buffer = reinterpret_cast<unsigned char *>(*reinterpret_cast<uint32_t *>(self + 0x18));
        if (fill > before && buffer)
        {
            const uint32_t added = std::min<uint32_t>(fill - before, static_cast<uint32_t>(maxBytes));
            out.assign(buffer + before, buffer + before + added);
        }
        if (!self[0x0C] && out.empty())
            error = api.WSAGetLastError ? api.WSAGetLastError() : WSAGetLastError();
        return true;
    }

    const int cap = std::max(2, maxBytes + 1);
    std::vector<unsigned char> buffer(static_cast<size_t>(cap), 0);
    alignas(4) unsigned char fakeClient[0x40] = {};

    *reinterpret_cast<uint32_t *>(fakeClient + 0x08) = static_cast<uint32_t>(cap);
    fakeClient[0x0C] = 1;
    *reinterpret_cast<uint32_t *>(fakeClient + 0x10) = static_cast<uint32_t>(sock);
    *reinterpret_cast<uint32_t *>(fakeClient + 0x18) = reinterpret_cast<uint32_t>(buffer.data());
    *reinterpret_cast<uint32_t *>(fakeClient + 0x24) = 0;

    const int timeoutSec = std::max(0, timeoutMs / 1000);
    const int timeoutUsec = std::max(0, timeoutMs % 1000) * 1000;

    auto callRawRecvSeh = [](GameWinsockApi::NetRawRecvFn fn, unsigned char *self,
                             int sec, int usec, int *retOut, int *errOut) -> bool {
        __try
        {
            *retOut = fn(self, nullptr, sec, usec);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *errOut = GetExceptionCode();
            return false;
        }
    };

    if (!callRawRecvSeh(api.netRawRecv, fakeClient, timeoutSec, timeoutUsec, &ret, &error))
        return false;

    const auto fill = *reinterpret_cast<uint32_t *>(fakeClient + 0x24);
    if (fill > 0 && fill <= static_cast<uint32_t>(maxBytes))
        out.assign(buffer.begin(), buffer.begin() + fill);
    if (!fakeClient[0x0C] && out.empty())
        error = api.WSAGetLastError ? api.WSAGetLastError() : WSAGetLastError();
    return true;
}

nlohmann::json ReadChunks(const GameWinsockApi &api, const BridgeSession &session, int totalTimeoutMs,
                          int quietMs, int maxChunks, int maxBytes, bool gameRawRecv)
{
    SOCKET sock = session.sock;
    void *nativeObject = session.nativeConnect && !session.nativeObject.empty()
        ? const_cast<unsigned char *>(session.nativeObject.data())
        : nullptr;
    nlohmann::json chunks = nlohmann::json::array();
    auto start = std::chrono::steady_clock::now();
    auto lastData = start;
    int total = 0;
    bool gotAny = false;
    bool closed = false;
    int error = 0;

    while (!g_stop)
    {
        auto now = std::chrono::steady_clock::now();
        int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
        if (elapsed >= totalTimeoutMs)
            break;
        if (gotAny)
        {
            int idle = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - lastData).count());
            if (idle >= quietMs)
                break;
        }
        if (static_cast<int>(chunks.size()) >= maxChunks || total >= maxBytes)
            break;

        if (gameRawRecv)
        {
            std::vector<unsigned char> buf;
            int ret = 0;
            int rawError = 0;
            const int want = std::min(maxBytes - total, 8192);
            if (!GameRawRecvOnce(api, nativeObject, sock, 100, want, buf, ret, rawError))
            {
                error = rawError;
                break;
            }
            if (buf.empty())
            {
                if (rawError)
                {
                    error = rawError;
                    break;
                }
                continue;
            }

            gotAny = true;
            lastData = std::chrono::steady_clock::now();
            total += static_cast<int>(buf.size());
            spdlog::info("GGTB::LoginBridge read chunk via NetRawRecv socket={} len={} ret={} error={} hex={}",
                         static_cast<uint32_t>(sock), buf.size(), ret, rawError,
                         HexDump(buf.data(), std::min<size_t>(buf.size(), 256)));
            chunks.push_back({
                {"len", static_cast<int>(buf.size())},
                {"hex", HexDump(buf.data(), buf.size())},
                {"via", "NetRawRecv"},
                {"ret", ret},
                {"error", rawError},
            });
            continue;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        int ready = api.select(0, &readSet, nullptr, nullptr, &tv);
        if (ready < 0)
        {
            error = api.WSAGetLastError ? api.WSAGetLastError() : WSAGetLastError();
            break;
        }
        if (ready == 0)
            continue;

        char buf[8192];
        int want = std::min<int>(static_cast<int>(sizeof(buf)), maxBytes - total);
        int n = api.recv(sock, buf, want, 0);
        if (n > 0)
        {
            gotAny = true;
            lastData = std::chrono::steady_clock::now();
            total += n;
            spdlog::info("GGTB::LoginBridge read chunk via recv socket={} len={} hex={}",
                         static_cast<uint32_t>(sock), n,
                         HexDump(buf, std::min<size_t>(static_cast<size_t>(n), 256)));
            chunks.push_back({
                {"len", n},
                {"hex", HexDump(buf, static_cast<size_t>(n))},
                {"via", "recv"},
            });
            continue;
        }
        if (n == 0)
        {
            closed = true;
            break;
        }
        error = api.WSAGetLastError ? api.WSAGetLastError() : WSAGetLastError();
        break;
    }

    return {
        {"chunks", chunks},
        {"bytes", total},
        {"closed", closed},
        {"error", error},
    };
}

std::shared_ptr<BridgeSession> GetSession(uint32_t id)
{
    std::lock_guard<std::mutex> lk(g_sessionsMutex);
    auto it = g_sessions.find(id);
    return it == g_sessions.end() ? nullptr : it->second;
}

void CloseSession(uint32_t id, const GameWinsockApi &api)
{
    std::shared_ptr<BridgeSession> session;
    {
        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        auto it = g_sessions.find(id);
        if (it == g_sessions.end())
            return;
        session = it->second;
        g_sessions.erase(it);
    }
    if (!session)
        return;
    if (session->nativeConnect)
    {
        if (api.netBaseDtor && !session->nativeObject.empty())
            api.netBaseDtor(session->nativeObject.data(), nullptr);
        session->sock = INVALID_SOCKET;
        return;
    }
    if (session->sock != INVALID_SOCKET && api.closesocket)
        api.closesocket(session->sock);
    session->sock = INVALID_SOCKET;
}

void CloseAllSessions()
{
    auto api = ResolveApi();
    std::vector<std::shared_ptr<BridgeSession>> sessions;
    {
        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        for (auto &kv : g_sessions)
            sessions.push_back(kv.second);
        g_sessions.clear();
    }
    for (auto &session : sessions)
    {
        if (!session)
            continue;
        if (session->nativeConnect)
        {
            if (api.netBaseDtor && !session->nativeObject.empty())
                api.netBaseDtor(session->nativeObject.data(), nullptr);
            session->sock = INVALID_SOCKET;
        }
        else if (session->sock != INVALID_SOCKET && api.closesocket)
        {
            api.closesocket(session->sock);
            session->sock = INVALID_SOCKET;
        }
    }
}

nlohmann::json HandleOpen(const nlohmann::json &cmd)
{
    auto api = ResolveApi();
    if (!ApiReady(api))
        return {{"ok", false}, {"error", "game winsock IAT is not ready"}, {"api", ApiSnapshotJson()}};

    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    std::string host = cmd.value("host", std::string("127.2.57.25"));
    int port = cmd.value("port", 10002);
    int readTimeoutMs = cmd.value("readTimeoutMs", 3000);
    int quietMs = cmd.value("quietMs", 200);
    bool gameRawRecv = cmd.value("gameRawRecv", false);
    bool nativeConnect = cmd.value("nativeConnect", false);
    int waitPatchMs = cmd.value("waitRecvPatchMs", 30000);

    auto patch = WaitForRecvPatchReady(std::max(0, waitPatchMs));
    if (!patch.ready)
    {
        return {
            {"ok", false},
            {"error", "123.dll recv patch is not ready"},
            {"recvPatch", RecvPatchStatusJson(patch)},
        };
    }
    spdlog::info("GGTB::LoginBridge open host={} port={} gameRawRecv={} nativeConnect={} recvPatch={}",
                 host, port, gameRawRecv, nativeConnect, RecvPatchStatusJson(patch).dump(-1, ' ', false));

    auto session = std::make_shared<BridgeSession>();
    session->nativeConnect = nativeConnect;

    if (nativeConnect)
    {
        if (!api.netBaseCtor || !api.netConnect || !api.netBaseDtor)
            return {{"ok", false}, {"error", "native connect functions are not ready"}, {"api", ApiSnapshotJson()}};
        session->nativeObject.assign(kNativeClientObjectSize, 0);
        api.netBaseCtor(session->nativeObject.data(), nullptr, kNativeRecvBufferSize);
        if (!api.netConnect(session->nativeObject.data(), nullptr, host.c_str(), port))
        {
            int err = api.WSAGetLastError ? api.WSAGetLastError() : WSAGetLastError();
            api.netBaseDtor(session->nativeObject.data(), nullptr);
            return {{"ok", false}, {"error", "native NetConnect failed"}, {"wsaError", err}};
        }
        session->sock = static_cast<SOCKET>(*reinterpret_cast<uint32_t *>(session->nativeObject.data() + 0x10));
    }
    else
    {
        SOCKET sock = api.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET)
            return {{"ok", false}, {"error", "socket failed"}, {"wsaError", api.WSAGetLastError()}};

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = api.htons(static_cast<u_short>(port));
        addr.sin_addr.s_addr = api.inet_addr(host.c_str());
        if (addr.sin_addr.s_addr == INADDR_NONE)
        {
            api.closesocket(sock);
            return {{"ok", false}, {"error", "host must be an IPv4 literal"}};
        }

        if (api.connect(sock, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0)
        {
            int err = api.WSAGetLastError();
            api.closesocket(sock);
            return {{"ok", false}, {"error", "connect failed"}, {"wsaError", err}};
        }
        session->sock = sock;
    }

    uint32_t id = 0;
    {
        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        id = g_nextSessionId++;
        if (g_nextSessionId == 0)
            g_nextSessionId = 1;
        g_sessions[id] = session;
    }

    auto reads = ReadChunks(api, *session, readTimeoutMs, quietMs, 16, 65536, gameRawRecv);
    spdlog::info("GGTB::LoginBridge open session={} socket={} host={} port={} readBytes={} chunks={}",
                 id, static_cast<uint32_t>(session->sock), host, port,
                 reads.value("bytes", 0), reads["chunks"].size());
    return {
        {"ok", true},
        {"sessionId", id},
        {"host", host},
        {"port", port},
        {"gameRawRecv", gameRawRecv},
        {"nativeConnect", nativeConnect},
        {"socket", static_cast<uint32_t>(session->sock)},
        {"recvPatch", RecvPatchStatusJson(patch)},
        {"read", reads},
    };
}

nlohmann::json HandleSendRecv(const nlohmann::json &cmd)
{
    auto api = ResolveApi();
    if (!ApiReady(api))
        return {{"ok", false}, {"error", "game winsock IAT is not ready"}, {"api", ApiSnapshotJson()}};

    uint32_t id = cmd.value("sessionId", 0u);
    auto session = GetSession(id);
    if (!session || session->sock == INVALID_SOCKET)
        return {{"ok", false}, {"error", "bad sessionId"}};

    auto patch = GetRecvPatchStatus();
    if (!patch.ready)
    {
        return {
            {"ok", false},
            {"error", "123.dll recv patch is not ready"},
            {"recvPatch", RecvPatchStatusJson(patch)},
        };
    }

    std::vector<unsigned char> payload;
    std::string parseError;
    if (!ParseHex(cmd.value("hex", std::string{}), payload, parseError))
        return {{"ok", false}, {"error", parseError}};
    if (payload.empty())
        return {{"ok", false}, {"error", "empty payload"}};

    int sent = 0;
    if (session->nativeConnect)
    {
        if (!api.netRawSendAll)
            return {{"ok", false}, {"error", "NetRawSendAll is not ready"}};
        sent = api.netRawSendAll(session->nativeObject.data(), nullptr, payload.data(), static_cast<int>(payload.size()));
    }
    else
    {
        sent = api.send(session->sock, reinterpret_cast<const char *>(payload.data()),
                        static_cast<int>(payload.size()), 0);
    }
    if (sent < 0)
        return {{"ok", false}, {"error", "send failed"}, {"wsaError", api.WSAGetLastError()}};

    int readTimeoutMs = cmd.value("readTimeoutMs", 5000);
    int quietMs = cmd.value("quietMs", 250);
    int maxChunks = cmd.value("maxChunks", 32);
    int maxBytes = cmd.value("maxBytes", 262144);
    bool gameRawRecv = cmd.value("gameRawRecv", false);

    auto reads = ReadChunks(api, *session, readTimeoutMs, quietMs, maxChunks, maxBytes, gameRawRecv);
    spdlog::info("GGTB::LoginBridge sendRecv session={} sent={} gameRawRecv={} readBytes={} chunks={} payload={}",
                 id, sent, gameRawRecv, reads.value("bytes", 0), reads["chunks"].size(),
                 HexDump(payload.data(), std::min<size_t>(payload.size(), 128)));
    return {
        {"ok", true},
        {"sessionId", id},
        {"sent", sent},
        {"gameRawRecv", gameRawRecv},
        {"nativeConnect", session->nativeConnect},
        {"recvPatch", RecvPatchStatusJson(patch)},
        {"read", reads},
    };
}

nlohmann::json HandleCommand(const nlohmann::json &cmd)
{
    std::string name = cmd.value("cmd", std::string{});
    if (name == "ping")
        return {{"ok", true}, {"pong", true}, {"pid", g_pid}, {"recvPatch", RecvPatchStatusJson(GetRecvPatchStatus())}};
    if (name == "apiSnapshot")
        return {{"ok", true}, {"api", ApiSnapshotJson()}, {"pid", g_pid}};
    if (name == "waitRecvPatch")
    {
        int waitMs = cmd.value("timeoutMs", 30000);
        auto patch = WaitForRecvPatchReady(std::max(0, waitMs));
        return {{"ok", patch.ready}, {"recvPatch", RecvPatchStatusJson(patch)}};
    }
    if (name == "open")
        return HandleOpen(cmd);
    if (name == "sendRecv")
        return HandleSendRecv(cmd);
    if (name == "close")
    {
        CloseSession(cmd.value("sessionId", 0u), ResolveApi());
        return {{"ok", true}};
    }
    if (name == "closeAll")
    {
        CloseAllSessions();
        return {{"ok", true}};
    }
    return {{"ok", false}, {"error", "unknown command"}};
}

bool WriteJsonLine(HANDLE pipe, const nlohmann::json &j)
{
    std::string line = j.dump(-1, ' ', false) + "\n";
    DWORD wrote = 0;
    return WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &wrote, nullptr) &&
           wrote == line.size();
}

bool ReadJsonLine(HANDLE pipe, std::string &line)
{
    line.clear();
    char ch = 0;
    while (!g_stop)
    {
        DWORD got = 0;
        BOOL ok = ReadFile(pipe, &ch, 1, &got, nullptr);
        if (!ok || got == 0)
            return false;
        if (ch == '\n')
            return true;
        if (ch != '\r')
            line.push_back(ch);
        if (line.size() > 1024 * 1024)
            return false;
    }
    return false;
}

void ClientLoop(HANDLE pipe)
{
    WriteJsonLine(pipe, {
        {"type", "hello"},
        {"pid", g_pid},
        {"pipe", g_pipePath},
        {"protocol", 1},
    });

    std::string line;
    while (!g_stop && ReadJsonLine(pipe, line))
    {
        nlohmann::json response;
        std::string id;
        try
        {
            auto cmd = nlohmann::json::parse(line);
            id = cmd.value("id", std::string{});
            response = HandleCommand(cmd);
        }
        catch (const std::exception &e)
        {
            response = {{"ok", false}, {"error", e.what()}};
        }
        response["type"] = "response";
        if (!id.empty())
            response["id"] = id;
        if (!WriteJsonLine(pipe, response))
            break;
    }
}

void ServerThread()
{
    g_pid = GetCurrentProcessId();
    g_pipePath = std::string("\\\\.\\pipe\\") + GGTB_LOGIN_BRIDGE_PIPE_PREFIX + std::to_string(g_pid);
    spdlog::info("GGTB::LoginBridge listening on {}", g_pipePath);

    while (!g_stop)
    {
        HANDLE pipe = CreateNamedPipeA(
            g_pipePath.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            64 * 1024,
            64 * 1024,
            0,
            nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            spdlog::warn("GGTB::LoginBridge CreateNamedPipe failed, err={}", GetLastError());
            Sleep(1000);
            continue;
        }

        BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected && !g_stop)
            ClientLoop(pipe);

        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    CloseAllSessions();
    spdlog::info("GGTB::LoginBridge stopped");
}

} // namespace

void Install()
{
    if (g_thread.joinable())
        return;
    g_stop = false;
    g_thread = std::thread(ServerThread);
}

void Uninstall()
{
    g_stop = true;
    if (!g_pipePath.empty())
    {
        HANDLE h = CreateFileA(g_pipePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
    if (g_thread.joinable())
        g_thread.join();
    CloseAllSessions();
}

} // namespace GGTB::LoginBridge
