#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
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

// Auto-pickup.
//
// Walks CItemContainer's drop list every `pollIntervalMs` and fires
// SendPickItemPacket(dropId) on each canPick==1 entry within `distance` of the
// local player. Picks at most `maxPicksPerTick` per cycle, with a small
// inter-packet sleep, to stay under the server's CG_ITEM_USE / CG_PICK_ITEM
// throttle and to dilute the burstiness that anti-cheat heuristics flag.
//
// Threading mirrors NearbyPlayerGuardModule: a dedicated worker started in the
// ctor / joined in the dtor — D3D9 EndScene only renders when the menu is open,
// and we want pickup to keep running with the menu closed. UI thread takes a
// snapshot under mutex_ for display only.
//
// NPG integration: `paused_by_guard_` flips via OnShutdown/OnResume so the
// worker silently skips picking while a remote player is nearby. `enabled_`
// stays untouched (user intent), and the worker re-engages on its own once NPG
// flips OnResume.
class AutoPickupModule : public IModule
{
  public:
    AutoPickupModule() : IModule(u8"自动拾取")
    {
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~AutoPickupModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    // NPG should be able to pause us — picking near a remote player is the
    // exact giveaway we're trying to hide.
    bool CanAutoPause() const override { return true; }

    void OnRender() override
    {
        ImGui::Checkbox(u8"启用##AutoPickup", &enabled_);
        ImGui::SameLine();
        ImGui::Checkbox(u8"显示掉落列表##AutoPickup", &showList_);

        ImGui::SliderFloat(u8"拾取距离##AutoPickup", &distance_,    50.0f, 1500.0f, "%.0f");
        ImGui::SliderInt  (u8"轮询间隔(ms)##AutoPickup", &pollIntervalMs_, 100, 3000);
        ImGui::SliderInt  (u8"每轮上限##AutoPickup",   &maxPicksPerTick_, 1, 20);

        // Snapshot for UI under lock.
        size_t      dropCount    = 0;
        size_t      picksLastTick = 0;
        DWORD       lastTickAge  = 0;
        std::vector<DropItemInfo> snapshot;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            dropCount    = lastDrops_.size();
            picksLastTick = picksLastTick_;
            lastTickAge  = lastTickMs_ ? (GetTickCount() - lastTickMs_) : 0;
            if (showList_)
                snapshot = lastDrops_;
        }

        ImGui::Separator();
        if (!enabled_)
            ImGui::TextDisabled(u8"未启用 (worker 仍在运行,但不拾取)");
        else if (paused_by_guard_.load())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               u8"已被 NPG 暂停 (附近有玩家)");
        else
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               u8"运行中: 周围 %d 件可拾取, 上轮拾取 %d, 距上轮 %lums",
                               static_cast<int>(dropCount),
                               static_cast<int>(picksLastTick),
                               static_cast<unsigned long>(lastTickAge));

        if (showList_)
            RenderListSnapshot(snapshot);
    }

    // NPG pause/resume — only flips paused_by_guard_, never enabled_.
    void OnShutdown() override { paused_by_guard_.store(true); }
    void OnResume()   override { paused_by_guard_.store(false); }

    // Persistence
    std::string ConfigKey() const override { return "AutoPickup"; }
    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["distance"]         = distance_;
        j["pollIntervalMs"]   = pollIntervalMs_;
        j["maxPicksPerTick"]  = maxPicksPerTick_;
        j["showList"]         = showList_;
    }
    void LoadState(const nlohmann::json &j) override
    {
        distance_        = j.value("distance",        distance_);
        pollIntervalMs_  = j.value("pollIntervalMs",  pollIntervalMs_);
        maxPicksPerTick_ = j.value("maxPicksPerTick", maxPicksPerTick_);
        showList_        = j.value("showList",        showList_);
        IModule::LoadState(j);
    }

  private:
    void RenderListSnapshot(const std::vector<DropItemInfo> &snapshot)
    {
        ImGui::Separator();
        ImGui::Text(u8"周围掉落 (%d):", static_cast<int>(snapshot.size()));

        constexpr ImGuiTableFlags kFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;

        if (ImGui::BeginTable("##AutoPickupList", 6, kFlags, ImVec2(0, 160)))
        {
            ImGui::TableSetupColumn("dropId");
            ImGui::TableSetupColumn("itemId");
            ImGui::TableSetupColumn(u8"距离");
            ImGui::TableSetupColumn("X");
            ImGui::TableSetupColumn("Y");
            ImGui::TableSetupColumn(u8"可拾取");
            ImGui::TableHeadersRow();

            for (auto &d : snapshot)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%u", d.dropId);
                ImGui::TableNextColumn(); ImGui::Text("%u", d.itemId);
                ImGui::TableNextColumn(); ImGui::Text("%.1f", d.distance);
                ImGui::TableNextColumn(); ImGui::Text("%.1f", d.x);
                ImGui::TableNextColumn(); ImGui::Text("%.1f", d.y);
                ImGui::TableNextColumn();
                if (d.canPick) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Y");
                else           ImGui::TextDisabled("N");
            }
            ImGui::EndTable();
        }
    }

    void WorkerLoop()
    {
        spdlog::info("GGTB::AutoPickup: worker thread started");
        DWORD lastHeartbeat = 0;
        while (!stop_.load())
        {
            if (enabled_ && !paused_by_guard_.load())
                Tick();

            DWORD now = GetTickCount();
            if (enabled_ && now - lastHeartbeat > 10000)
            {
                size_t cnt = 0;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    cnt = lastDrops_.size();
                }
                spdlog::info("GGTB::AutoPickup: heartbeat — drops={}, paused={}",
                             cnt, paused_by_guard_.load());
                lastHeartbeat = now;
            }

            // Sleep in small slices so stop_ is responsive on shutdown.
            for (int i = 0; i < 10 && !stop_.load(); ++i)
                Sleep(10);
        }
        spdlog::info("GGTB::AutoPickup: worker thread exited");
    }

    void Tick()
    {
        DWORD now = GetTickCount();
        if (now - lastTickMs_ < static_cast<DWORD>(pollIntervalMs_))
            return;
        lastTickMs_ = now;

        auto drops = GetNearbyDropItems(distance_, /*includeUnpickable=*/false);

        size_t picked = 0;
        for (auto &d : drops)
        {
            if (picked >= static_cast<size_t>(maxPicksPerTick_))
                break;
            int rv = SendPickItem(d.dropId);
            if (rv > 0)
            {
                ++picked;
                spdlog::info("GGTB::AutoPickup: picked dropId={} itemId={} d={:.1f}",
                             d.dropId, d.itemId, d.distance);
            }
            // Small gap between packets — engine's Net__BeginSend / sub_B2C930
            // path isn't strictly thread-safe vs the main render thread, and
            // bursty CG_PICK_ITEM is exactly what server throttles flag.
            Sleep(15);
        }

        std::lock_guard<std::mutex> lk(mutex_);
        lastDrops_     = std::move(drops);
        picksLastTick_ = picked;
    }

    float             distance_         = 600.0f;
    int               pollIntervalMs_   = 500;
    int               maxPicksPerTick_  = 5;
    bool              showList_         = false;

    DWORD             lastTickMs_       = 0;
    size_t            picksLastTick_    = 0;

    std::mutex                  mutex_;
    std::vector<DropItemInfo>   lastDrops_;

    std::atomic<bool> paused_by_guard_{false};
    std::atomic<bool> stop_{false};
    std::thread       worker_;
};

} // namespace GGTB
