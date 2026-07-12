#pragma once
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <string>

namespace GGTB
{

// 常驻屏幕 overlay：中部提示文字 (ShowToast)。火力全开热键 N 现在切换的是
// 「多重施法」,切换时弹一行「多重施法：开/关」,持续 durationMs 后淡出消失。
// 用全局 atomic 存到期时间戳 + mutex 存文本 —— dllmain 的热键线程写,
// D3D9 UI 线程 (Setting::Render) 读。
//
// 画在 Setting::Render 末尾(ImGui::End() 之后),用 GetForegroundDrawList 直接
// 画到最上层,不进任何标签页/子窗口。
//
// (樂園鎮 / 獅子城 回城按钮已移到「传送」标签页,见 TeleportTownModule.h)
namespace ScreenOverlay
{

inline std::mutex   g_toastMutex;
inline std::string  g_toastText;
inline std::atomic<DWORD> g_toastExpiryTick{0}; // GetTickCount() 到期点,0=无

// dllmain 热键线程调用:显示一行居中提示,持续 durationMs 毫秒。
inline void ShowToast(const char *text, DWORD durationMs)
{
    {
        std::lock_guard<std::mutex> lk(g_toastMutex);
        g_toastText = text ? text : "";
    }
    g_toastExpiryTick.store(GetTickCount() + durationMs, std::memory_order_release);
}

// Setting::Render 末尾调用 (在 ImGui::End() 之后),把 overlay 画在最上层、
// 固定位置,不受模块列表滚动影响。
inline void Render()
{
    ImGuiIO &io = ImGui::GetIO();

    // ---- 中部提示文字 ----
    DWORD expiry = g_toastExpiryTick.load(std::memory_order_acquire);
    if (expiry == 0)
        return;
    DWORD now = GetTickCount();
    // GetTickCount 32-bit wrap 用差值符号判断到期,抗 ~49.7 天回绕。
    if (static_cast<int32_t>(now - expiry) >= 0)
    {
        g_toastExpiryTick.store(0, std::memory_order_release);
        return;
    }

    std::string text;
    {
        std::lock_guard<std::mutex> lk(g_toastMutex);
        text = g_toastText;
    }
    if (text.empty())
        return;

    // 最后 500ms 做个线性淡出,不突然消失。
    float alpha = 1.0f;
    DWORD remain = expiry - now;
    if (remain < 500)
        alpha = static_cast<float>(remain) / 500.0f;

    auto *drawList = ImGui::GetForegroundDrawList();
    ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    float textX = io.DisplaySize.x * 0.5f - textSize.x * 0.5f;
    float textY = io.DisplaySize.y * 0.5f - textSize.y * 0.5f;

    ImU32 bgCol  = IM_COL32(0, 0, 0, static_cast<int>(180 * alpha));
    ImU32 brdCol = IM_COL32(255, 200, 60, static_cast<int>(255 * alpha));
    ImU32 txtCol = IM_COL32(255, 255, 255, static_cast<int>(255 * alpha));

    drawList->AddRectFilled(ImVec2(textX - 16, textY - 8),
                            ImVec2(textX + textSize.x + 16, textY + textSize.y + 8),
                            bgCol, 4.0f);
    drawList->AddRect(ImVec2(textX - 16, textY - 8),
                      ImVec2(textX + textSize.x + 16, textY + textSize.y + 8),
                      brdCol, 4.0f, 0, 2.0f);
    drawList->AddText(ImVec2(textX, textY), txtCol, text.c_str());
}

} // namespace ScreenOverlay
} // namespace GGTB
