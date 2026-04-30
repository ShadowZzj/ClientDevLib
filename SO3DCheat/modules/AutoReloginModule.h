#pragma once
#include "../IModule.h"
#include "NetworkMonitorModule.h"
#include <Windows.h>
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

class AutoReloginModule : public IModule
{
  public:
    // ── RVAs (offset from SO3D.exe base) ────────────────────────
    static constexpr uintptr_t kRVA_SendRawLargePacket  = 0x7FE5B0;
    static constexpr uintptr_t kRVA_GameConn            = 0x179D638;
    static constexpr uintptr_t kRVA_LobbyConn           = 0x7B36D4;
    static constexpr uintptr_t kRVA_LocalPlayer         = 0xD6E3E0;
    static constexpr uintptr_t kRVA_EscMenuFlag         = 0xBF7B04;
    static constexpr uintptr_t kRVA_EscMenuObj          = 0x7B3760;

    // Flags set by sub_837120 around the 111051 send
    static constexpr uintptr_t kRVA_LoginPending        = 0xD70C24;
    static constexpr uintptr_t kRVA_GameChannelData     = 0xCA42D8;
    static constexpr uintptr_t kRVA_ChannelSrc          = 0xCA4314;
    static constexpr uintptr_t kRVA_ChannelTable        = 0xCA42DC;
    static constexpr uintptr_t kRVA_ChannelIdxSrc       = 0xD70C2C;
    static constexpr uintptr_t kRVA_byte_11A68EC        = 0x7A68EC;

    static constexpr int kPktChannelSelect = 111051;

    static constexpr float     kDefaultInterval   = 35.0f;
    static constexpr ULONGLONG kStep2MinWaitMs    = 10000;
    static constexpr ULONGLONG kTimeoutMs         = 30000;

    enum State { kIdle = 0, kWaitingCharSel = 1 };

    static inline uintptr_t s_base_         = 0;
    static inline ULONGLONG s_lastTick_     = 0;
    static inline float     s_intervalMin_  = kDefaultInterval;
    static inline State     s_state_        = kIdle;
    static inline ULONGLONG s_stateStart_   = 0;

    AutoReloginModule() : IModule("Auto Relogin") {}
    bool CanAutoPause() const override { return false; }

    void EnsureBase()
    {
        if (!s_base_)
        {
            s_base_ = reinterpret_cast<uintptr_t>(GetModuleHandleA("SO3D.exe"));
            s_lastTick_ = GetTickCount64();
        }
    }

    // ── Step 1: arm ESC menu relogin state machine ──────────────
    //
    // The ESC menu's EscMenu__Update (called every frame from CSO3D__OnFrame)
    // has a state machine:
    //   state 0 = idle (process button clicks)
    //   state 2 = relogin countdown → when timer expires, does FULL cleanup:
    //             close all UI panels, destroy secondary connection,
    //             sub_9FB9A0, Connection__SendPacketSmall(411007),
    //             set g_nReloginFlag=1, advance to state 3
    //
    // By setting state=2 + timer≈0, the game itself handles everything
    // on the next frame — exactly as if the player clicked "ReLogin".
    //
    void DoRelogin()
    {
        EnsureBase();
        if (!s_base_)
            return;

        if (!NetworkMonitorModule::s_has111051_)
        {
            spdlog::warn("AutoRelogin: 111051 not captured, skipping");
            return;
        }

        auto pConn = *reinterpret_cast<uintptr_t*>(s_base_ + kRVA_GameConn);
        if (!pConn)
        {
            spdlog::warn("AutoRelogin: game connection null, skipping");
            return;
        }

        auto pMenu = *reinterpret_cast<DWORD**>(s_base_ + kRVA_EscMenuObj);
        if (!pMenu)
        {
            spdlog::warn("AutoRelogin: ESC menu object null, skipping");
            return;
        }

        spdlog::info("AutoRelogin: >>> arming ESC menu state=2 <<<");

        pMenu[12] = 2;
        *reinterpret_cast<float*>(&pMenu[13]) = 0.001f;

        s_state_      = kWaitingCharSel;
        s_stateStart_ = GetTickCount64();
    }

    // ── Step 2: wait for char-select screen → send 111051 ──────
    void PollState()
    {
        if (s_state_ != kWaitingCharSel)
            return;

        ULONGLONG now     = GetTickCount64();
        ULONGLONG elapsed = now - s_stateStart_;

        if (elapsed < kStep2MinWaitMs)
            return;

        if (elapsed > kTimeoutMs)
        {
            spdlog::error("AutoRelogin: timeout ({}ms), aborting", elapsed);
            s_state_    = kIdle;
            s_lastTick_ = now;
            return;
        }

        auto pGameConn = *reinterpret_cast<uintptr_t*>(s_base_ + kRVA_GameConn);
        if (pGameConn)
            return;

        auto pLobby = *reinterpret_cast<uintptr_t*>(s_base_ + kRVA_LobbyConn);
        if (!pLobby)
            return;

        if (!NetworkMonitorModule::s_has111051_)
        {
            spdlog::error("AutoRelogin: 111051 data lost");
            s_state_    = kIdle;
            s_lastTick_ = now;
            return;
        }

        spdlog::info("AutoRelogin: char-select detected, sending 111051 "
                     "({} bytes) after {}ms",
                     NetworkMonitorModule::s_captured111051Size_, elapsed);

        *reinterpret_cast<BYTE*>(s_base_ + kRVA_LoginPending) = 1;

        using LargeFn = int(__fastcall*)(void*, void*, int, void*, int);
        auto fn = reinterpret_cast<LargeFn>(s_base_ + kRVA_SendRawLargePacket);
        fn(reinterpret_cast<void*>(pLobby), nullptr,
           kPktChannelSelect,
           NetworkMonitorModule::s_captured111051_,
           NetworkMonitorModule::s_captured111051Size_);

        DWORD chanSrc = *reinterpret_cast<DWORD*>(s_base_ + kRVA_ChannelSrc);
        *reinterpret_cast<DWORD*>(s_base_ + kRVA_GameChannelData) = chanSrc - 1;

        DWORD idxSrc = *reinterpret_cast<DWORD*>(s_base_ + kRVA_ChannelIdxSrc);
        *reinterpret_cast<DWORD*>(s_base_ + kRVA_ChannelTable) = idxSrc - 1;

        *reinterpret_cast<BYTE*>(s_base_ + kRVA_EscMenuFlag)  = 0;
        *reinterpret_cast<BYTE*>(s_base_ + kRVA_byte_11A68EC) = 0;

        auto pPlayer = *reinterpret_cast<uintptr_t*>(s_base_ + kRVA_LocalPlayer);
        if (pPlayer)
            *reinterpret_cast<BYTE*>(pPlayer + 26904) = 0;

        s_state_    = kIdle;
        s_lastTick_ = now;
        spdlog::info("AutoRelogin: done, timer reset");
    }

    // ── Render ──────────────────────────────────────────────────
    void OnRender() override
    {
        if (ImGui::Checkbox("Enable##AutoRelogin", &enabled_))
        {
            if (enabled_)
            {
                EnsureBase();
                s_lastTick_ = GetTickCount64();
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"定时自动返回角色选择再进入游戏，刷新GG认证计时器");

        if (!enabled_)
            return;

        ImGui::SliderFloat(u8"间隔(分钟)", &s_intervalMin_, 5.0f, 60.0f, "%.0f");

        PollState();

        ULONGLONG now = GetTickCount64();

        if (s_state_ == kWaitingCharSel)
        {
            ULONGLONG elapsed = now - s_stateStart_;
            float sec = elapsed / 1000.0f;
            if (elapsed < kStep2MinWaitMs)
                ImGui::TextColored(ImVec4(1, 1, 0, 1),
                                   u8"等待角色选择... (%.1fs / %.0fs)",
                                   sec, kStep2MinWaitMs / 1000.0f);
            else
                ImGui::TextColored(ImVec4(0, 1, 0, 1),
                                   u8"检测中... (%.1fs)", sec);
        }
        else
        {
            ULONGLONG elapsedMs  = now - s_lastTick_;
            ULONGLONG intervalMs = static_cast<ULONGLONG>(s_intervalMin_ * 60000.0f);
            ULONGLONG remaining  = (elapsedMs < intervalMs)
                                       ? (intervalMs - elapsedMs)
                                       : 0;

            int rMin = static_cast<int>(remaining / 60000);
            int rSec = static_cast<int>((remaining % 60000) / 1000);

            if (remaining > 60000)
                ImGui::Text(u8"下次重连: %02d:%02d", rMin, rSec);
            else if (remaining > 0)
                ImGui::TextColored(ImVec4(1, 1, 0, 1),
                                   u8"下次重连: %02d:%02d", rMin, rSec);
            else
                ImGui::TextColored(ImVec4(1, 0, 0, 1), u8"即将重连...");

            if (remaining == 0)
                DoRelogin();

            ImGui::SameLine();
            if (ImGui::Button(u8"立即重连"))
                DoRelogin();
        }

        ImGui::Separator();
        if (NetworkMonitorModule::s_has111051_)
            ImGui::TextColored(ImVec4(0, 1, 0, 1),
                               u8"111051 已捕获 (%d bytes)",
                               NetworkMonitorModule::s_captured111051Size_);
        else
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1),
                               u8"等待111051（首次手动进入后自动获取）");
    }

    void OnShutdown() override {}
};
