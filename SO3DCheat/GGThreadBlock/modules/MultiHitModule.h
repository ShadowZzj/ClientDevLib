#pragma once
#include "../IModule.h"
#include "../util/PatternResolver.h"
#include <Detours/build/include/detours.h>
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace GGTB
{

// 多段攻击 — Detour Net__SendPacket_Plaintext。
// 当游戏发送 proto 411014(普攻 32B) 或 411072(连续技 44B) 时,在 hook 里
// 先捕获明文字节,等原始调用(obfuscate + send)返回后,把明文写回 pkt 缓冲区,
// 再调原函数 N-1 次。每次重发都重新做 XOR 混淆走完整发送路径,服务端看到
// 的是 N 个完全一致的攻击包。
//
// 倍率 hitRate_ 1.0..30.0 支持小数,用累加器实现:
//   1.5 → 序列 1,2,1,2... 长期均值 1.5
//   1.3 → 大约每 3-4 次原始攻击多 1 发
// 内部 milli-unit 定点 + atomic CAS,任意线程并发安全。
//
// NetLog 共 hook 同一函数: Detours 会 stack,我们后装的 hook 先跑;重发的
// N-1 次会再走 NetLog 的 hook,sendlog 里能看到全部包(符合实际线上发送数)。
class MultiHitModule : public IModule
{
  public:
    MultiHitModule() : IModule(u8"多段攻击") {}

    ~MultiHitModule() override
    {
        if (hookInstalled_)
            UninstallHook();
    }

    bool CanAutoPause() const override { return true; }

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"启用##MultiHit", &enabled_))
        {
            if (enabled_)
            {
                if (!InstallHook())
                    enabled_ = false;
            }
            else
            {
                UninstallHook();
            }
        }

        bool changed = false;
        ImGui::PushItemWidth(180.0f);
        if (ImGui::SliderFloat(u8"攻击倍率##MultiHitSlider", &hitRate_, 1.0f, 30.0f,
                               u8"%.2f 倍", ImGuiSliderFlags_AlwaysClamp))
            changed = true;
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::PushItemWidth(110.0f);
        if (ImGui::InputFloat(u8"##MultiHitInput", &hitRate_, 0.1f, 1.0f, u8"%.2f"))
            changed = true;
        ImGui::PopItemWidth();
        if (changed)
        {
            if (hitRate_ < 1.0f)  hitRate_ = 1.0f;
            if (hitRate_ > 30.0f) hitRate_ = 30.0f;
            SyncRateToAtomic();
        }
        if (hitRate_ <= 1.0f)
            ImGui::TextDisabled(u8"= 1.00 时不加发,等同未启用");
        else
            ImGui::TextDisabled(u8"小数走累加器:1.5 = 长期均值 1.5 倍");

        if (enabled_)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                               u8"已 hook NetSendPacketPlaintext — 倍率 %.2f", hitRate_);
            uint64_t triggers = s_triggerCount_.load(std::memory_order_relaxed);
            uint64_t extras   = s_extraSentCount_.load(std::memory_order_relaxed);
            ImGui::Text(u8"触发: %llu, 额外包: %llu (实测倍率 %.3f)",
                        static_cast<unsigned long long>(triggers),
                        static_cast<unsigned long long>(extras),
                        triggers ? (1.0 + double(extras) / double(triggers)) : 1.0);
        }
        else
        {
            ImGui::TextDisabled(u8"未启用 — Detour 未安装");
        }
    }

    void OnShutdown() override
    {
        if (hookInstalled_)
            UninstallHook();
    }

    void OnResume() override
    {
        if (!enabled_)
            return;
        SyncRateToAtomic();
        if (!InstallHook())
            enabled_ = false;
    }

    std::string ConfigKey() const override { return "MultiHit"; }

    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["hitRate"] = hitRate_;
    }

    void LoadState(const nlohmann::json &j) override
    {
        hitRate_ = j.value("hitRate", hitRate_);
        if (hitRate_ < 1.0f)  hitRate_ = 1.0f;
        if (hitRate_ > 30.0f) hitRate_ = 30.0f;
        IModule::LoadState(j);
    }

  private:
    // ---- Detours ----
    // Net__SendPacket_Plaintext(this=CGameClient, pktObj, length)
    using fnSendPacketPT = int(__fastcall *)(void *ecx, void *edx, void *pkt, int len);

    static fnSendPacketPT        s_oSendPacketPT_;
    static std::atomic<int>      s_hitRateMilli_; // 倍率 * 1000
    static std::atomic<int>      s_accumMilli_;   // 累加器(milli-units)
    static std::atomic<uint64_t> s_triggerCount_;
    static std::atomic<uint64_t> s_extraSentCount_;

    bool  hookInstalled_ = false;
    float hitRate_       = 2.0f;

    void SyncRateToAtomic()
    {
        int milli = static_cast<int>(hitRate_ * 1000.0f + 0.5f);
        s_hitRateMilli_.store(milli, std::memory_order_release);
        s_accumMilli_.store(0, std::memory_order_release);
    }

    bool InstallHook()
    {
        if (hookInstalled_) return true;
        auto addr = PatternResolver::Get("NetSendPacketPlaintext");
        if (!addr)
        {
            spdlog::error("GGTB::MultiHit: NetSendPacketPlaintext 未解析");
            return false;
        }
        s_oSendPacketPT_ = reinterpret_cast<fnSendPacketPT>(addr);

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        LONG err = DetourAttach(reinterpret_cast<PVOID *>(&s_oSendPacketPT_),
                                HookSendPacketPT);
        LONG cm = DetourTransactionCommit();
        if (err != NO_ERROR || cm != NO_ERROR)
        {
            spdlog::error("GGTB::MultiHit: Detour 失败 attach={} commit={}", err, cm);
            return false;
        }
        hookInstalled_ = true;
        SyncRateToAtomic();
        spdlog::info("GGTB::MultiHit: hook attached @ {:x}", addr);
        return true;
    }

    void UninstallHook()
    {
        if (!hookInstalled_) return;
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(reinterpret_cast<PVOID *>(&s_oSendPacketPT_),
                     HookSendPacketPT);
        DetourTransactionCommit();
        hookInstalled_ = false;
        spdlog::info("GGTB::MultiHit: hook detached");
    }

    static int ConsumeAccumulator(int rateMilli)
    {
        int prev = s_accumMilli_.load(std::memory_order_acquire);
        int totalToSend, next;
        for (;;)
        {
            int newAccum = prev + rateMilli;
            totalToSend  = newAccum / 1000;
            next         = newAccum - totalToSend * 1000;
            if (s_accumMilli_.compare_exchange_weak(prev, next,
                    std::memory_order_acq_rel, std::memory_order_acquire))
                break;
        }
        return totalToSend;
    }

    // 攻击 proto 白名单 — 只对普攻 / 连续技包做加发,其他全放行。
    static bool IsAttackProto(uint32_t proto)
    {
        return proto == 411014 || proto == 411072;
    }

    static int __fastcall HookSendPacketPT(void *ecx, void *edx, void *pkt, int len)
    {
        // 头部 8 字节:totalLen(u32) + protocolId(u32),body 从 +8 开始,
        // body 才是被 sub_5CA910 in-place 混淆的范围,头部不会被改。
        uint32_t totalLen = 0, proto = 0;
        if (pkt && len >= 8)
        {
            std::memcpy(&totalLen, static_cast<uint8_t *>(pkt),     4);
            std::memcpy(&proto,    static_cast<uint8_t *>(pkt) + 4, 4);
        }

        bool attack = IsAttackProto(proto);
        // 只在长度自洽时缓存(避免 totalLen 损坏导致越界 memcpy)
        bool canRepeat = attack && totalLen >= 8 && totalLen <= static_cast<uint32_t>(len);

        // 缓冲在栈上 — 411014=32B / 411072=44B,留 64B 足够
        uint8_t savedBuf[64];
        if (canRepeat)
            std::memcpy(savedBuf, pkt, totalLen);

        int rv = s_oSendPacketPT_(ecx, edx, pkt, len);

        if (!canRepeat)
            return rv;

        s_triggerCount_.fetch_add(1, std::memory_order_relaxed);

        int rateMilli = s_hitRateMilli_.load(std::memory_order_acquire);
        if (rateMilli <= 1000)
            return rv; // 1.0 倍不加发

        int totalToSend = ConsumeAccumulator(rateMilli);
        int extras      = totalToSend - 1;
        if (extras < 0)  extras = 0;
        if (extras > 30) extras = 30; // 安全上限

        for (int i = 0; i < extras; ++i)
        {
            // 把缓存的明文写回 pkt;原函数会再做 XOR + Net__RawSendAll。
            // 之所以不能跳过 SendPacketPlaintext 直接发已混淆字节,是因为
            // 混淆里有计数器/校验,服务端按顺序解,每次都得走完整路径。
            std::memcpy(pkt, savedBuf, totalLen);
            s_oSendPacketPT_(ecx, edx, pkt, len);
            s_extraSentCount_.fetch_add(1, std::memory_order_relaxed);
        }
        return rv;
    }
};

inline MultiHitModule::fnSendPacketPT       MultiHitModule::s_oSendPacketPT_      = nullptr;
inline std::atomic<int>                     MultiHitModule::s_hitRateMilli_       {2000};
inline std::atomic<int>                     MultiHitModule::s_accumMilli_         {0};
inline std::atomic<uint64_t>                MultiHitModule::s_triggerCount_       {0};
inline std::atomic<uint64_t>                MultiHitModule::s_extraSentCount_     {0};

} // namespace GGTB
