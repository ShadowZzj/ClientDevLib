#pragma once
#include "../IModule.h"
#include "../util/PatternResolver.h"
#include <Windows.h>
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <vector>

class GGBypassModule : public IModule
{
  public:
    // =================================================================
    //  Early-patch: safe to call from DllMain (DLL_PROCESS_ATTACH).
    //  Uses hardcoded fallback RVAs — no PatternResolver, no file I/O.
    //  This runs while the loader lock is held, so the main thread's
    //  GameGuard checks cannot race with the byte writes.
    // =================================================================

    static constexpr int kEarlyCount = 6;

    struct EarlyEntry
    {
        uintptr_t rva;
        BYTE patchBytes[8];
        BYTE origBytes[8];
        BYTE size;
        bool applied;
    };

    static inline uintptr_t s_earlyBase_ = 0;
    static inline EarlyEntry s_early_[kEarlyCount] = {
        {0x4BE30,  {0xC3},                                           {}, 1, false},
        {0x8D8BB0, {0xB8, 0x55, 0x07, 0x00, 0x00, 0xC3},           {}, 6, false},
        {0x8D8F80, {0xB8, 0x55, 0x07, 0x00, 0x00, 0xC3},           {}, 6, false},
        {0x6300B7, {0x90, 0x90},                                     {}, 2, false},
        {0x607570, {0x33, 0xC0, 0xC2, 0x04, 0x00},                  {}, 5, false},
        {0x5822B0, {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC2, 0x04, 0x00},{}, 8, false},
    };

    // GG Live Auth mode: let GameGuard init & handle auth natively.
    // Skip Patch 0 (GG init blocker) and Patch 5 (auth packet blocker).
    // GG detects our cheats but Patches 1-4 suppress the error callbacks.
    static inline bool s_liveAuth_ = false;

    static void EarlyPatch()
    {
        s_earlyBase_ = reinterpret_cast<uintptr_t>(GetModuleHandleA("SO3D.exe"));
        if (!s_earlyBase_)
            return;

        for (int i = 0; i < kEarlyCount; ++i)
        {
            // Live Auth mode: skip Patch 0 (GG init) and Patch 5 (auth packet)
            // so GameGuard can initialize and handle auth natively.
            if (s_liveAuth_ && (i == 0 || i == 5))
                continue;

            auto &e    = s_early_[i];
            auto *addr = reinterpret_cast<void *>(s_earlyBase_ + e.rva);
            DWORD oldProt = 0;
            if (!VirtualProtect(addr, e.size, PAGE_EXECUTE_READWRITE, &oldProt))
                continue;
            std::memcpy(e.origBytes, addr, e.size);
            if (std::memcmp(addr, e.patchBytes, e.size) != 0)
                std::memcpy(addr, e.patchBytes, e.size);
            VirtualProtect(addr, e.size, oldProt, &oldProt);
            e.applied = true;
        }
    }

    // =================================================================
    //  Normal module interface (runs later in HackThread)
    // =================================================================

    GGBypassModule() : IModule("GG Bypass") {}

    bool CanAutoPause() const override { return false; }

    /// Called in HackThread after PatternResolver::Init().
    /// Adopts early-patched entries if addresses match, applies any remaining.
    void AdoptOrApply()
    {
        if (patches_.empty())
            InitPatches();

        int ok = 0;
        for (size_t i = 0; i < patches_.size(); ++i)
        {
            auto &p = patches_[i];

            // Live Auth: skip Patch 0 and 5 — GG handles auth natively
            if (s_liveAuth_ && (i == 0 || i == 5))
            {
                spdlog::info("GGBypass: {} skipped (Live Auth mode)", p.label);
                continue;
            }

            if (p.applied)
                ++ok;
            else if (ApplyPatch(p))
                ++ok;
        }

        enabled_ = true;
        spdlog::info("GGBypass: {}/{} patches active (liveAuth={})",
                     ok, patches_.size(), s_liveAuth_);
    }

    void OnRender() override
    {
        if (ImGui::Checkbox("Enable##GGBypass", &enabled_))
        {
            if (enabled_)
            {
                for (size_t i = 0; i < patches_.size(); ++i)
                {
                    if (s_liveAuth_ && (i == 0 || i == 5))
                        continue;
                    ApplyPatch(patches_[i]);
                }
            }
            else
            {
                for (auto &p : patches_)
                    RestorePatch(p);
            }
            spdlog::info("GGBypass {}", enabled_ ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"动态绕过 GameGuard 保护");

        if (s_liveAuth_)
            ImGui::TextColored(ImVec4(0, 1, 0.5f, 1),
                              u8"Live Auth: GG 进程处理认证 (P0/P5 已跳过)");

        if (ImGui::BeginTable("##gg_patches", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn(u8"补丁", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(u8"地址", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(u8"状态", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableHeadersRow();

            for (const auto &p : patches_)
            {
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(p.label.c_str());
                if (ImGui::IsItemHovered() && !p.tooltip.empty())
                    ImGui::SetTooltip("%s", p.tooltip.c_str());

                ImGui::TableNextColumn();
                if (p.address)
                    ImGui::Text("%08X", static_cast<unsigned>(p.address));
                else
                    ImGui::TextDisabled("--------");

                ImGui::TableNextColumn();
                if (p.applied)
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "OK");
                else if (p.address == 0)
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "N/A");
                else
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "OFF");
            }
            ImGui::EndTable();
        }
    }

    void OnShutdown() override
    {
        for (auto &p : patches_)
            RestorePatch(p);
        spdlog::info("GGBypass: all patches restored");
    }

    void OnResume() override
    {
        if (enabled_)
        {
            for (size_t i = 0; i < patches_.size(); ++i)
            {
                if (s_liveAuth_ && (i == 0 || i == 5))
                    continue;
                ApplyPatch(patches_[i]);
            }
        }
    }

  private:
    struct PatchInfo
    {
        std::string resolverName;
        std::string label;
        std::string tooltip;
        std::vector<BYTE> patchBytes;
        std::vector<BYTE> origBackup;
        uintptr_t address = 0;
        bool applied       = false;
    };

    std::vector<PatchInfo> patches_;

    void InitPatches()
    {
        // clang-format off
        patches_ = {
            { "GG_StaticInit",    u8"P0: 阻止GG启动",
              u8"函数入口 ret — 阻止 GameGuard 进程启动 (CRT静态初始化)",
              {0xC3},
              {}, 0, false },

            { "GG_RuntimeCheck",  u8"P1: 运行时检查",
              u8"mov eax,0x755; ret — RuntimeCheck 强制返回 1877, 防止 NpCheck 弹窗",
              {0xB8, 0x55, 0x07, 0x00, 0x00, 0xC3},
              {}, 0, false },

            { "GG_GetResult",     u8"P2: 初始化结果",
              u8"mov eax,0x755; ret — GetResult 强制返回 1877, WinMain 判断通过",
              {0xB8, 0x55, 0x07, 0x00, 0x00, 0xC3},
              {}, 0, false },

            { "GG_ExitJnz",       u8"P3: 退出检查",
              u8"jnz → nop nop — 禁用游戏循环中 g_bGameGuardExit 退出跳转",
              {0x90, 0x90},
              {}, 0, false },

            { "GG_ErrorMsgBox",   u8"P4: 错误弹窗",
              u8"xor eax,eax; ret 4 — 屏蔽 GameGuard 错误 MessageBox",
              {0x33, 0xC0, 0xC2, 0x04, 0x00},
              {}, 0, false },

            { "GG_PacketHandler", u8"P5: 服务器认证包",
              u8"mov eax,1; ret 4 — 忽略服务器下发的 GG 认证包",
              {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC2, 0x04, 0x00},
              {}, 0, false },
        };
        // clang-format on

        for (size_t i = 0; i < patches_.size(); ++i)
        {
            auto &p = patches_[i];
            p.address = SO3D::PatternResolver::Get(p.resolverName);
            p.origBackup.resize(p.patchBytes.size(), 0);

            if (i >= kEarlyCount || !s_earlyBase_)
                continue;

            auto &e = s_early_[i];
            uintptr_t earlyAddr = s_earlyBase_ + e.rva;

            if (e.applied && earlyAddr == p.address)
            {
                std::memcpy(p.origBackup.data(), e.origBytes, p.patchBytes.size());
                p.applied = true;
                spdlog::info("GGBypass: {} adopted early patch at {:x}", p.label, p.address);
            }
            else if (e.applied && earlyAddr != p.address && p.address != 0)
            {
                RestoreEarly(i);
                spdlog::warn("GGBypass: {} early addr {:x} != resolved {:x}, re-applying",
                             p.label, earlyAddr, p.address);
            }
        }
    }

    static void RestoreEarly(int idx)
    {
        auto &e      = s_early_[idx];
        auto *addr   = reinterpret_cast<void *>(s_earlyBase_ + e.rva);
        DWORD oldProt = 0;
        if (VirtualProtect(addr, e.size, PAGE_EXECUTE_READWRITE, &oldProt))
        {
            std::memcpy(addr, e.origBytes, e.size);
            VirtualProtect(addr, e.size, oldProt, &oldProt);
            e.applied = false;
        }
    }

    bool ApplyPatch(PatchInfo &p)
    {
        if (p.applied || p.address == 0)
            return false;

        const size_t len = p.patchBytes.size();
        void *addr       = reinterpret_cast<void *>(p.address);

        DWORD oldProt = 0;
        if (!VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProt))
        {
            spdlog::error("GGBypass: VirtualProtect failed for {} at {:x}", p.label, p.address);
            return false;
        }

        std::memcpy(p.origBackup.data(), addr, len);

        if (std::memcmp(addr, p.patchBytes.data(), len) == 0)
        {
            VirtualProtect(addr, len, oldProt, &oldProt);
            p.applied = true;
            spdlog::info("GGBypass: {} already patched at {:x}", p.label, p.address);
            return true;
        }

        std::memcpy(addr, p.patchBytes.data(), len);
        VirtualProtect(addr, len, oldProt, &oldProt);

        p.applied = true;
        spdlog::info("GGBypass: {} applied at {:x}", p.label, p.address);
        return true;
    }

    bool RestorePatch(PatchInfo &p)
    {
        if (!p.applied || p.address == 0)
            return false;

        const size_t len = p.origBackup.size();
        void *addr       = reinterpret_cast<void *>(p.address);

        DWORD oldProt = 0;
        if (!VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProt))
            return false;

        std::memcpy(addr, p.origBackup.data(), len);
        VirtualProtect(addr, len, oldProt, &oldProt);

        p.applied = false;
        spdlog::info("GGBypass: {} restored at {:x}", p.label, p.address);
        return true;
    }
};
