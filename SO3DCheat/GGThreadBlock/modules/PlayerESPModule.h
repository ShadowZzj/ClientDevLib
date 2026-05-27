#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace GGTB
{

class PlayerESPModule : public IModule
{
  public:
    PlayerESPModule() : IModule(u8"玩家屏幕标记 (ESP)") {}

    bool CanAutoPause() const override { return false; }

    void OnRender() override
    {
        ImGui::Checkbox(u8"启用##ESP", &enabled_);
        ImGui::SameLine();
        ImGui::TextDisabled(u8"(在附近玩家列表中勾选要标记的人)");

        if (!enabled_)
            return;

        RenderOverlay();
    }

    bool IsMarked(const std::string &name) const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return marked_.count(name) > 0;
    }

    void SetMarked(const std::string &name, bool v)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (v)
            marked_.insert(name);
        else
            marked_.erase(name);
    }

    void UpdateActiveEntries(const std::vector<NearbyPlayer> &players)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        activeEntries_.clear();
        for (auto &p : players)
        {
            if (marked_.count(p.name))
                activeEntries_.push_back({p.name, p.x, p.y, p.z, p.distance});
        }
    }

    // --- persistence ---
    std::string ConfigKey() const override { return "ESP"; }

    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        std::lock_guard<std::mutex> lk(mutex_);
        nlohmann::json arr = nlohmann::json::array();
        for (auto &n : marked_)
            arr.push_back(n);
        j["marked"] = std::move(arr);
    }

    void LoadState(const nlohmann::json &j) override
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (j.contains("marked") && j["marked"].is_array())
            {
                marked_.clear();
                for (auto &v : j["marked"])
                    if (v.is_string())
                        marked_.insert(v.get<std::string>());
            }
        }
        IModule::LoadState(j);
    }

  private:
    struct MarkedEntry
    {
        std::string name;
        float       x, y, z;
        float       distance;
    };

    mutable std::mutex              mutex_;
    std::unordered_set<std::string> marked_;
    std::vector<MarkedEntry>        activeEntries_;

    static bool WorldToScreen(float wx, float wy, float wz, float &sx, float &sy)
    {
        uintptr_t renderer = *(uintptr_t *)0x189DC08;
        if (!renderer)
            return false;

        float *view = (float *)(renderer + 0x1D0);
        float *proj = (float *)(renderer + 0x190);

        float vp[16];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                vp[i * 4 + j] = view[i * 4 + 0] * proj[0 * 4 + j] +
                                 view[i * 4 + 1] * proj[1 * 4 + j] +
                                 view[i * 4 + 2] * proj[2 * 4 + j] +
                                 view[i * 4 + 3] * proj[3 * 4 + j];

        float cw = wx * vp[3] + wy * vp[7] + wz * vp[11] + vp[15];
        if (cw <= 0.001f)
            return false;

        float cx = wx * vp[0] + wy * vp[4] + wz * vp[8] + vp[12];
        float cy = wx * vp[1] + wy * vp[5] + wz * vp[9] + vp[13];

        float ndcX = cx / cw;
        float ndcY = cy / cw;

        auto &io = ImGui::GetIO();
        sx = (ndcX + 1.0f) * 0.5f * io.DisplaySize.x;
        sy = (1.0f - ndcY) * 0.5f * io.DisplaySize.y;
        return true;
    }

    void RenderOverlay()
    {
        std::vector<MarkedEntry> entries;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            entries = activeEntries_;
        }

        if (entries.empty())
            return;

        auto *drawList = ImGui::GetBackgroundDrawList();
        auto &io = ImGui::GetIO();
        char buf[128];

        // 屏幕中间上方显示标记玩家列表
        float centerX = io.DisplaySize.x * 0.5f;
        float startY = 50.0f;
        float lineHeight = 25.0f;

        for (size_t i = 0; i < entries.size(); ++i)
        {
            auto &e = entries[i];
            snprintf(buf, sizeof(buf), u8"出现标记玩家：%s (%.0f)", e.name.c_str(), e.distance);

            ImVec2 textSize = ImGui::CalcTextSize(buf);
            float textX = centerX - textSize.x * 0.5f;
            float textY = startY + i * lineHeight;

            // 背景框
            drawList->AddRectFilled(
                ImVec2(textX - 10, textY - 5),
                ImVec2(textX + textSize.x + 10, textY + textSize.y + 5),
                IM_COL32(0, 0, 0, 180));

            // 边框
            drawList->AddRect(
                ImVec2(textX - 10, textY - 5),
                ImVec2(textX + textSize.x + 10, textY + textSize.y + 5),
                IM_COL32(255, 80, 80, 255), 0.0f, 0, 2.0f);

            // 文字
            drawList->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 80, 255), buf);
        }

        // 在世界坐标位置绘制标记
        for (auto &e : entries)
        {
            float sx, sy;
            if (!WorldToScreen(e.x, e.y, e.z, sx, sy))
                continue;

            if (sx < 0 || sx > io.DisplaySize.x || sy < 0 || sy > io.DisplaySize.y)
                continue;

            drawList->AddCircleFilled(ImVec2(sx, sy), 5.0f, IM_COL32(255, 50, 50, 220));

            snprintf(buf, sizeof(buf), "%s (%.0f)", e.name.c_str(), e.distance);

            // black outline for readability
            drawList->AddText(ImVec2(sx + 9, sy - 7), IM_COL32(0, 0, 0, 255), buf);
            drawList->AddText(ImVec2(sx + 7, sy - 9), IM_COL32(0, 0, 0, 255), buf);
            drawList->AddText(ImVec2(sx + 8, sy - 6), IM_COL32(0, 0, 0, 255), buf);
            drawList->AddText(ImVec2(sx + 8, sy - 8), IM_COL32(255, 80, 80, 255), buf);
        }
    }
};

} // namespace GGTB
