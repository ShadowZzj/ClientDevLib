#pragma once
#include "../IModule.h"
#include "../Setting.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace GGTB
{

// Auto-pause guard.
//
// Polling MUST run independent of UI: D3D9Hook only renders when the menu is
// open AND the user hasn't collapsed our CollapsingHeader, so a Tick() inside
// OnRender() would freeze the moment the user closes the menu — defeating the
// whole "auto-pause when someone is nearby" point.
//
// So scanning lives on a dedicated worker thread, started in the ctor and
// stopped in the dtor. UI thread only reads the snapshot under `mutex_`.
class NearbyPlayerGuardModule : public IModule
{
  public:
    explicit NearbyPlayerGuardModule(Setting *setting)
        : IModule(u8"附近玩家自动停手"), setting_(setting)
    {
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~NearbyPlayerGuardModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    bool CanAutoPause() const override { return false; }

    void OnRender() override
    {
        bool wasEnabled = enabled_;
        ImGui::Checkbox(u8"启用##NPG", &enabled_);
        ImGui::SameLine();
        ImGui::Checkbox(u8"显示玩家列表##NPG", &showList_);
        ImGui::SliderFloat(u8"距离阈值##NPG", &distanceThreshold_, 50.0f, 2000.0f, "%.0f");
        ImGui::SliderInt(u8"轮询间隔(ms)##NPG", &pollIntervalMs_, 100, 5000);

        // Disabling mid-pause: resume immediately so user isn't stuck.
        if (wasEnabled && !enabled_)
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (nearby_)
            {
                ResumeAllLocked();
                nearby_ = false;
                spdlog::info("GGTB::NPG: disabled, force-resumed paused modules");
            }
        }

        // Snapshot for UI under lock.
        size_t      playerCount = 0;
        size_t      pausedCount = 0;
        bool        nearbyNow   = false;
        std::string nearestName;
        float       nearestDist = 0.0f;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            playerCount = lastPlayers_.size();
            pausedCount = autoPaused_.size();
            nearbyNow   = nearby_;
            nearestName = lastNearestName_;
            nearestDist = lastNearestDist_;
        }

        ImGui::Separator();
        if (!enabled_)
            ImGui::TextDisabled(u8"未启用 (worker 仍在运行,但不扫描)");
        else if (nearbyNow)
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                               u8"附近有玩家 (%d, 最近=%s @ %.1f), 已暂停 %d 个模块",
                               static_cast<int>(playerCount),
                               nearestName.c_str(), nearestDist,
                               static_cast<int>(pausedCount));
        else
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), u8"周围无人");

        if (pausedCount > 0)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"强制恢复"))
            {
                std::lock_guard<std::mutex> lk(mutex_);
                ResumeAllLocked();
                nearby_ = false;
            }
        }

        if (showList_)
            RenderListSnapshot();
    }

    void OnShutdown() override
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (nearby_)
        {
            ResumeAllLocked();
            nearby_ = false;
        }
    }

    void OnResume() override {}

  private:
    void RenderListSnapshot()
    {
        std::vector<NearbyPlayer> snapshot;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            snapshot = lastPlayers_;
        }

        ImGui::Separator();
        ImGui::Text(u8"周围玩家 (%d):", static_cast<int>(snapshot.size()));

        constexpr ImGuiTableFlags kFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;

        if (ImGui::BeginTable("##NPGList", 5, kFlags, ImVec2(0, 160)))
        {
            ImGui::TableSetupColumn(u8"名字");
            ImGui::TableSetupColumn(u8"距离");
            ImGui::TableSetupColumn("X");
            ImGui::TableSetupColumn("Y");
            ImGui::TableSetupColumn("Z");
            ImGui::TableHeadersRow();

            for (auto &p : snapshot)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(p.name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", p.distance);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", p.x);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", p.y);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", p.z);
            }
            ImGui::EndTable();
        }
    }

    void WorkerLoop()
    {
        spdlog::info("GGTB::NPG: worker thread started");
        DWORD lastHeartbeat = 0;
        while (!stop_.load())
        {
            if (enabled_)
                Tick();

            // Heartbeat every ~10s while enabled — proves the worker is alive
            // even when the user has the menu closed.
            DWORD now = GetTickCount();
            if (enabled_ && now - lastHeartbeat > 10000)
            {
                size_t cnt = 0;
                bool   nb  = false;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    cnt = lastPlayers_.size();
                    nb  = nearby_;
                }
                spdlog::info("GGTB::NPG: heartbeat — nearby={}, count={}", nb, cnt);
                lastHeartbeat = now;
            }

            // Sleep small slices so stop_ is responsive (max ~100ms latency on shutdown).
            for (int i = 0; i < 10 && !stop_.load(); ++i)
                Sleep(10);
        }
        spdlog::info("GGTB::NPG: worker thread exited");
    }

    void Tick()
    {
        DWORD now = GetTickCount();
        if (now - lastCheckMs_ < static_cast<DWORD>(pollIntervalMs_))
            return;
        lastCheckMs_ = now;

        std::string localName = GetLocalPlayerName();
        auto        players   = GetAroundPlayers(localName, distanceThreshold_);

        std::sort(players.begin(), players.end(),
                  [](const NearbyPlayer &a, const NearbyPlayer &b) {
                      return a.distance < b.distance;
                  });

        bool sawSomeone = !players.empty();

        std::lock_guard<std::mutex> lk(mutex_);

        // Hysteresis: a single empty scan is unreliable even with the linked-list
        // walker. EntityManager / LocalUser pointers briefly read as 0 across
        // scene transitions and channel switches, and the kind==5 filter can
        // transiently miss nodes mid-mutation. Without the grace window, the UI
        // text and the auto-pause toggle thrash every poll.
        //
        // Rule: any non-empty scan immediately marks nearby=true (we want to react
        // fast). Empty scans only flip nearby=false after kClearAfterEmptyPolls
        // consecutive empties. lastPlayers_ is held during the grace window so the
        // UI list stays stable too.
        if (sawSomeone)
        {
            emptyPollStreak_ = 0;
            lastPlayers_     = std::move(players);
            lastNearestName_ = lastPlayers_.front().name;
            lastNearestDist_ = lastPlayers_.front().distance;

            if (!nearby_)
            {
                PauseAllLocked();
                nearby_ = true;
                spdlog::info("GGTB::NPG: nearby player detected ({} @ {:.1f}), paused {} modules",
                             lastNearestName_, lastNearestDist_, autoPaused_.size());
            }
        }
        else
        {
            ++emptyPollStreak_;
            if (emptyPollStreak_ < kClearAfterEmptyPolls)
                return; // grace window — keep prior snapshot and nearby_ state

            lastPlayers_.clear();
            lastNearestName_.clear();
            lastNearestDist_ = 0.0f;

            if (nearby_)
            {
                ResumeAllLocked();
                nearby_ = false;
                spdlog::info("GGTB::NPG: area clear ({} consecutive empty polls), resumed modules",
                             emptyPollStreak_);
            }
        }
    }

    void PauseAllLocked()
    {
        if (!setting_)
            return;
        for (auto &mod : setting_->GetModules())
        {
            if (!mod || mod.get() == this)
                continue;
            if (!mod->CanAutoPause())
                continue;
            if (!mod->IsEnabled())
                continue;
            mod->OnShutdown();
            autoPaused_.push_back(mod);
            spdlog::info("GGTB::NPG:   paused '{}'", mod->GetName());
        }
    }

    void ResumeAllLocked()
    {
        for (auto &weak : autoPaused_)
        {
            if (auto mod = weak.lock())
            {
                if (mod->IsEnabled())
                {
                    mod->OnResume();
                    spdlog::info("GGTB::NPG:   resumed '{}'", mod->GetName());
                }
            }
        }
        autoPaused_.clear();
    }

    static constexpr int kClearAfterEmptyPolls = 3;

    Setting                            *setting_           = nullptr;
    float                               distanceThreshold_ = 600.0f;
    int                                 pollIntervalMs_    = 500;
    DWORD                               lastCheckMs_       = 0;
    int                                 emptyPollStreak_   = 0;
    bool                                nearby_            = false;
    bool                                showList_          = false;

    std::mutex                          mutex_;
    std::vector<NearbyPlayer>           lastPlayers_;
    std::string                         lastNearestName_;
    float                               lastNearestDist_   = 0.0f;
    std::vector<std::weak_ptr<IModule>> autoPaused_;

    std::atomic<bool>                   stop_{false};
    std::thread                         worker_;
};

} // namespace GGTB
