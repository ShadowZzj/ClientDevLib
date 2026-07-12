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

// 自动分解宝石。
//
// 独立 worker(ctor 起 / dtor join,和 AutoPickupModule 一致 —— 菜单关着也要跑),
// 每 intervalMs_ 扫一遍背包 GetBagItems(),对名字命中 kGemNames 的每个堆叠调用
// SendDecomposeGem(bagId, min(count, 300))。每包之间 sleep 一小段避免突发触发服务端节流。
//
// 不持久化:ConfigKey() 返回空串 —— 分解是破坏性动作,不该在换角色/重开后自动恢复,
// 每次进游戏都要用户手动勾。
//
// 名字精确匹配 4 种:水晶 / 紅寶石 / 鑽石 / 粉紅鑽。名字取自 itemTable+0x0C(Big5→UTF8)。
// 若线上物品名带后缀对不上,改 IsTargetGem 里的 kGemNames 即可。
class AutoDecomposeGemModule : public IModule
{
  public:
    AutoDecomposeGemModule() : IModule(u8"自动分解宝石")
    {
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~AutoDecomposeGemModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    // 分解是纯背包/协议操作,附近玩家看不到任何表现,不需要被 NPG 拍暂停。
    bool CanAutoPause() const override { return false; }

    void OnRender() override
    {
        ImGui::Checkbox(u8"自动分解宝石##AutoDecomposeGem", &enabled_);
        ImGui::SliderInt(u8"扫描间隔(ms)##AutoDecomposeGem", &intervalMs_, 200, 5000);

        size_t stacks = 0, gemTotal = 0, lastSent = 0;
        DWORD  age = 0;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            stacks   = lastStacks_;
            gemTotal = lastGemTotal_;
            lastSent = lastSentCount_;
            age      = lastTickMs_ ? (GetTickCount() - lastTickMs_) : 0;
        }

        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           u8"目标: 水晶 / 紅寶石 / 鑽石 / 粉紅鑽,每堆每次分解 %d",
                           kPerPacketMax);

        if (!enabled_)
            ImGui::TextDisabled(u8"未启用 (worker 仍在运行,但不分解)");
        else
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               u8"运行中: 命中 %d 堆 / 共 %d 颗, 上轮发 %d 包, 距上轮 %lums",
                               static_cast<int>(stacks),
                               static_cast<int>(gemTotal),
                               static_cast<int>(lastSent),
                               static_cast<unsigned long>(age));
    }

    // 不持久化 —— 空 ConfigKey。
    std::string ConfigKey() const override { return {}; }

  private:
    static bool IsTargetGem(const std::string &name)
    {
        static const char *const kGemNames[] = {u8"水晶", u8"紅寶石", u8"鑽石", u8"粉紅鑽"};
        for (auto *g : kGemNames)
            if (name == g)
                return true;
        return false;
    }

    void WorkerLoop()
    {
        spdlog::info("GGTB::AutoDecomposeGem: worker thread started");
        while (!stop_.load())
        {
            if (enabled_ && GetTickCount() - lastTickMs_ >= static_cast<DWORD>(intervalMs_))
                RunPass();

            // 小切片睡眠,让 stop_ 在退出时能及时响应。
            for (int i = 0; i < 10 && !stop_.load(); ++i)
                Sleep(10);
        }
        spdlog::info("GGTB::AutoDecomposeGem: worker thread exited");
    }

    void RunPass()
    {
        auto   items    = GetBagItems();
        size_t stacks   = 0;
        size_t gemTotal = 0;
        size_t sent     = 0;

        for (auto &it : items)
        {
            if (!IsTargetGem(it.name))
                continue;
            ++stacks;
            gemTotal += it.count;

            uint32_t cnt = it.count > static_cast<uint32_t>(kPerPacketMax)
                               ? static_cast<uint32_t>(kPerPacketMax)
                               : it.count;
            if (cnt == 0)
                continue;

            if (SendDecomposeGem(it.bagId, cnt))
            {
                ++sent;
                spdlog::info("GGTB::AutoDecomposeGem: slot={} bagId={} name='{}' count={}",
                             it.slotIndex, it.bagId, it.name, cnt);
            }
            Sleep(kPerPacketDelayMs);
        }

        std::lock_guard<std::mutex> lk(mutex_);
        lastStacks_    = stacks;
        lastGemTotal_  = gemTotal;
        lastSentCount_ = sent;
        lastTickMs_    = GetTickCount();
    }

    static constexpr int   kPerPacketMax     = 300;
    static constexpr DWORD kPerPacketDelayMs = 60;

    int   intervalMs_ = 1000;
    DWORD lastTickMs_ = 0;

    std::mutex mutex_;
    size_t     lastStacks_    = 0;
    size_t     lastGemTotal_  = 0;
    size_t     lastSentCount_ = 0;

    std::atomic<bool> stop_{false};
    std::thread       worker_;
};

} // namespace GGTB
