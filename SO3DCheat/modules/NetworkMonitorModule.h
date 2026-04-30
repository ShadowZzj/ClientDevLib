#pragma once
#include "../IModule.h"
#include <Windows.h>
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <atomic>

class NetworkMonitorModule : public IModule
{
  public:
    NetworkMonitorModule() : IModule("Net Monitor") {}
    bool CanAutoPause() const override { return false; }

    // ── RVA constants ─────────────────────────────────────────────
    // Connection::SendRawPacket (VA 0xBFCC80)
    //   __thiscall(this, int type, int data) — 12-byte small packets
    //   prologue: push ebp; mov ebp,esp; push esi; mov esi,ecx  (6 bytes)
    static constexpr uintptr_t kSendRawRVA         = 0x7FCC80;
    static constexpr int       kSendRawPrologSize   = 6;

    // Connection::SendRawLargePacket (VA 0xBFE5B0)
    //   __thiscall(this, int type, void* data, int size) — variable-size packets
    //   prologue: push ebp; mov ebp,esp; cmp [ebp+10h],0;
    //             push esi; mov esi,ecx  (10 bytes)
    static constexpr uintptr_t kSendRawLargeRVA     = 0x7FE5B0;
    static constexpr int       kSendRawLargePrologSz = 10;

    // sub_6AA2A0  int __cdecl RecvDispatch(int type, int* data, int size)
    //   prologue: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+0Ch]  (7 bytes)
    static constexpr uintptr_t kRecvRVA        = 0x2AA2A0;
    static constexpr int       kRecvPrologSize  = 7;

    // dword_1B9D638 — game server connection object
    static constexpr uintptr_t kConnObjRVA     = 0x179D638;

    static constexpr int kGGAuthType = 511252;

    // ── Static hook state ─────────────────────────────────────────
    static inline uintptr_t s_base_            = 0;
    static inline bool      s_hooksInstalled_  = false;

    static inline BYTE  s_sendRawOrig_[8]      = {};
    static inline BYTE* s_sendRawTramp_        = nullptr;

    static inline BYTE  s_sendRawLargeOrig_[12] = {};
    static inline BYTE* s_sendRawLargeTramp_    = nullptr;

    static inline BYTE  s_recvOrig_[8]         = {};
    static inline BYTE* s_recvTramp_           = nullptr;

    static inline std::atomic<int> s_lastSendType_{0};
    static inline std::atomic<int> s_lastSendSize_{0};
    static inline std::atomic<int> s_lastRecvType_{0};
    static inline std::atomic<int> s_lastRecvSize_{0};
    static inline std::atomic<int> s_ggAuthCount_ {0};
    static inline bool             s_wasConnected_ = false;
    static inline bool             s_verbose_       = false;

    // ── Capture buffer for 111051 (channel + char select) ───────
    static inline BYTE  s_captured111051_[64]  = {};
    static inline int   s_captured111051Size_  = 0;
    static inline bool  s_has111051_           = false;

    // ── Lobby auth tracking (111000 sent timestamp) ─────────────
    static inline ULONGLONG s_lastLobbyLoginMs_ = 0;

    // ── Detours ───────────────────────────────────────────────────

    // SendRawPacket: __thiscall(this, type, data) → __fastcall shim
    static int __fastcall SendRawDetour(void* thisPtr, void* /*edx*/,
                                        int type, int data)
    {
        s_lastSendType_.store(type, std::memory_order_relaxed);
        s_lastSendSize_.store(12, std::memory_order_relaxed);
        if (s_verbose_)
            spdlog::info("[SEND-S] type:{} data:{}", type, data);

        using Fn = int(__fastcall*)(void*, void*, int, int);
        return reinterpret_cast<Fn>(s_sendRawTramp_)(thisPtr, nullptr, type, data);
    }

    // SendRawLargePacket: __thiscall(this, type, data*, size) → __fastcall shim
    static int __fastcall SendRawLargeDetour(void* thisPtr, void* /*edx*/,
                                             int type, void* data, int size)
    {
        s_lastSendType_.store(type, std::memory_order_relaxed);
        s_lastSendSize_.store(size, std::memory_order_relaxed);
        if (s_verbose_)
            spdlog::info("[SEND-L] type:{} size:{}", type, size);

        if (type == 111000)
            s_lastLobbyLoginMs_ = GetTickCount64();

        if (type == 111051 && size > 0 &&
            size <= static_cast<int>(sizeof(s_captured111051_)))
        {
            std::memcpy(s_captured111051_, data, size);
            s_captured111051Size_ = size;
            s_has111051_ = true;
            spdlog::info("NetMon: captured 111051 ({} bytes)", size);
        }

        using Fn = int(__fastcall*)(void*, void*, int, void*, int);
        return reinterpret_cast<Fn>(s_sendRawLargeTramp_)(thisPtr, nullptr,
                                                           type, data, size);
    }

    // sub_6AA2A0 is __cdecl(type, data*, size)
    static int __cdecl RecvDetour(int type, int* data, int size)
    {
        s_lastRecvType_.store(type, std::memory_order_relaxed);
        s_lastRecvSize_.store(size, std::memory_order_relaxed);

        if (type == kGGAuthType)
        {
            s_ggAuthCount_.fetch_add(1, std::memory_order_relaxed);
            spdlog::warn("[RECV] *** GG_AUTH *** type:{} size:{} count:{}",
                         type, size, s_ggAuthCount_.load());
        }
        else if (s_verbose_)
        {
            spdlog::info("[RECV] type:{} size:{}", type, size);
        }

        using Fn = int(__cdecl*)(int, int*, int);
        return reinterpret_cast<Fn>(s_recvTramp_)(type, data, size);
    }

    // ── Inline hook helpers ───────────────────────────────────────

    static bool Install(uintptr_t target, void* detour, int prologSz,
                        BYTE* savedOut, BYTE** trampOut)
    {
        std::memcpy(savedOut, reinterpret_cast<void*>(target), prologSz);

        BYTE* tramp = static_cast<BYTE*>(
            VirtualAlloc(nullptr, prologSz + 5,
                         MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!tramp)
            return false;

        std::memcpy(tramp, savedOut, prologSz);
        tramp[prologSz] = 0xE9;
        *reinterpret_cast<int32_t*>(tramp + prologSz + 1) =
            static_cast<int32_t>((target + prologSz) -
                                 reinterpret_cast<uintptr_t>(tramp + prologSz + 5));
        *trampOut = tramp;

        DWORD old;
        VirtualProtect(reinterpret_cast<void*>(target), prologSz,
                       PAGE_EXECUTE_READWRITE, &old);
        *reinterpret_cast<BYTE*>(target) = 0xE9;
        *reinterpret_cast<int32_t*>(target + 1) =
            static_cast<int32_t>(reinterpret_cast<uintptr_t>(detour) - (target + 5));
        for (int i = 5; i < prologSz; ++i)
            *reinterpret_cast<BYTE*>(target + i) = 0x90;
        VirtualProtect(reinterpret_cast<void*>(target), prologSz, old, &old);

        return true;
    }

    static void Uninstall(uintptr_t target, int prologSz,
                          const BYTE* saved, BYTE** trampPtr)
    {
        if (!*trampPtr)
            return;
        DWORD old;
        VirtualProtect(reinterpret_cast<void*>(target), prologSz,
                       PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(reinterpret_cast<void*>(target), saved, prologSz);
        VirtualProtect(reinterpret_cast<void*>(target), prologSz, old, &old);

        VirtualFree(*trampPtr, 0, MEM_RELEASE);
        *trampPtr = nullptr;
    }

    // ── Public API ────────────────────────────────────────────────

    void InstallHooks()
    {
        if (s_hooksInstalled_)
            return;
        s_base_ = reinterpret_cast<uintptr_t>(GetModuleHandleA("SO3D.exe"));
        if (!s_base_)
            return;

        if (Install(s_base_ + kSendRawRVA,
                    reinterpret_cast<void*>(&SendRawDetour),
                    kSendRawPrologSize, s_sendRawOrig_, &s_sendRawTramp_))
            spdlog::info("NetMon: SendRaw hook @ {:X}", s_base_ + kSendRawRVA);

        if (Install(s_base_ + kSendRawLargeRVA,
                    reinterpret_cast<void*>(&SendRawLargeDetour),
                    kSendRawLargePrologSz, s_sendRawLargeOrig_,
                    &s_sendRawLargeTramp_))
            spdlog::info("NetMon: SendRawLarge hook @ {:X}",
                         s_base_ + kSendRawLargeRVA);

        if (Install(s_base_ + kRecvRVA,
                    reinterpret_cast<void*>(&RecvDetour),
                    kRecvPrologSize, s_recvOrig_, &s_recvTramp_))
            spdlog::info("NetMon: recv hook @ {:X}", s_base_ + kRecvRVA);

        s_hooksInstalled_ = true;
        spdlog::info("NetMon: all hooks installed, verbose={}", s_verbose_);
    }

    void RemoveHooks()
    {
        if (!s_hooksInstalled_ || !s_base_)
            return;
        Uninstall(s_base_ + kSendRawRVA, kSendRawPrologSize,
                  s_sendRawOrig_, &s_sendRawTramp_);
        Uninstall(s_base_ + kSendRawLargeRVA, kSendRawLargePrologSz,
                  s_sendRawLargeOrig_, &s_sendRawLargeTramp_);
        Uninstall(s_base_ + kRecvRVA, kRecvPrologSize,
                  s_recvOrig_, &s_recvTramp_);
        s_hooksInstalled_ = false;
        spdlog::info("NetMon: hooks removed");
    }

    // ── IModule ───────────────────────────────────────────────────

    void OnRender() override
    {
        ImGui::Checkbox("Verbose##NetMon", &s_verbose_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"打印所有收发包到 cheat.log");

        ImGui::Text("Last SEND: %d  size:%d",
                    s_lastSendType_.load(), s_lastSendSize_.load());
        ImGui::Text("Last RECV: %d  size:%d",
                    s_lastRecvType_.load(), s_lastRecvSize_.load());

        int gg = s_ggAuthCount_.load();
        if (gg > 0)
            ImGui::TextColored(ImVec4(1, 1, 0, 1),
                              "GG Auth (511252) received: %d", gg);
        else
            ImGui::TextDisabled("GG Auth (511252): none");

        if (s_has111051_)
            ImGui::TextColored(ImVec4(0, 1, 0, 1),
                              "111051 captured (%d bytes)", s_captured111051Size_);

        if (s_base_)
        {
            auto pConn = *reinterpret_cast<uintptr_t*>(s_base_ + kConnObjRVA);
            if (pConn)
            {
                bool alive = *reinterpret_cast<BYTE*>(pConn + 12) != 0;
                if (alive)
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "Connected");
                else
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "DISCONNECTED");

                if (s_wasConnected_ && !alive)
                    spdlog::error("[NET] *** DISCONNECT *** lastRecv:{} lastSend:{} "
                                 "ggAuth:{}", s_lastRecvType_.load(),
                                 s_lastSendType_.load(), s_ggAuthCount_.load());
                s_wasConnected_ = alive;
            }
        }
    }

    void OnShutdown() override { RemoveHooks(); }
};
