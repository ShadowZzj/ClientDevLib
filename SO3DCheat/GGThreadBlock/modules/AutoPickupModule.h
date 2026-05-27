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

        ImGui::Checkbox(u8"按Z拾取##AutoPickup", &pickOnZ_);
        if (pickOnZ_)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                               u8"<- 不自动扫描,按住 Z 持续拾取");
        }

        ImGui::SliderFloat(u8"拾取距离(格)##AutoPickup", &distance_,       2.0f, 50.0f, "%.0f");
        ImGui::SliderInt  (u8"轮询间隔(ms)##AutoPickup", &pollIntervalMs_, 100, 3000);
        ImGui::SliderInt  (u8"每轮上限##AutoPickup",     &maxPicksPerTick_, 1, 20);
        ImGui::SliderInt  (u8"包间延时(ms)##AutoPickup", &perPickDelayMs_,  0, 500);

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
        else if (IsPausedByGuard())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               u8"已被 NPG 暂停 (附近有玩家)");
        else if (pickOnZ_)
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                               u8"按Z模式: 周围 %d 件可拾取(被动扫描), 上次按Z拾取 %d, 距上次%lums",
                               static_cast<int>(dropCount),
                               static_cast<int>(picksLastTick),
                               static_cast<unsigned long>(lastTickAge));
        else
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               u8"运行中: 周围 %d 件可拾取, 上轮拾取 %d, 距上轮 %lums",
                               static_cast<int>(dropCount),
                               static_cast<int>(picksLastTick),
                               static_cast<unsigned long>(lastTickAge));

        if (showList_)
            RenderListSnapshot(snapshot);
    }

    // NPG integration: pauses via the IModule::pausedByGuard_ flag that NPG
    // flips on entry/exit. Worker checks IsPausedByGuard() each tick and
    // silently skips picking while a remote player is nearby. `enabled_`
    // stays untouched (user intent).
    void OnShutdown() override {}
    void OnResume()   override {}

    // Persistence
    std::string ConfigKey() const override { return "AutoPickup"; }
    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["distance"]         = distance_;
        j["pollIntervalMs"]   = pollIntervalMs_;
        j["maxPicksPerTick"]  = maxPicksPerTick_;
        j["showList"]         = showList_;
        j["pickOnZ"]          = pickOnZ_;
        j["perPickDelayMs"]   = perPickDelayMs_;
    }
    void LoadState(const nlohmann::json &j) override
    {
        distance_        = j.value("distance",        distance_);
        pollIntervalMs_  = j.value("pollIntervalMs",  pollIntervalMs_);
        maxPicksPerTick_ = j.value("maxPicksPerTick", maxPicksPerTick_);
        showList_        = j.value("showList",        showList_);
        pickOnZ_         = j.value("pickOnZ",         pickOnZ_);
        perPickDelayMs_  = j.value("perPickDelayMs",  perPickDelayMs_);
        // Migrate from the old world-unit default (600) / legacy 50~1500 slider —
        // the engine's own AutoHunt__FindNearestPickableDrop caps at 50 tiles
        // (see g_AutoHuntSearchRadiusTiles), so anything above that is an
        // instant anti-cheat tell. Clamp silently on load.
        if (distance_ < 2.0f)   distance_ = 2.0f;
        if (distance_ > 50.0f)  distance_ = 15.0f;
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
            if (enabled_ && !IsPausedByGuard())
            {
                if (pickOnZ_)
                {
                    // Z 模式:电平触发发包,按住 Z 就按 pollIntervalMs_ 节流持续拾取。
                    // 空闲时(Z 未按下)也按 pollIntervalMs_ 节流的刷一下 drop snapshot,
                    // 让 UI 列表不会卡在上次按 Z 的瞬间。
                    //
                    // 用 GetAsyncKeyState 高位读"当前是否按下" — 不用低位 &1 那种
                    // "自上次调用以来按过没"的语义,因为游戏的 Input_PollKeyboard
                    // 每帧也在调 GetAsyncKeyState('Z'),低位会被它清掉,边沿丢失。
                    //
                    // 注意:GetAsyncKeyState 的虚拟注入(util/InputInjector)也会把
                    // 'Z' 显示成按下 — 想从 web 触发拾取直接 sendInput vks=[Z]
                    // 就可以,不用单独走命令。
                    bool currZ = (GetAsyncKeyState('Z') & 0x8000) != 0;
                    if (currZ)
                    {
                        // Z 按下:按节流间隔持续拾取
                        if (GetTickCount() - lastTickMs_ >= static_cast<DWORD>(pollIntervalMs_))
                            RunPickPass();
                    }
                    else
                    {
                        // Z 未按下:只刷新 UI 列表
                        if (GetTickCount() - lastTickMs_ >= static_cast<DWORD>(pollIntervalMs_))
                            RefreshSnapshotOnly();
                    }
                }
                else
                {
                    Tick();
                }
            }

            DWORD now = GetTickCount();
            if (enabled_ && now - lastHeartbeat > 10000)
            {
                size_t cnt = 0;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    cnt = lastDrops_.size();
                }
                spdlog::info("GGTB::AutoPickup: heartbeat — drops={}, paused={}, pickOnZ={}",
                             cnt, IsPausedByGuard(), pickOnZ_);
                lastHeartbeat = now;
            }

            // Sleep in small slices so stop_ is responsive on shutdown.
            for (int i = 0; i < 10 && !stop_.load(); ++i)
                Sleep(10);
        }
        spdlog::info("GGTB::AutoPickup: worker thread exited");
    }

    // 自动模式入口:沿用原逻辑(节流 + 发包)。
    void Tick()
    {
        DWORD now = GetTickCount();
        if (now - lastTickMs_ < static_cast<DWORD>(pollIntervalMs_))
            return;
        lastTickMs_ = now;
        RunPickPass();
    }

    // 真正的"扫描 + 发拾取包"一轮。Z 模式按下 Z 时走这条,自动模式经 Tick 节流后
    // 走这条。不做节流判断 — 调用方负责。
    void RunPickPass()
    {
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
            // 包间延时 — 可配置,默认 50ms。引擎的 Net__BeginSend / sub_B2C930
            // 路径对主渲染线程不是严格线程安全,且突发的 CG_PICK_ITEM 正是服务端
            // 节流标记的目标。
            if (perPickDelayMs_ > 0)
                Sleep(static_cast<DWORD>(perPickDelayMs_));
        }

        std::lock_guard<std::mutex> lk(mutex_);
        lastDrops_     = std::move(drops);
        picksLastTick_ = picked;
        lastTickMs_    = GetTickCount(); // Z 模式下让被动刷新继续按节流走
    }

    // Z 模式空闲期用 — 只刷新 UI 列表,不发任何包,不动 picksLastTick_。
    void RefreshSnapshotOnly()
    {
        auto drops = GetNearbyDropItems(distance_, /*includeUnpickable=*/false);
        std::lock_guard<std::mutex> lk(mutex_);
        lastDrops_  = std::move(drops);
        lastTickMs_ = GetTickCount();
    }

    float             distance_         = 15.0f;
    int               pollIntervalMs_   = 500;
    int               maxPicksPerTick_  = 5;
    bool              showList_         = false;
    bool              pickOnZ_          = false; // true = 关自动扫描,按住 Z 持续拾取
    int               perPickDelayMs_   = 50;    // 每个拾取包之间的延时,默认 50ms

    DWORD             lastTickMs_       = 0;
    size_t            picksLastTick_    = 0;

    std::mutex                  mutex_;
    std::vector<DropItemInfo>   lastDrops_;

    std::atomic<bool> stop_{false};
    std::thread       worker_;
};

} // namespace GGTB
