#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "AutoConfirmModule.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace GGTB
{

// 自动邮寄
//
// 周期性轮询本地玩家钱包 (CLocalUser+0x3498, int64)。如果 money >= threshold 且
// 距上一封成功邮件 >= cooldown 毫秒,就调用引擎自带的 MailBox::SendMoneyMail
// (proto 411524, op=0) 把固定金额寄给配置好的收件人。包结构和 wire byte 校验
// 见 CLocalPlayer.h 的 SendMoneyMail 文档。
//
// 不受 NPG 影响 (CanAutoPause -> false)。原因:邮件不会暴露给附近真人 (是 NPC
// 接收 + 服务器异步派送),NearbyPlayerGuard 是为了"附近有人就别 leak 视觉/移
// 速类"准备的;邮件是后台动作,跟视觉/移动 zero overlap,加进 NPG 锁定列表只
// 会让操作员在战斗里没机会触发 backup。
//
// 自带 worker 线程 + ctor 启动 / dtor join,跟 AutoPickup 同款。这样 ImGui 菜
// 单关闭时也能继续巡检——不挂菜单挂机的常见场景。
class AutoMailModule : public IModule
{
  public:
    AutoMailModule() : IModule(u8"自动邮寄")
    {
        // 默认收件人留空,启动后必须由用户填好再开启 enabled_
        recipient_[0] = '\0';
        worker_       = std::thread([this] { WorkerLoop(); });
    }

    ~AutoMailModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    // 不参与 NPG 暂停 — 见类注释。
    bool CanAutoPause() const override { return false; }

    void OnRender() override
    {
        ImGui::Checkbox(u8"自动邮寄##AutoMail", &enabled_);

        // 收件人 (16 字节槽位,引擎 strlen 后 memmove,超长会冲掉 op/money 字段)
        if (ImGui::InputText(u8"收件人##AutoMail", recipient_, sizeof(recipient_)))
        {
            // 即时清掉非 ASCII 控制字符,引擎不允许带换行/制表符
            for (char *p = recipient_; *p; ++p)
                if (static_cast<unsigned char>(*p) < 0x20) *p = '\0';
        }

        ImGui::InputScalar(u8"钱 >= (阈值)##AutoMail", ImGuiDataType_S64, &threshold_,
                           nullptr, nullptr, "%lld");
        ImGui::InputScalar(u8"邮寄金额##AutoMail", ImGuiDataType_S64, &amount_,
                           nullptr, nullptr, "%lld");
        ImGui::SliderInt(u8"轮询间隔(ms)##AutoMail", &pollIntervalMs_, 500, 60000);
        ImGui::SliderInt(u8"两封间隔(ms)##AutoMail", &cooldownMs_,    1000, 600000);

        // 实时状态 — UI 线程读 worker 的 snapshot,不直接戳引擎指针
        int64_t  money    = 0;
        DWORD    lastMail = 0;
        uint32_t sentCnt  = 0;
        bool     lastOk   = false;
        char     lastErr[64]{};
        {
            std::lock_guard<std::mutex> lk(mutex_);
            money    = lastMoney_;
            lastMail = lastSendMs_;
            sentCnt  = sentCount_;
            lastOk   = lastOk_;
            strncpy_s(lastErr, lastErrMsg_, _TRUNCATE);
        }

        ImGui::Separator();
        ImGui::Text(u8"当前钱包: %lld", static_cast<long long>(money));
        if (lastMail)
            ImGui::Text(u8"上次邮件: %lums 前 (累计 %u 封)",
                        static_cast<unsigned long>(GetTickCount() - lastMail),
                        sentCnt);
        else
            ImGui::TextDisabled(u8"未发送过邮件");

        if (!enabled_)
        {
            ImGui::TextDisabled(u8"未启用");
        }
        else if (!recipient_[0])
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), u8"收件人为空,不会发送");
        }
        else if (amount_ <= 0 || threshold_ <= 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), u8"阈值/金额必须 > 0");
        }
        else if (amount_ > threshold_)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                               u8"邮寄金额 > 阈值,触发后钱包还会再过线 → 死循环风险");
        }
        else
        {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               u8"运行中: 钱>=%lld 时寄 %lld 给 %s",
                               static_cast<long long>(threshold_),
                               static_cast<long long>(amount_),
                               recipient_);
        }
        if (lastErr[0])
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), u8"最近错误: %s", lastErr);
    }

    void OnShutdown() override {}
    void OnResume()   override {}

    std::string ConfigKey() const override { return "AutoMail"; }
    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["recipient"]      = std::string(recipient_);
        j["threshold"]      = threshold_;
        j["amount"]         = amount_;
        j["pollIntervalMs"] = pollIntervalMs_;
        j["cooldownMs"]     = cooldownMs_;
    }
    void LoadState(const nlohmann::json &j) override
    {
        std::string r   = j.value("recipient", std::string{});
        threshold_      = j.value("threshold",      threshold_);
        amount_         = j.value("amount",         amount_);
        pollIntervalMs_ = j.value("pollIntervalMs", pollIntervalMs_);
        cooldownMs_     = j.value("cooldownMs",     cooldownMs_);
        strncpy_s(recipient_, r.c_str(), _TRUNCATE);
        IModule::LoadState(j);
    }

  private:
    void WorkerLoop()
    {
        spdlog::info("GGTB::AutoMail: worker thread started");
        while (!stop_.load())
        {
            Tick();
            for (int i = 0; i < 10 && !stop_.load(); ++i)
                Sleep(10);
        }
        spdlog::info("GGTB::AutoMail: worker thread exited");
    }

    void Tick()
    {
        DWORD now = GetTickCount();
        if (now - lastTickMs_ < static_cast<DWORD>(pollIntervalMs_))
            return;
        lastTickMs_ = now;

        int64_t money = GetLocalMoney();
        {
            std::lock_guard<std::mutex> lk(mutex_);
            lastMoney_ = money;
        }

        if (!enabled_) return;
        if (!recipient_[0]) return;
        if (amount_ <= 0 || threshold_ <= 0) return;
        if (money < threshold_) return;

        // 余额够发,但要 >= cooldownMs 才允许下一封 — 防止短时间连发同一票包
        // 触发服务器节流/反作弊 burst 检测,以及避免邮件 UI 还没收尾就再压一封。
        if (lastSendMs_ && now - lastSendMs_ < static_cast<DWORD>(cooldownMs_))
            return;

        // 防呆:钱不能超额寄 (引擎服务端会直接 reject,但客户端先挡一层省得空跑)
        int64_t toSend = amount_;
        if (toSend > money) toSend = money;

        char nameSnapshot[16]{};
        strncpy_s(nameSnapshot, recipient_, _TRUNCATE);

        bool ok = SendMoneyMail(nameSnapshot, toSend, "");

        if (ok)
        {
            // 服务端会回 proto 0x7F55B (resultCode=5, MailBox__HandleResultAck case 5)
            // -> UI_ShowMessageBox_Modal(text, dialogType=1, ...)，弹"讯息已寄送"。
            // 即使用户没勾 AutoConfirm，也得装好 hook 并临时静音 OK toast，否则
            // 自动邮寄会留一堆不会自动消失的弹窗在屏幕上。8 秒覆盖正常 RTT + UI 渲染。
            AutoConfirmDetail::EnsureHooksInstalled();
            AutoConfirmDetail::SilenceOkToastFor(8000);
        }

        std::lock_guard<std::mutex> lk(mutex_);
        lastOk_     = ok;
        lastSendMs_ = now;
        if (ok)
        {
            ++sentCount_;
            lastErrMsg_[0] = '\0';
        }
        else
        {
            strncpy_s(lastErrMsg_, "SendMoneyMail returned false", _TRUNCATE);
        }
    }

    // ---- 用户配置 ----
    char    recipient_[16]   = {};      // 16 字节槽位,引擎只接 ASCII 名;末位留 NUL
    int64_t threshold_       = 1000000; // 当 money >= threshold 时触发
    int64_t amount_          = 500000;  // 每次邮寄金额
    int     pollIntervalMs_  = 5000;    // 轮询周期
    int     cooldownMs_      = 30000;   // 两封间最短间隔

    // ---- worker 状态 (mutex 保护) ----
    std::mutex mutex_;
    int64_t    lastMoney_  = 0;
    DWORD      lastSendMs_ = 0;
    DWORD      lastTickMs_ = 0;
    uint32_t   sentCount_  = 0;
    bool       lastOk_     = false;
    char       lastErrMsg_[64] = {};

    std::atomic<bool> stop_{false};
    std::thread       worker_;
};

} // namespace GGTB
