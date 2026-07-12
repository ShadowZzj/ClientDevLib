#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "ScreenOverlay.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace GGTB
{

// 打开附近玩家的个人摆摊 (CG 411042)。
//
// 勾选后按 Ctrl+L 打开「最近」的摆摊玩家的店铺(faithful 点击路径,见
// CLocalPlayer.cpp::OpenStreetStall —— 走引擎自身发送/UI call,不裸发包,不卡移动)。
// 同时把周围正在摆摊的玩家(店铺名/距离/类型)列出来,可逐个手动打开。
//
// 扫描和 NPG 一样跑在独立 worker 线程上:菜单收起 / CollapsingHeader 折叠时 OnRender
// 不再被调,但热键仍需要最新快照,所以不能把扫描放在 OnRender 里。
class OpenNearbyStallModule : public IModule
{
  public:
    OpenNearbyStallModule() : IModule(u8"打开最近摆摊 (Ctrl+L)")
    {
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~OpenNearbyStallModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    // 开/关摊位本身不是作弊行为(就是正常逛街买东西),不需要被「附近玩家自动停手」暂停。
    bool CanAutoPause() const override { return false; }
    Tab  GetTab() const override { return Tab::Common; }

    void OnRender() override
    {
        ImGui::Checkbox(u8"打开最近摆摊##OpenStall", &enabled_);
        ImGui::SameLine();
        ImGui::TextDisabled(u8"(勾选后按 Ctrl+L 打开最近摊位)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"勾选启用:扫描周围正在摆个人摊的玩家;按 Ctrl+L 打开下表最近的一个。");

        // 手动打开:不依赖扫描勾选,直接用已知 userId + 类型访摊(目标仍须在 AOI 内摆摊)。
        ImGui::Separator();
        ImGui::TextDisabled(u8"手动打开:");
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt(u8"userId##OpenStallManual", &manualUserId_, 0, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt(u8"类型##OpenStallManual", &manualStallType_, 0, 0);
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"打开##OpenStallManual"))
            OpenManual();

        if (!enabled_)
        {
            ImGui::TextDisabled(u8"未启用 (勾选后开始扫描周围摊位)");
            return;
        }

        ImGui::SliderFloat(u8"扫描距离##OpenStall", &distanceThreshold_, 5.0f, 200.0f, "%.0f");
        ImGui::SliderInt(u8"轮询间隔(ms)##OpenStall", &pollIntervalMs_, 200, 5000);

        std::vector<StallPlayer> snapshot;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            snapshot = lastStalls_;
        }

        ImGui::Separator();
        ImGui::Text(u8"周围摊位 (%d):", static_cast<int>(snapshot.size()));

        constexpr ImGuiTableFlags kFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;

        if (ImGui::BeginTable("##OpenStallList", 6, kFlags, ImVec2(0, 180)))
        {
            ImGui::TableSetupColumn(u8"摊主");
            ImGui::TableSetupColumn(u8"userId");
            ImGui::TableSetupColumn(u8"店铺名");
            ImGui::TableSetupColumn(u8"距离");
            ImGui::TableSetupColumn(u8"类型");
            ImGui::TableSetupColumn(u8"操作");
            ImGui::TableHeadersRow();

            for (const auto &s : snapshot)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(s.name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%u", s.userId);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(s.stallName.empty() ? u8"(无名)" : s.stallName.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", s.distance);
                ImGui::TableNextColumn();
                ImGui::Text("%u", s.stallType);
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(s.userId));
                if (ImGui::SmallButton(u8"打开"))
                    OpenOne(s);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    // 由 Ctrl+L 热键(dllmain)调用:打开当前快照里距离最近的摊位。返回是否成功发出。
    // enabled_==false 时不动作(热键被勾选框 gate)。
    bool OpenNearest()
    {
        if (!enabled_)
            return false;

        StallPlayer nearest;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (lastStalls_.empty())
            {
                ScreenOverlay::ShowToast(u8"附近没有摆摊的玩家", 2500);
                return false;
            }
            nearest = lastStalls_.front(); // worker 已按距离升序排好
        }
        return OpenOne(nearest);
    }

    std::string ConfigKey() const override { return "OpenStall"; }

    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["distanceThreshold"] = distanceThreshold_;
        j["pollIntervalMs"]    = pollIntervalMs_;
    }

    void LoadState(const nlohmann::json &j) override
    {
        distanceThreshold_ = j.value("distanceThreshold", distanceThreshold_);
        // 旧版本默认 800,远超真实摊距(实测最远 ~50),夹回新滑块范围避免列表把全图都算进来。
        if (distanceThreshold_ < 5.0f)   distanceThreshold_ = 5.0f;
        if (distanceThreshold_ > 200.0f) distanceThreshold_ = 200.0f;
        pollIntervalMs_    = j.value("pollIntervalMs",    pollIntervalMs_);
        enabled_           = j.value("enabled", false);
    }

  private:
    void OpenManual()
    {
        if (manualUserId_ <= 0)
        {
            ScreenOverlay::ShowToast(u8"请输入有效的 userId", 2500);
            return;
        }
        uint32_t uid  = static_cast<uint32_t>(manualUserId_);
        uint32_t type = manualStallType_ < 0 ? 0u : static_cast<uint32_t>(manualStallType_);
        bool     ok   = OpenStreetStall(uid, type);
        std::string toast =
            (ok ? u8"已打开摊位 userId=" : u8"打开摊位失败 userId=") + std::to_string(uid);
        ScreenOverlay::ShowToast(toast.c_str(), 3000);
        spdlog::info("GGTB::OpenStall(manual): userId={} type={} -> {}", uid, type, ok ? "ok" : "fail");
    }

    bool OpenOne(const StallPlayer &s)
    {
        bool ok = OpenStreetStall(s.userId, s.stallType);
        std::string label = s.stallName.empty() ? s.name : (s.name + u8" 的「" + s.stallName + u8"」");
        std::string toast = (ok ? u8"已打开摊位:" : u8"打开摊位失败:") + label;
        ScreenOverlay::ShowToast(toast.c_str(), 3000);
        spdlog::info("GGTB::OpenStall: open '{}' userId={} type={} -> {}",
                     s.name, s.userId, s.stallType, ok ? "ok" : "fail");
        return ok;
    }

    void WorkerLoop()
    {
        spdlog::info("GGTB::OpenStall: worker thread started");
        while (!stop_.load())
        {
            if (enabled_)
                Tick();
            for (int i = 0; i < 10 && !stop_.load(); ++i)
                Sleep(10);
        }
        spdlog::info("GGTB::OpenStall: worker thread exited");
    }

    void Tick()
    {
        DWORD now = GetTickCount();
        if (now - lastCheckMs_ < static_cast<DWORD>(pollIntervalMs_))
            return;
        lastCheckMs_ = now;

        auto stalls = GetNearbyStallPlayers(distanceThreshold_); // 已按距离升序

        std::lock_guard<std::mutex> lk(mutex_);
        lastStalls_ = std::move(stalls);
    }

    float                    distanceThreshold_ = 60.0f;
    int                      pollIntervalMs_    = 800;
    DWORD                    lastCheckMs_       = 0;

    int                      manualUserId_      = 0;
    int                      manualStallType_   = 0;

    std::mutex               mutex_;
    std::vector<StallPlayer> lastStalls_;

    std::atomic<bool>        stop_{false};
    std::thread              worker_;
};

} // namespace GGTB
