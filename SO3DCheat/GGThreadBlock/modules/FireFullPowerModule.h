#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "../util/NetLog.h"
#include "ModuleExclusivity.h"
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

// Fire-full-power (火力全开).
//
// Every `intervalMs`:
//   1. Read profession (CUser+0x1AF8).
//      - profession == 16 (爆破) or 6 (铁匠): bomber branch —
//        find bomb item, pin localUser+0x3578, Net__SendSkillPacket(83, nearestMob).
//        If no bomb, fire Net__SendSkillPacket(325, 0) to craft. This mirrors
//        AutoHunt__ChooseNextAction's own dispatch for these professions.
//      - any other profession: normal rotation —
//   2. Snapshot learned skills (skillId>0, level>0, leftCoolDown<=0).
//   3. Snapshot nearby monsters (kind != 7/8, alive, hp>0, within `distance`).
//   4. For each ready skill, fire SendSkillPacket against the nearest monster,
//      capped at `maxCastsPerTick` casts per cycle with a small inter-packet
//      sleep — same throttling story as AutoPickup. Server still validates
//      range/MP/cooldown/target-type, but the client never spins on a rejected
//      condition.
//
// Threading mirrors AutoPickupModule: dedicated worker started in ctor,
// joined in dtor. UI thread takes a snapshot under `mutex_` for display only.
//
// NPG integration: `paused_by_guard_` flips via OnShutdown/OnResume so the
// worker silently skips firing while a remote player is nearby (visible packet
// burst is the giveaway). `enabled_` stays untouched.
class FireFullPowerModule : public IModule
{
  public:
    // Bomber 投弹的两条路径:
    //   EngineCall — Net__BeginSend + Net__SendSkillPacket(83, target). 走引擎自己的
    //                变体分发,会读 SkillTable+0x16C/+0x170,服务端会再算一遍 AOE。
    //                等价于点游戏内技能图标。
    //   RawPacket  — 模仿老 so3dFullCheat: 直接用 Net__SkillSendPackage(0x6458E, buf, n)
    //                喂手填的 UINT32[] (skillId/level/bombItemId/inRange/count/targets)。
    //                跳过引擎的变体分发,自己挑 5-tile 邻居。
    enum class BomberMode : int
    {
        EngineCall = 0,
        RawPacket  = 1,
    };

    // 稳健模式超时:从上一次发包起最长等多久就强制放下一炮。NetLog 的伤害
    // 包(521056)观察点是首选信号,这只是兜底——网络坏掉/服务端漏推送的话也不会
    // 永远卡住。2 秒 = 用户原话需求,够覆盖正常 RTT (~80-200ms 见 recvlog)。
    static constexpr DWORD kStableModeTimeoutMs = 2000;

    FireFullPowerModule() : IModule(u8"火力全开")
    {
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~FireFullPowerModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    bool CanAutoPause() const override { return true; }
    Tab  GetTab() const override { return Tab::OP; }

    void OnRender() override
    {
        bool prevEnabled = enabled_;
        if (ImGui::Checkbox(u8"火力全开##FireFullPower", &enabled_))
        {
            // 互斥:启用本模块时拍下多重施法 (同源 hook 路径会乘 N 倍掉线)
            if (enabled_ && !prevEnabled)
            {
                if (peer_ && peer_->IsEnabled())
                {
                    spdlog::info("GGTB::FireFullPower: 启用 — 强制关闭多重施法(互斥)");
                    peer_->ForceDisable();
                    g_multiCastActive.store(false);
                }
                g_fireFullPowerActive.store(true);
            }
            else if (!enabled_ && prevEnabled)
            {
                g_fireFullPowerActive.store(false);
            }
        }
        ImGui::SameLine();
        ImGui::Checkbox(u8"显示技能/怪物##FireFullPower", &showLists_);
        ImGui::Checkbox(u8"按X触发##FireFullPower", &triggerOnX_);
        if (triggerOnX_)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.9f, 1.0f),
                               u8"<- 按住 X 键持续释放技能");
        }

        ImGui::SliderFloat(u8"作用距离(格)##FireFullPower", &distance_,         5.0f,   50.0f,  "%.0f");
        ImGui::SliderInt  (u8"轮询间隔(ms)##FireFullPower", &intervalMs_,        50,    2000);
        ImGui::SliderInt  (u8"每轮上限##FireFullPower",     &maxCastsPerTick_,    1,      30);
        ImGui::SliderInt  (u8"包间延时(ms)##FireFullPower", &perCastDelayMs_,     0,     200);
        ImGui::Checkbox  (u8"忽略客户端冷却##FireFullPower", &ignoreClientCooldown_);
        ImGui::Checkbox  (u8"干跑(只记日志不发包)##FireFullPower", &dryRun_);
        ImGui::Checkbox  (u8"稳健模式(等服务端ACK)##FireFullPower", &stableMode_);
        if (stableMode_)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.9f, 1.0f),
                               u8"<- 收到伤害包(GC_SKILL_CAST_RESULT)或%dms超时才下一炮",
                               kStableModeTimeoutMs);
        }
        if (dryRun_)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                               u8"<- 测试模式,不会真的发技能包");
        }

        // 爆破模式下才显示发包路径选择 — 非爆破职业走原本的技能轮换分支不受影响。
        uint32_t profForUI = GetProfession();
        bool     isBomberUI = (profForUI == kProfessionBomber || profForUI == kProfessionBlacksmith);
        if (isBomberUI)
        {
            const char *kBomberModeLabels[] = {u8"调用 CastSkill", u8"直接发包"};
            int         currentMode         = static_cast<int>(bomberMode_);
            if (ImGui::Combo(u8"投弹方式##FireFullPower", &currentMode,
                             kBomberModeLabels, IM_ARRAYSIZE(kBomberModeLabels)))
            {
                bomberMode_ = static_cast<BomberMode>(currentMode);
            }
            ImGui::SameLine();
            if (bomberMode_ == BomberMode::RawPacket)
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f),
                                   u8"<- 手填UINT32[]+0x6458E (邻居%dtile)",
                                   static_cast<int>(kBomberNeighborTileRadius));
            else
                ImGui::TextDisabled(u8"<- Net__BeginSend + Net__SendSkillPacket");
        }

        ImGui::Separator();
        size_t  skillCount = 0, monsterCount = 0, castsLastTick = 0;
        DWORD   lastTickAge = 0;
        std::vector<LearnedSkillInfo> skillSnap;
        std::vector<NearbyMonster>    monsterSnap;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            skillCount    = lastSkills_.size();
            monsterCount  = lastMonsters_.size();
            castsLastTick = castsLastTick_;
            lastTickAge   = lastTickMs_ ? (GetTickCount() - lastTickMs_) : 0;
            if (showLists_)
            {
                skillSnap   = lastSkills_;
                monsterSnap = lastMonsters_;
            }
        }

        if (!enabled_)
            ImGui::TextDisabled(u8"未启用 (worker 仍在运行,但不放技能)");
        else if (IsPausedByGuard())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               u8"已被 NPG 暂停 (附近有玩家)");
        else
        {
            uint32_t prof = GetProfession();
            bool isBomber = (prof == kProfessionBomber || prof == kProfessionBlacksmith);
            if (isBomber)
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f),
                                   u8"%s: [爆破模式] 周围怪物 %d, 上轮%s %d, 距上轮 %lums",
                                   dryRun_ ? u8"干跑中" : u8"运行中",
                                   static_cast<int>(monsterCount),
                                   dryRun_ ? u8"模拟" : u8"投弹",
                                   static_cast<int>(castsLastTick),
                                   static_cast<unsigned long>(lastTickAge));
            else
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                                   u8"%s: 可用技能 %d, 周围怪物 %d, 上轮%s %d, 距上轮 %lums",
                                   dryRun_ ? u8"干跑中" : u8"运行中",
                                   static_cast<int>(skillCount),
                                   static_cast<int>(monsterCount),
                                   dryRun_ ? u8"模拟" : u8"释放",
                                   static_cast<int>(castsLastTick),
                                   static_cast<unsigned long>(lastTickAge));
        }

        if (showLists_)
        {
            RenderSkillSnapshot(skillSnap);
            RenderMonsterSnapshot(monsterSnap);
        }
    }

    void OnShutdown() override
    {
        // 注意:OnShutdown 同时被 NPG 暂停和"用户/对方互斥关闭"两个路径调用。
        // 两种情况都把 atomic flag 清掉是安全的 — flag 只用于 UI 互斥决策。
        g_fireFullPowerActive.store(false);
    }

    void OnResume() override
    {
        if (!enabled_)
            return;
        // LoadState 时 enabled=true 走到这里 — 做一次互斥检查,免得两个模块的
        // LoadState 顺序导致两者都 active。
        if (peer_ && peer_->IsEnabled())
        {
            spdlog::info("GGTB::FireFullPower: OnResume 强制关闭多重施法(互斥)");
            peer_->ForceDisable();
            g_multiCastActive.store(false);
        }
        g_fireFullPowerActive.store(true);
    }

    // dllmain 在两个模块都构造完之后塞 peer 指针进来,免得这里 include
    // MultiCastModule.h 引循环。
    static void SetPeer(IModule *p) { peer_ = p; }

    std::string ConfigKey() const override { return "FireFullPower"; }
    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["distance"]             = distance_;
        j["intervalMs"]           = intervalMs_;
        j["maxCastsPerTick"]      = maxCastsPerTick_;
        j["perCastDelayMs"]       = perCastDelayMs_;
        j["ignoreClientCooldown"] = ignoreClientCooldown_;
        j["showLists"]            = showLists_;
        j["dryRun"]               = dryRun_;
        j["bomberMode"]           = static_cast<int>(bomberMode_);
        j["stableMode"]           = stableMode_;
        j["triggerOnX"]           = triggerOnX_;
    }
    void LoadState(const nlohmann::json &j) override
    {
        distance_             = j.value("distance",             distance_);
        intervalMs_           = j.value("intervalMs",           intervalMs_);
        maxCastsPerTick_      = j.value("maxCastsPerTick",      maxCastsPerTick_);
        perCastDelayMs_       = j.value("perCastDelayMs",       perCastDelayMs_);
        ignoreClientCooldown_ = j.value("ignoreClientCooldown", ignoreClientCooldown_);
        showLists_            = j.value("showLists",            showLists_);
        dryRun_               = j.value("dryRun",               dryRun_);
        // 老配置没有 bomberMode 字段时默认 EngineCall(0),保持当前行为不变 —
        // 任何非 0/1 的脏值同样降级为 EngineCall。
        int rawMode           = j.value("bomberMode", static_cast<int>(BomberMode::EngineCall));
        bomberMode_           = (rawMode == 1) ? BomberMode::RawPacket : BomberMode::EngineCall;
        // 老配置没有 stableMode 字段时默认 false,保持当前行为不变。
        stableMode_           = j.value("stableMode", stableMode_);
        // 老配置没有 triggerOnX 字段时默认 false,保持当前行为不变。
        triggerOnX_           = j.value("triggerOnX", triggerOnX_);
        // Migrate old world-unit defaults (100~2000) to the tile-space 5~50
        // slider used by the UI now. Anything outside the new band is clamped
        // silently to the default so saved configs don't resurrect a huge
        // radius that matches no target data.
        if (distance_ < 5.0f || distance_ > 50.0f)
            distance_ = 15.0f;
        IModule::LoadState(j);
    }

  private:
    void RenderSkillSnapshot(const std::vector<LearnedSkillInfo> &snap)
    {
        ImGui::Separator();
        ImGui::Text(u8"已学习技能 (%d):", static_cast<int>(snap.size()));
        constexpr ImGuiTableFlags kFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##FFPSkills", 4, kFlags, ImVec2(0, 160)))
        {
            ImGui::TableSetupColumn("skillId");
            ImGui::TableSetupColumn(u8"名字");
            ImGui::TableSetupColumn(u8"等级");
            ImGui::TableSetupColumn(u8"剩余冷却");
            ImGui::TableHeadersRow();
            for (auto &info : snap)
            {
                const auto &s = info.skill;
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%u", s.skillId);
                ImGui::TableNextColumn();
                if (info.name.empty())
                    ImGui::TextDisabled(u8"(?)");
                else
                    ImGui::TextUnformatted(info.name.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%u", s.skillLevel);
                ImGui::TableNextColumn();
                if (s.leftCoolDown > 0.0f)
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
                                       "%.2fs", s.leftCoolDown);
                else
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), u8"就绪");
            }
            ImGui::EndTable();
        }
    }

    void RenderMonsterSnapshot(const std::vector<NearbyMonster> &snap)
    {
        ImGui::Separator();
        ImGui::Text(u8"周围怪物 (%d):", static_cast<int>(snap.size()));
        constexpr ImGuiTableFlags kFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##FFPMonsters", 5, kFlags, ImVec2(0, 160)))
        {
            ImGui::TableSetupColumn("id");
            ImGui::TableSetupColumn(u8"距离");
            ImGui::TableSetupColumn(u8"等级");
            ImGui::TableSetupColumn("kind");
            ImGui::TableSetupColumn("hp");
            ImGui::TableHeadersRow();
            for (auto &m : snap)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%u",   m.monsterId);
                ImGui::TableNextColumn(); ImGui::Text("%.1f", m.distance);
                ImGui::TableNextColumn(); ImGui::Text("%u",   m.level);
                ImGui::TableNextColumn(); ImGui::Text("%u",   m.kind);
                ImGui::TableNextColumn(); ImGui::Text("%lld", static_cast<long long>(m.hp));
            }
            ImGui::EndTable();
        }
    }

    void WorkerLoop()
    {
        spdlog::info("GGTB::FireFullPower: worker thread started");
        DWORD lastHeartbeat = 0;
        while (!stop_.load())
        {
            if (enabled_ && !IsPausedByGuard())
            {
                // 如果启用了"按X触发"模式,检测X键是否按下
                if (triggerOnX_)
                {
                    // 使用 GetAsyncKeyState 高位检测 X 键当前是否按下(电平触发)
                    bool currX = (GetAsyncKeyState('X') & 0x8000) != 0;
                    if (currX)
                    {
                        // X 按下:按节流间隔持续释放技能
                        Tick();
                    }
                }
                else
                {
                    // 正常模式:按轮询间隔自动触发
                    Tick();
                }
            }

            DWORD now = GetTickCount();
            if (enabled_ && now - lastHeartbeat > 10000)
            {
                size_t sk = 0, mo = 0;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    sk = lastSkills_.size();
                    mo = lastMonsters_.size();
                }
                spdlog::info("GGTB::FireFullPower: heartbeat — skills={}, monsters={}, paused={}",
                             sk, mo, IsPausedByGuard());
                lastHeartbeat = now;
            }

            for (int i = 0; i < 10 && !stop_.load(); ++i)
                Sleep(10);
        }
        spdlog::info("GGTB::FireFullPower: worker thread exited");
    }

    // 稳健模式门:开关关闭直接放行;否则要么收到了上一次发包之后的伤害包,要么
    // 距上次发包已经超过 kStableModeTimeoutMs。dryRun 不阻挡——干跑本来就不会
    // 真撞掉线,而且这样测试日志能看清门控行为。
    bool StableModeAllowFire() const
    {
        if (!stableMode_)
            return true;
        if (lastSendStableTickMs_ == 0) // 头一炮无条件放行
            return true;
        DWORD lastAck = GGTB::NetLog::GetLastSkillResultTickMs();
        // GetTickCount 32-bit wraparound (~49.7 天) 用差值比较抗 wrap。
        if (lastAck != 0 && static_cast<int32_t>(lastAck - lastSendStableTickMs_) > 0)
            return true;
        DWORD now = GetTickCount();
        if (static_cast<int32_t>(now - lastSendStableTickMs_) >= static_cast<int32_t>(kStableModeTimeoutMs))
            return true;
        return false;
    }

    void Tick()
    {
        DWORD now = GetTickCount();
        if (now - lastTickMs_ < static_cast<DWORD>(intervalMs_))
            return;
        lastTickMs_ = now;

        auto skills      = GetLearnedSkills();
        auto allMonsters = GetNearbyMonsters(distance_);

        size_t casts = 0;

        // Bomber-class branch: when profession is 爆破(16) or 铁匠(6), the
        // entire normal skill rotation is replaced by bomb-throwing. This
        // mirrors the game's own AutoHunt__ChooseNextAction @ 0x5FD1C0 which
        // does `if (profession == 6 || profession == 16) ThrowBomb()`.
        //
        // MakeBomb(325) fires on its own CD — regardless of whether monsters
        // are nearby — so the stockpile tops up while we're walking between
        // fights. ThrowBomb(83) fires against nearby monsters when we have a
        // bomb in the bag. Both can happen in the same tick.
        uint32_t prof = GetProfession();
        if (prof == kProfessionBomber || prof == kProfessionBlacksmith)
        {
            // MakeBomb: look it up in the learned-skill snapshot and cast iff
            // learned + CD ready. ignoreClientCooldown_ bypasses the client
            // leftCoolDown gate (server still validates).
            for (auto &info : skills)
            {
                if (info.skill.skillId != kSkillIdMakeBomb)
                    continue;
                if (!ignoreClientCooldown_ && info.skill.leftCoolDown > 0.0f)
                    break;
                if (!StableModeAllowFire())
                    break; // 稳健模式:还没等到 ACK / 超时,本 tick 跳过
                CastMakeBomb(dryRun_);
                if (!dryRun_)
                    lastSendStableTickMs_ = GetTickCount();
                ++casts;
                if (perCastDelayMs_ > 0)
                    Sleep(static_cast<DWORD>(perCastDelayMs_));
                break;
            }

            // ThrowBomb: only when monsters are nearby AND we still have
            // per-tick budget left. bomberMode_ picks the wire path:
            //   EngineCall -> CastBombOnMonster (Net__SendSkillPacket variant dispatch)
            //   RawPacket  -> CastBombOnMonsterRaw (hand-built UINT32[] + 0x6458E)
            if (!allMonsters.empty())
            {
                for (size_t i = 0;
                     i < static_cast<size_t>(maxCastsPerTick_) && i < allMonsters.size() &&
                     casts < static_cast<size_t>(maxCastsPerTick_);
                     ++i)
                {
                    if (!StableModeAllowFire())
                        break; // 稳健模式:本 tick 已经发过一炮、还没等到 ACK,余下的目标下个 tick 再说
                    int sent = (bomberMode_ == BomberMode::RawPacket)
                                   ? CastBombOnMonsterRaw(allMonsters[i], allMonsters, dryRun_)
                                   : CastBombOnMonster   (allMonsters[i], dryRun_);
                    if (sent > 0)
                    {
                        casts += static_cast<size_t>(sent);
                        if (!dryRun_)
                            lastSendStableTickMs_ = GetTickCount();
                        if (perCastDelayMs_ > 0)
                            Sleep(static_cast<DWORD>(perCastDelayMs_));
                    }
                }
            }
        }
        else if (!skills.empty() && !allMonsters.empty())
        {
            // Normal (non-bomber) skill rotation — unchanged from before.
            for (auto &info : skills)
            {
                if (casts >= static_cast<size_t>(maxCastsPerTick_))
                    break;
                if (!info.skill.skillTable)
                    continue;
                if (!ignoreClientCooldown_ && info.skill.leftCoolDown > 0.0f)
                    continue;
                if (!StableModeAllowFire())
                    break; // 稳健模式跨所有发包路径 — 普通技能轮换也走同一个门

                int sent = CastSkill(info, allMonsters, dryRun_);
                if (sent > 0)
                {
                    casts += static_cast<size_t>(sent);
                    if (!dryRun_)
                        lastSendStableTickMs_ = GetTickCount();
                    if (perCastDelayMs_ > 0)
                        Sleep(static_cast<DWORD>(perCastDelayMs_));
                }
            }
        }

        std::lock_guard<std::mutex> lk(mutex_);
        lastSkills_     = std::move(skills);
        lastMonsters_   = std::move(allMonsters);
        castsLastTick_  = casts;
    }

    // Defaults intentionally CONSERVATIVE:
    //   - dryRun_ ON so the user sees what would be cast in ggtb.log without
    //     risking another disconnect on first run after a rebuild
    //   - 1 cast per 800ms with 120ms inter-packet sleep — well under the
    //     20pkt/s rate that prompted the original disconnect investigation
    //   - distance 15 tiles lines up with AutoPickup's tile-space slider and
    //     with Combat__GetAttackRangeSquared's hard cap (15^2). AoE/nuke casts
    //     still find a target in that band without resembling a half-map sweep.
    // Saved settings override these on next launch (LoadState).
    float      distance_             = 15.0f;
    int        intervalMs_           = 800;
    int        maxCastsPerTick_      = 1;
    int        perCastDelayMs_       = 120;
    bool       ignoreClientCooldown_ = false;
    bool       showLists_            = false;
    bool       dryRun_               = true;
    // 默认走老 EngineCall 路径,跟改这版前一致 — 想试 RawPacket 必须手动切。
    BomberMode bomberMode_           = BomberMode::EngineCall;
    // 默认关 — 想要扛掉线就手动开。开了之后每次发包前必须等到下面任一条件:
    //   1) NetLog::GetLastSkillResultTickMs() > lastSendStableTickMs_  (服务端 ACK 了某个技能)
    //   2) GetTickCount() - lastSendStableTickMs_ >= kStableModeTimeoutMs (硬超时兜底)
    bool       stableMode_           = false;
    DWORD      lastSendStableTickMs_ = 0; // 上一次实际发包 (engine/raw/普通技能) 的时间戳
    // 默认关 — 想要按X触发就手动开。开了之后按住 X 键持续释放技能。
    bool       triggerOnX_           = false;

    // 互斥伙伴:多重施法。dllmain 构造之后 SetPeer 注入。
    inline static IModule *peer_ = nullptr;

    DWORD  lastTickMs_     = 0;
    size_t castsLastTick_  = 0;

    std::mutex                       mutex_;
    std::vector<LearnedSkillInfo>    lastSkills_;
    std::vector<NearbyMonster>       lastMonsters_;

    std::atomic<bool> stop_{false};
    std::thread       worker_;
};

} // namespace GGTB
