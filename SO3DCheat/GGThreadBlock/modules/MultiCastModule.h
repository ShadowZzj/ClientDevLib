#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "../util/PatternResolver.h"
#include "ModuleExclusivity.h"
#include <Detours/build/include/detours.h>
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>
#include <thread>

namespace GGTB
{

// 多重施法 — Detour Net__SendSkillPacket(thiscall(this, skillId, targetId)).
// 任何代码(游戏 UI 点击 / FireFullPower / 我们的 CastSkillOnMonster /
// CastBombOnMonster)调用这个函数,我们就把它再叫 (castCount_ - 1) 次。
//
// castCount_ 范围 1..100:
//   1 → 不加发(行为等同未启用,默认值)
//   N → 总共 N 次原始调用 = 1 次正常 + (N-1) 次复制
// 上限 100 是经验值:再高单次 cast 会一次性 push 100 个包到 sendq,触发服务端
// burst 检测/掉线 watchdog 的概率指数级上升。即便不掉线,server tick 一次最
// 多吞 ~30 包,多余的会被吞掉算无效——上限 100 给"压力测试 + 缓冲被吞掉"
// 留点余量,但不要再往上抬。
//
// 互斥: 和火力全开同源 hook 路径 — 同时启用会把火力全开的自动轮换乘 N 倍直接
// 触发掉线 watchdog。任一方启用都通过 ModuleExclusivity 的 atomic + IModule
// 的 ForceDisable 把对方关掉。
//
// Stage-2 安全: NetSendSkillPacket 在 stage-1 就解出来了(FireFullPower 一直在
// 用),所以用户启用本模块时它已就绪;万一没解出来 install 失败回滚 enabled_。
//
// 爆破职业自动制作炸弹: 启用本模块 + 职业==16/6 时,worker 每 ~500ms 检查一次
// 制作炸弹(skillId 325),冷却好就放一发。和火力全开的 MakeBomb 分支同源,但
// 只做 MakeBomb 不做 ThrowBomb——丢弹是用户手点的,正好走 hook 被加发 N 次。
// MakeBomb 本身用 thread_local 标记跳过加发(用户原话"放一下" = 单发,不要 100
// 个炸弹一次性 push 进 sendq)。
class MultiCastModule : public IModule
{
  public:
    MultiCastModule() : IModule(u8"多重施法")
    {
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~MultiCastModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
        if (hookInstalled_)
            UninstallHook();
    }

    bool CanAutoPause() const override { return true; }
    Tab  GetTab() const override { return Tab::OP; }

    void OnRender() override
    {
        bool prevEnabled = enabled_;
        if (ImGui::Checkbox(u8"多重施法##MultiCast", &enabled_))
        {
            if (enabled_ && !prevEnabled)
                ActivateExclusively();
            else if (!enabled_ && prevEnabled)
                Deactivate();
        }

        // 双控件:滑块快速调整 + InputInt 直接输入精确值。两边共享 castCount_,
        // 任一端改动都同步进 hook 用的原子量。1 = 不加发(无副作用,行为等同未启用)。
        bool changed = false;
        ImGui::PushItemWidth(160.0f);
        if (ImGui::SliderInt(u8"施法次数##MultiCastSlider", &castCount_, 1, 100,
                             u8"%d 次/原始调用"))
            changed = true;
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::PushItemWidth(120.0f);
        if (ImGui::InputInt(u8"##MultiCastInput", &castCount_, 1, 10,
                            ImGuiInputTextFlags_CharsDecimal))
            changed = true;
        ImGui::PopItemWidth();
        if (changed)
        {
            if (castCount_ < 1)   castCount_ = 1;
            if (castCount_ > 100) castCount_ = 100;
            s_castCount_.store(castCount_, std::memory_order_release);
        }
        if (castCount_ <= 1)
            ImGui::TextDisabled(u8"= 1 时不加发,等同未启用");

        if (enabled_)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                               u8"已 hook Net__SendSkillPacket — 每次 cast 实发 %d 包",
                               castCount_);
            uint64_t triggers = s_triggerCount_.load(std::memory_order_relaxed);
            uint64_t extras   = s_extraSentCount_.load(std::memory_order_relaxed);
            ImGui::Text(u8"触发: %llu, 额外包: %llu",
                        static_cast<unsigned long long>(triggers),
                        static_cast<unsigned long long>(extras));
        }
        else
        {
            ImGui::TextDisabled(u8"未启用 — Detour 未安装,游戏 cast 走原路径");
        }

        // 爆破职业 — 自动制作炸弹子开关。和上面 castCount 是独立的;就算 castCount=1
        // (不加发),只要 autoMakeBomb_ + bomber 还是会自动放制作炸弹。
        ImGui::Separator();
        ImGui::Checkbox(u8"爆破职业自动制作炸弹##MultiCast", &autoMakeBomb_);
        if (autoMakeBomb_)
        {
            uint32_t prof     = GetProfession();
            bool     isBomber = (prof == kProfessionBomber || prof == kProfessionBlacksmith);
            uint64_t made     = s_autoMakeBombCount_.load(std::memory_order_relaxed);
            if (!enabled_)
                ImGui::TextDisabled(u8"  (主开关未启用,不会自动制作)");
            else if (IsPausedByGuard())
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                   u8"  已被 NPG 暂停 (附近有玩家)");
            else if (!isBomber)
                ImGui::TextDisabled(u8"  当前职业不是爆破/铁匠 (prof=%u),不会触发",
                                    static_cast<unsigned>(prof));
            else
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                                   u8"  已制作 %llu 次 (CD 好就发一发,不被加发放大)",
                                   static_cast<unsigned long long>(made));
        }
    }

    // OnShutdown 由 ForceDisable / End() / dtor / NPG 调用 — 都得卸 hook。
    void OnShutdown() override
    {
        if (hookInstalled_)
            UninstallHook();
        g_multiCastActive.store(false);
    }

    // OnResume 由 LoadState 触发 (用户上次配置 enabled=true 进来时)。同样要做
    // 互斥检查 — 否则两个模块的 LoadState 谁先后跑都可能两者都 active。
    void OnResume() override
    {
        if (!enabled_)
            return;
        // LoadState 已经把 castCount_ 写好了,同步到原子量
        s_castCount_.store(castCount_, std::memory_order_release);
        ActivateExclusivelyImpl();
    }

    std::string ConfigKey() const override { return "MultiCast"; }

    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["castCount"]    = castCount_;
        j["autoMakeBomb"] = autoMakeBomb_;
    }

    void LoadState(const nlohmann::json &j) override
    {
        castCount_    = j.value("castCount",    castCount_);
        autoMakeBomb_ = j.value("autoMakeBomb", autoMakeBomb_);
        if (castCount_ < 1)   castCount_ = 1;
        if (castCount_ > 100) castCount_ = 100;
        IModule::LoadState(j); // 触发 OnResume / OnShutdown
    }

    // dllmain 在两个模块都构造完之后塞 peer 指针进来,免得这里 include
    // FireFullPowerModule.h 引循环。
    static void SetPeer(IModule *p) { s_peer_ = p; }

    // 制作炸弹独占锁 — MakeBombDropModule 也需要用
    static void LockCast()   { s_castLocked_.store(true, std::memory_order_release); }
    static void UnlockCast() { s_castLocked_.store(false, std::memory_order_release); }
    static void SetThreadMakeBomb(bool v) { tls_inAutoMakeBomb = v; }

  private:
    void ActivateExclusively()
    {
        ActivateExclusivelyImpl();
    }

    void ActivateExclusivelyImpl()
    {
        // 拍下火力全开
        if (s_peer_ && s_peer_->IsEnabled())
        {
            spdlog::info("GGTB::MultiCast: 启用 — 强制关闭火力全开(互斥)");
            s_peer_->ForceDisable();
            g_fireFullPowerActive.store(false);
        }
        g_multiCastActive.store(true);

        if (!InstallHook())
        {
            // 安装失败回滚:checkbox 状态 + 互斥位都还原
            enabled_ = false;
            g_multiCastActive.store(false);
            spdlog::error("GGTB::MultiCast: install 失败,回滚 enabled_");
        }
    }

    void Deactivate()
    {
        g_multiCastActive.store(false);
        UninstallHook();
    }

    static IModule *s_peer_;

    // ---- Detours ----
    using fnNetSendSkillPacket =
        int(__fastcall *)(void *ecx, void *edx, int skillId, int targetId);

    static fnNetSendSkillPacket  s_oNetSendSkillPacket_;
    static std::atomic<int>      s_castCount_;
    static std::atomic<uint64_t> s_triggerCount_;
    static std::atomic<uint64_t> s_extraSentCount_;
    static std::atomic<bool>     s_castLocked_;

    bool hookInstalled_ = false;

    bool InstallHook()
    {
        if (hookInstalled_) return true;
        auto addr = PatternResolver::Get("NetSendSkillPacket");
        if (!addr)
        {
            spdlog::error("GGTB::MultiCast: NetSendSkillPacket 未解析,无法 hook");
            return false;
        }
        s_oNetSendSkillPacket_ = reinterpret_cast<fnNetSendSkillPacket>(addr);

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        LONG err = DetourAttach(reinterpret_cast<PVOID *>(&s_oNetSendSkillPacket_),
                                HookNetSendSkillPacket);
        LONG cm = DetourTransactionCommit();
        if (err != NO_ERROR || cm != NO_ERROR)
        {
            spdlog::error("GGTB::MultiCast: Detour 失败 attach={} commit={}", err, cm);
            return false;
        }
        hookInstalled_ = true;
        s_castCount_.store(castCount_, std::memory_order_release);
        spdlog::info("GGTB::MultiCast: hook attached @ {:x}", addr);
        return true;
    }

    void UninstallHook()
    {
        if (!hookInstalled_) return;
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(reinterpret_cast<PVOID *>(&s_oNetSendSkillPacket_),
                     HookNetSendSkillPacket);
        DetourTransactionCommit();
        hookInstalled_ = false;
        spdlog::info("GGTB::MultiCast: hook detached");
    }

    // 真正的 detour 入口 — 任意线程都可能进来。原始调用先发,确保上层意图的
    // cast 总会落地;然后按 castCount-1 复发。Detours trampoline 保证递归
    // 不会再进 hook 自己。
    //
    // tls_inAutoMakeBomb 短路:本模块的 worker 线程主动发 MakeBomb 时设这个
    // thread-local,hook 看到就只走原始 1 次、不放大。游戏主线程 / UI 线程上
    // 玩家手点的 MakeBomb 不受影响,照常按 castCount 加发。
    //
    // s_castLock_ 互斥: 制作炸弹线程 set flag 后独占整个 hook 路径,其他线程
    // 进来发现 flag 已 set 直接 return — 避免制作炸弹期间其他线程的 cast 包
    // 和 MakeBomb 包交错导致服务端异常。
    static int __fastcall HookNetSendSkillPacket(void *ecx, void *edx,
                                                  int skillId, int targetId)
    {
        // 制作炸弹独占: 其他线程进来直接丢弃
        if (!tls_inAutoMakeBomb && s_castLocked_.load(std::memory_order_acquire))
            return 0;

        s_triggerCount_.fetch_add(1, std::memory_order_relaxed);
        int rv = s_oNetSendSkillPacket_(ecx, edx, skillId, targetId);

        if (tls_inAutoMakeBomb)
            return rv; // 自动制作炸弹 — 不放大,只发一发

        int n = s_castCount_.load(std::memory_order_acquire);
        if (n < 1)   n = 1;
        if (n > 100) n = 100;

        for (int i = 1; i < n; ++i)
        {
            s_oNetSendSkillPacket_(ecx, edx, skillId, targetId);
            s_extraSentCount_.fetch_add(1, std::memory_order_relaxed);
        }
        return rv;
    }

    // ---- 爆破职业自动制作炸弹 worker ----
    //
    // 设计跟火力全开的 bomber 分支同源,但只做 MakeBomb 这一件事:
    //   1. 主开关 enabled_ 必须开 (否则 Detour 都没装,加发也没意义)
    //   2. autoMakeBomb_ 子开关必须开
    //   3. NPG 暂停时跳过 (附近有玩家发 MakeBomb 包同样会被看到)
    //   4. 职业是 16 (爆破) 或 6 (铁匠)
    //   5. SkillManager 里 skillId 325 已学会且 leftCoolDown <= 0
    // 满足就调一次 CastMakeBomb(false),用 tls_inAutoMakeBomb 圈住,这一发不被
    // 自己 hook 加发 N 倍。
    void WorkerLoop()
    {
        spdlog::info("GGTB::MultiCast: auto-makebomb worker started");
        while (!stop_.load())
        {
            if (enabled_ && autoMakeBomb_ && !IsPausedByGuard())
                TickAutoMakeBomb();

            // 500ms 轮询 — MakeBomb CD 是秒级,这个粒度够;细切成 10ms 是为了
            // dtor 里 stop_ 翻 true 时能在半个 tick 内退出,不让 join() 卡住宿主关闭。
            for (int i = 0; i < 50 && !stop_.load(); ++i)
                Sleep(10);
        }
        spdlog::info("GGTB::MultiCast: auto-makebomb worker exited");
    }

    void TickAutoMakeBomb()
    {
        uint32_t prof = GetProfession();
        if (prof != kProfessionBomber && prof != kProfessionBlacksmith)
            return;

        // GetLearnedSkills 已经过滤掉 learnedFlag==0 的格子,这里只查 325。
        // 没找到 = 还没学会制作炸弹,什么也不做。
        auto skills = GetLearnedSkills();
        for (auto &info : skills)
        {
            if (info.skill.skillId != kSkillIdMakeBomb)
                continue;
            if (info.skill.leftCoolDown > 0.0f)
                return; // 还在 CD,等下个 tick

            // 背包炸弹已满 9000 则不再制作
            if (GetTotalBombCount() >= 9000)
                return;

            LockCast();
            tls_inAutoMakeBomb = true;
            CastMakeBomb(false);
            tls_inAutoMakeBomb = false;
            UnlockCast();
            s_autoMakeBombCount_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    int castCount_     = 1;    // 1 = 不加发(默认),滑块范围 1..100
    bool autoMakeBomb_ = true; // 默认开 — 用户手动开多重施法八成是 farm 场景

    std::atomic<bool>           stop_{false};
    std::thread                 worker_;
    static thread_local bool    tls_inAutoMakeBomb;
    static std::atomic<uint64_t> s_autoMakeBombCount_;
};

inline IModule                                *MultiCastModule::s_peer_              = nullptr;
inline MultiCastModule::fnNetSendSkillPacket   MultiCastModule::s_oNetSendSkillPacket_ = nullptr;
inline std::atomic<int>                        MultiCastModule::s_castCount_         {1};
inline std::atomic<uint64_t>                   MultiCastModule::s_triggerCount_      {0};
inline std::atomic<uint64_t>                   MultiCastModule::s_extraSentCount_    {0};
inline std::atomic<bool>                       MultiCastModule::s_castLocked_        {false};
inline thread_local bool                       MultiCastModule::tls_inAutoMakeBomb   = false;
inline std::atomic<uint64_t>                   MultiCastModule::s_autoMakeBombCount_ {0};

} // namespace GGTB
