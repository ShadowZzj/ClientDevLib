#pragma once
#include "../IModule.h"
#include "../Setting.h"
#include "../entity/CLocalPlayer.h"
#include "../util/UserConfig.h"
#include "PlayerESPModule.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
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
    explicit NearbyPlayerGuardModule(Setting *setting, PlayerESPModule *esp = nullptr)
        : IModule(u8"附近玩家自动停手"), setting_(setting), esp_(esp)
    {
        enabled_ = true;
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
        ImGui::BeginDisabled();
        ImGui::Checkbox(u8"启用##NPG", &enabled_);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(u8"只能通过 config.json 的 modules.NPG.enabled 修改");
        ImGui::SameLine();
        ImGui::Checkbox(u8"显示玩家列表##NPG", &showList_);
        ImGui::SliderFloat(u8"距离阈值##NPG", &distanceThreshold_, 50.0f, 2000.0f, "%.0f");
        ImGui::SliderInt(u8"轮询间隔(ms)##NPG", &pollIntervalMs_, 100, 5000);

        // Snapshot for UI under lock.
        size_t      playerCount = 0;
        bool        nearbyNow   = false;
        std::string nearestName;
        float       nearestDist = 0.0f;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            playerCount = lastPlayers_.size();
            nearbyNow   = nearby_;
            nearestName = lastNearestName_;
            nearestDist = lastNearestDist_;
        }
        // Count actively-held modules outside the lock — touches setting_ but
        // not our protected state. Each module's pausedByGuard_ is atomic.
        size_t pausedCount = CountGuardedModules();

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
        else
            FeedESP();

        RenderWhitelistEditor();
    }

    std::string ConfigKey() const override { return "NPG"; }
    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["distanceThreshold"] = distanceThreshold_;
        j["pollIntervalMs"]    = pollIntervalMs_;
        j["showList"]          = showList_;
    }
    void LoadState(const nlohmann::json &j) override
    {
        distanceThreshold_ = j.value("distanceThreshold", distanceThreshold_);
        pollIntervalMs_    = j.value("pollIntervalMs",    pollIntervalMs_);
        showList_          = j.value("showList",          showList_);

        bool want = j.value("enabled", true);
        if (want && !enabled_) { enabled_ = true; OnResume(); }
        else if (!want && enabled_) { enabled_ = false; OnShutdown(); }
        else { enabled_ = want; }
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
    // Feed ESP without rendering the list (called when showList_ is false).
    void FeedESP()
    {
        if (!esp_)
            return;
        std::vector<NearbyPlayer> snapshot;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            snapshot = lastPlayers_;
        }
        esp_->UpdateActiveEntries(snapshot);
    }

    void RenderListSnapshot()
    {
        std::vector<NearbyPlayer> snapshot;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            snapshot = lastPlayers_;
        }

        // Feed ESP module with current visible players every frame.
        if (esp_)
            esp_->UpdateActiveEntries(snapshot);

        ImGui::Separator();
        ImGui::Text(u8"周围玩家 (%d):", static_cast<int>(snapshot.size()));

        constexpr ImGuiTableFlags kFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;

        if (ImGui::BeginTable("##NPGList", 8, kFlags, ImVec2(0, 160)))
        {
            ImGui::TableSetupColumn(u8"名字");
            ImGui::TableSetupColumn(u8"距离");
            ImGui::TableSetupColumn(u8"职业");
            ImGui::TableSetupColumn("X");
            ImGui::TableSetupColumn("Y");
            ImGui::TableSetupColumn("Z");
            ImGui::TableSetupColumn(u8"操作");
            ImGui::TableSetupColumn(u8"标记");
            ImGui::TableHeadersRow();

            for (auto &p : snapshot)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(p.name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", p.distance);
                ImGui::TableNextColumn();
                if (!p.professionName.empty())
                    ImGui::Text("%s (%u)", p.professionName.c_str(), p.profession);
                else
                    ImGui::Text("%u", p.profession);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", p.x);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", p.y);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", p.z);
                ImGui::TableNextColumn();
                ImGui::PushID(p.name.c_str());
                if (ImGui::SmallButton(u8"+白名单"))
                    UserConfig::AddWhitelist(p.name);
                ImGui::TableNextColumn();
                {
                    bool marked = esp_ && esp_->IsMarked(p.name);
                    if (ImGui::Checkbox("##esp", &marked) && esp_)
                        esp_->SetMarked(p.name, marked);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    void RenderWhitelistEditor()
    {
        if (!ImGui::CollapsingHeader(u8"白名单管理##NPG"))
            return;

        ImGui::TextDisabled(u8"白名单中的角色不会被视为附近玩家 (用于自己多开)");

        static char inputBuf[64] = {};
        ImGui::SetNextItemWidth(220);
        ImGui::InputText(u8"角色名##NPGwlInput", inputBuf, sizeof(inputBuf));
        ImGui::SameLine();
        if (ImGui::Button(u8"添加##NPGwlAdd") && inputBuf[0])
        {
            UserConfig::AddWhitelist(inputBuf);
            inputBuf[0] = 0;
        }

        auto wl = UserConfig::GetWhitelist();
        ImGui::Text(u8"当前白名单 (%d):", static_cast<int>(wl.size()));
        for (auto &n : wl)
        {
            ImGui::PushID(n.c_str());
            ImGui::BulletText("%s", n.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"删除"))
                UserConfig::RemoveWhitelist(n);
            ImGui::PopID();
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
                spdlog::info("GGTB::NPG: nearby player detected ({} @ {:.1f})",
                             lastNearestName_, lastNearestDist_);
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
        // Mark every CanAutoPause module (enabled or not) so anything the user
        // turns on while we're holding the guard up defers the patch instead
        // of showing the cheat effect to the nearby player. OnShutdown is only
        // called on currently-enabled modules — the rest have nothing to undo
        // but still need the guard flag set so their checkbox/slider handlers
        // read !IsPausedByGuard() as false.
        for (auto &mod : setting_->GetModules())
        {
            if (!mod || mod.get() == this)
                continue;
            if (!mod->CanAutoPause())
                continue;
            mod->SetPausedByGuard(true);
            if (mod->IsEnabled())
            {
                mod->OnShutdown();
                spdlog::info("GGTB::NPG:   paused '{}'", mod->GetName());
            }
        }
    }

    void ResumeAllLocked()
    {
        if (!setting_)
            return;
        // Symmetric to PauseAllLocked: clear the guard flag on every tracked
        // module, and call OnResume only on those currently enabled. This
        // covers modules the user enabled DURING the guard window (which
        // deferred their patch) — OnResume will now actually apply them.
        for (auto &mod : setting_->GetModules())
        {
            if (!mod || mod.get() == this)
                continue;
            if (!mod->CanAutoPause())
                continue;
            mod->SetPausedByGuard(false);
            if (mod->IsEnabled())
            {
                mod->OnResume();
                spdlog::info("GGTB::NPG:   resumed '{}'", mod->GetName());
            }
        }
    }

    // UI helper: count how many guarded, enabled modules are currently held
    // down. Walks the setting's module list (each pausedByGuard_ is atomic).
    size_t CountGuardedModules() const
    {
        if (!setting_)
            return 0;
        size_t n = 0;
        for (auto &mod : setting_->GetModules())
        {
            if (!mod || mod.get() == this)
                continue;
            if (!mod->CanAutoPause())
                continue;
            if (mod->IsEnabled() && mod->IsPausedByGuard())
                ++n;
        }
        return n;
    }

    static constexpr int kClearAfterEmptyPolls = 3;

    Setting                            *setting_           = nullptr;
    PlayerESPModule                    *esp_               = nullptr;
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

    std::atomic<bool>                   stop_{false};
    std::thread                         worker_;
};

} // namespace GGTB
