#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "../util/NetLog.h"
#include "../util/PatternResolver.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace GGTB
{

// 自动钓鱼 —— 全程纯发包驱动,不调用任何游戏内钓鱼函数(Fishing_CastStateTick
// 等一概不碰)。所有包都自己拼好字节,经游戏最底层的明文发送 Net__SendPacket_Plaintext
// (PatternResolver "NetSendPacketPlaintext")发出去 —— 这是用户明确授权可以 call 的发包口。
//
// 线上钓鱼会话其实只由三种 CG 包构成(IDA 静态确认,见各 Send* 注释):
//   411047 CG_FISHING_BAIT —— 抛竿/放饵,开一局。body=[baitSlot, *(localUser+408),
//           coordX, coordY],totalLen=24。baitSlot 是**背包槽位 wire 编码(slotIndex+13)**,
//           不是物品模板 id(IDA 实证 CG_SendFishingBait_411047,收鱼端 521102 -13 反推槽位)。
//           proto 换成 411174 是备用饵类型(body 结构相同)。
//   412048 CG_FISHING_REEL —— "中鱼"/收竿,body=0,totalLen=12。每隔 N 秒发一次,
//           服务端回 521102(收鱼),其 recv handler 自动入袋 + 扣饵 + 重新 arm 咬钩位,
//           所以只要我们按节奏持续发 412048,循环自维持。
//   411023 CG_ACTION_CANCEL mode=1 —— 停钓/动作复位,body=1,totalLen=12。每次抛竿前
//           先发一发(与游戏 Fishing_CastStateTick 的 cancel→cast 配对一致),关模块也发。
//
// 流程:启用后 worker 先发一次 411047(开局/放饵),之后每 intervalSec_(30~45 可调)
// 发一次 412048。可选每 recastEveryReels_ 次重新抛竿一次(补饵/防会话过期,默认开)。
//
// 断线防护(实测踩坑):抛竿包带本地坐标,而场景切换/角色重载窗口里 localUser 坐标会
// 瞬时读成 (0,0),带 (0,0) 抛竿会被服务端判违规直接踢线。Tick 每拍先 LocalStateValid()
// 校验坐标非 (0,0) 才发包,SendCastSEH 内再自守一次。更进一步:刚启用/刚换图/刚重启进
// 游戏时人物可能还没加载稳,故"角色信息可见(LocalStateValid 通过)"后再压 5s 稳定期才
// 首抛(awaitingCharInfo_ + castHoldUntilMs_)。本模块明确不参与 NPG(CanAutoPause()==false):
// 中途被拍暂停再恢复的"抛竿风暴"反而更易撞上抖动窗口的 (0,0) 坐标踢线,索性全程自管节奏。
//
// 鱼饵选择:UI 填一个模糊物品名(子串,持久化)。worker 每拍扫背包 GetBagItems(),
// 找名字包含该子串、且 count>0 的格子,用它的 bagId(=背包槽位 wire 编码 slotIndex+13)
// 当 411047 的 bait 字段 —— 不是物品模板 id。当前格用 bagId 粘住,直到中鱼扣饵把它扣空
// (count==0/格消失)再按 slotIndex 升序换下一个匹配格 —— 即"用完自动换饵"。无匹配则整拍
// 不抛竿。见 ResolveBaitSlot。
//
// 换图重抛:worker 每拍 GetCurrentMapId() 比对 lastMapId_,变了就先发 411023 收杆、复位
// 本地会话,并置 awaitingCharInfo_ —— 等新场景"角色信息可见"后再压 5s 才重新开局抛竿。
// 换图会让服务端钓鱼会话作废,且新场景坐标要等一会儿才稳定(否则带 (0,0) 抛竿被判违规
// 踢线)。见 DetectMapChange / Tick。
//
// 发包做法:Engage 时把 PatternResolver 解析出的 Net__SendPacket_Plaintext 原始入口
// 地址缓存为 sendFn_。NetLog 用 DetourAttach 把该入口就地改成 jmp,所以我们直接 call
// 这个地址会自动走 NetLog 的 hook(包被记进 sendlog),且不会自我递归。
//
// 中鱼自关保护(实测踩坑 + IDA 静态确认):收鱼包 521102 的 handler
// GC_OnFishingCatch_521102(0x891810)开头有一道一致性/反作弊保护 —— 客户端若不在
// 钓鱼会话态(dword_ED3D74+0x31==0)、又没开钓鱼窗口(dword_D67CC4!=0x10)却收到 521102,
// 会 SendMessageA(主窗口, WM_CLOSE) 自关。纯发包从不进钓鱼态/开窗口,于是每次中鱼都触发
// 自关(鱼其实已入袋,因入袋逻辑在 WM_CLOSE 调用之后、且 WM_CLOSE 异步)。Engage 时把
// 0x891824 的 jnz(75)就地改成 jmp(EB)跳过这处 WM_CLOSE,Disengage 还原;对正常钓鱼零
// 副作用(正常 +49!=0 本就 jnz 跳走)。详见 PatternResolver "FishingCatchGuardJnz"。
//
// 注意(留给使用者,无法静态验证、需实测):游戏自带的钓鱼界面 tick(Fishing_CastStateTick)
// 一旦在跑也会自己发 412048,会和本模块叠加成双发。建议关掉游戏钓鱼窗口,纯靠本模块驱动;
// 间隔取 30~45s 与引擎自身 45s 阈值同量级,基本不触发服务端节流。
class AutoFishingModule : public IModule
{
  public:
    AutoFishingModule() : IModule(u8"自动钓鱼")
    {
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~AutoFishingModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
        SetCatchGuard(false); // 别把游戏 .text 留在已打补丁的状态
        SetPoseGuard(false);
    }

    // 本模块明确不参与 NPG:中途被拍暂停、恢复时的"抛竿风暴"反而更容易撞上场景
    // 抖动窗口的 (0,0) 坐标被判违规踢线。索性全程自管节奏,不让 NPG 介入。
    bool CanAutoPause() const override { return false; }

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"启用##AutoFishing", &enabled_))
        {
            if (enabled_) Engage();
            else          Disengage();
        }

        ImGui::SliderInt(u8"中鱼间隔(秒)##AutoFishing", &intervalSec_, 30, 45);
        if (intervalSec_ < 30) intervalSec_ = 30;
        if (intervalSec_ > 45) intervalSec_ = 45;

        // 鱼饵按模糊名匹配:扫背包找名字包含该子串、且还有数量的格当饵,用完自动换格。
        // InputText 在 UI 线程写 baitNameBuf_,改动时同步进 mutex 快照供 worker 读。
        if (ImGui::InputText(u8"鱼饵名称(模糊)##AutoFishing", baitNameBuf_, sizeof(baitNameBuf_)))
        {
            std::lock_guard<std::mutex> lk(baitMtx_);
            baitFilter_ = baitNameBuf_;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(u8"(名字含此串即用,用完自动换)");

        ImGui::Checkbox(u8"备用鱼饵类型(411174)##AutoFishing", &altBait_);
        ImGui::SliderInt(u8"每N次重新抛竿(0=不)##AutoFishing", &recastEveryReels_, 0, 30);

        ImGui::Checkbox(u8"抛竿后微移破除远端钓鱼姿态##AutoFishing", &breakPose_);
        ImGui::SameLine();
        ImGui::TextDisabled(u8"(若微移会取消钓鱼则关掉)");

        ImGui::Checkbox(u8"抛竿前检查附近真人(NPG)##AutoFishing", &castGuardNearby_);
        ImGui::SameLine();
        ImGui::TextDisabled(u8"(附近有非白名单真人时暂不抛竿;中鱼不受影响)");
        if (castGuardNearby_)
        {
            ImGui::SliderFloat(u8"抛竿安全距离##AutoFishing", &castGuardDist_, 50.0f, 2000.0f, "%.0f");
            if (castGuardDist_ < 50.0f) castGuardDist_ = 50.0f;
            if (castGuardDist_ > 2000.0f) castGuardDist_ = 2000.0f;
        }

        ImGui::Separator();
        if (!enabled_)
        {
            ImGui::TextDisabled(u8"未启用 (worker 仍在运行,但不发包)");
        }
        else if (baitNameBuf_[0] == '\0')
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                               u8"请填写鱼饵名称(模糊匹配背包物品)");
        }
        else if (curBaitItemId_.load(std::memory_order_relaxed) == 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                               u8"背包无名字含\"%s\"且有数量的物品", baitNameBuf_);
        }
        else
        {
            DWORD last  = lastActionMs_.load(std::memory_order_relaxed);
            DWORD ageMs = last ? (GetTickCount() - last) : 0;
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               u8"运行中: 抛竿 %llu, 中鱼 %llu, 距上次 %lums",
                               static_cast<unsigned long long>(castCount_.load(std::memory_order_relaxed)),
                               static_cast<unsigned long long>(reelCount_.load(std::memory_order_relaxed)),
                               static_cast<unsigned long>(ageMs));
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               u8"当前饵: id=%u 余 %u",
                               static_cast<unsigned>(curBaitItemId_.load(std::memory_order_relaxed)),
                               static_cast<unsigned>(curBaitCount_.load(std::memory_order_relaxed)));
        }
    }

    // 本模块不参与 NPG(CanAutoPause()==false),OnShutdown 只会在真正关闭
    // (ForceDisable / 退出)时被调到 —— 直接收杆复位并还原补丁。
    void OnShutdown() override
    {
        Disengage();
    }

    // OnResume 现在只来自 config 载入启用(NPG 不再暂停本模块)。等同手动勾选:走
    // 完整 Engage —— 解析地址、复位会话、打补丁、置 awaitingCharInfo_,让重启后的
    // 首抛同样等到角色信息可见再压 5s(否则人物没加载稳就抛竿会失败/踢线)。
    void OnResume() override
    {
        if (enabled_)
            Engage();
    }

    std::string ConfigKey() const override { return "AutoFishing"; }

    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["intervalSec"]      = intervalSec_;
        j["baitName"]         = baitNameBuf_;
        j["altBait"]          = altBait_;
        j["recastEveryReels"] = recastEveryReels_;
        j["breakPose"]        = breakPose_;
        j["castGuardNearby"]  = castGuardNearby_;
        j["castGuardDist"]    = castGuardDist_;
    }

    void LoadState(const nlohmann::json &j) override
    {
        intervalSec_      = j.value("intervalSec",      intervalSec_);
        altBait_          = j.value("altBait",          altBait_);
        recastEveryReels_ = j.value("recastEveryReels", recastEveryReels_);
        breakPose_        = j.value("breakPose",        breakPose_);
        castGuardNearby_  = j.value("castGuardNearby",  castGuardNearby_);
        castGuardDist_    = j.value("castGuardDist",    castGuardDist_);
        if (castGuardDist_ < 50.0f) castGuardDist_ = 50.0f;
        if (castGuardDist_ > 2000.0f) castGuardDist_ = 2000.0f;
        std::string nm = j.value("baitName", std::string());
        size_t n = nm.size();
        if (n >= sizeof(baitNameBuf_)) n = sizeof(baitNameBuf_) - 1;
        memcpy(baitNameBuf_, nm.data(), n);
        baitNameBuf_[n] = '\0';
        {
            std::lock_guard<std::mutex> lk(baitMtx_);
            baitFilter_ = baitNameBuf_;
        }
        if (intervalSec_ < 30) intervalSec_ = 30;
        if (intervalSec_ > 45) intervalSec_ = 45;
        IModule::LoadState(j);
    }

  private:
    // Net__SendPacket_Plaintext(this=CGameClient, pkt, len) —— __thiscall,用 __fastcall
    // shim 调:ecx=client, edx 忽略, pkt 指向 {u32 totalLen, u32 proto, ...body}, len=totalLen。
    using fnSendPacketPT = int(__fastcall *)(void *ecx, void *edx, void *pkt, int len);

    // enabled_ / pausedByGuard_ 继承自 IModule —— 不要在这里重声明,否则会 shadow
    // 基类字段,checkbox / 持久化 / NPG 全部失灵。
    int       intervalSec_      = 35;
    bool      altBait_          = false;
    int       recastEveryReels_ = 6;
    bool      breakPose_        = true;  // 抛竿后微移破除远端钓鱼姿态(见 NudgeBreakPose)

    // 抛竿 NPG 门:抛竿(及其内嵌的破姿态微移)是一个原子动作,只在周围无非白名单
    // 真人时才执行(同 NPG 判定)。中鱼/收杆不受此门影响。本模块 CanAutoPause()==false,
    // 故 NPG 不会全局暂停我们 —— 这里自带一份"附近真人"判定只 gate 抛竿。
    // castGuardDist_ 是自己的安全距离(NPG 的 distanceThreshold_ 私有,故独立一份,默认与其一致)。
    bool      castGuardNearby_  = true;
    float     castGuardDist_    = 600.0f;

    // 鱼饵按模糊名匹配(替代旧的直填 itemId)。baitNameBuf_ 是 UI 线程的 InputText
    // 缓冲;worker 只读 mutex 快照 baitFilter_(避免跨线程读半写 char 数组)。选中的
    // 饵格用 bagId 粘住,用完(count==0 或格消失)自动换到下一个名字匹配的格 —— 见 ResolveBaitSlot。
    mutable std::mutex    baitMtx_;
    char                  baitNameBuf_[64] = {0}; // UI 线程独占
    std::string           baitFilter_;            // mutex 保护,worker 读
    uint32_t              curBaitBagId_  = 0;      // worker-only:当前饵格 (item+0x00=wire 槽位编码 slotIndex+13);411047 bait 字段实际用它
    std::atomic<uint32_t> curBaitItemId_{0};       // UI 读:物品模板 id,仅供显示(非发包值)
    std::atomic<uint32_t> curBaitCount_{0};        // UI 读:当前饵格剩余数量

    fnSendPacketPT sendFn_           = nullptr; // 解析出的 Net__SendPacket_Plaintext 入口(走 NetLog hook)
    uintptr_t      gameClientPtrAddr_ = 0;      // &g_pGameClient,解引用拿当前 client
    uintptr_t      localUserPtrAddr_  = 0;      // &g_pLocalUser,解引用拿当前 localUser

    // worker 状态(只 worker 线程读写,lastActionMs_/计数 例外:UI 读)
    bool                sessionStarted_ = false;
    int                 reelsSinceCast_ = 0;
    DWORD               nextActionMs_   = 0;
    uint32_t            lastMapId_        = 0;    // 地图切换检测基准(0=未知/撕裂读,忽略)
    DWORD               castHoldUntilMs_  = 0;    // !=0:此 tick 前不抛竿(启用/换图后的 5s 稳定期)
    bool                awaitingCharInfo_ = true; // 待角色信息可见后再起 5s 首抛倒计时;Engage/换图置位
    std::atomic<DWORD>    lastActionMs_{0};
    std::atomic<uint64_t> castCount_{0};
    std::atomic<uint64_t> reelCount_{0};

    std::atomic<bool> stop_{false};
    std::thread       worker_;

    // 收鱼自关保护补丁状态(0x891824 jnz->jmp,单字节)。Engage 打、Disengage/dtor 还原。
    BYTE catchGuardOrig_     = 0;
    bool catchGuardCaptured_ = false;
    bool catchGuardPatched_  = false;

    // 钓鱼姿态广播补丁状态(0x94EFE0 call->5×NOP)。Engage 打、Disengage/dtor 还原。
    BYTE poseGuardOrig_[5]   = {0};
    bool poseGuardCaptured_  = false;
    bool poseGuardPatched_   = false;

    // 地址解析,幂等。这些都是静态地址(&g_pXxx / 函数入口),解析一次即可长期复用。
    bool EnsureResolved()
    {
        if (gameClientPtrAddr_ && localUserPtrAddr_ && sendFn_)
            return true;
        gameClientPtrAddr_ = PatternResolver::Get("GameClientPtr");
        localUserPtrAddr_  = PatternResolver::Get("LocalUserPtr");
        sendFn_            = reinterpret_cast<fnSendPacketPT>(
            PatternResolver::Get("NetSendPacketPlaintext"));
        return gameClientPtrAddr_ && localUserPtrAddr_ && sendFn_;
    }

    // 用户手动勾选启用 —— 唯一会复位会话状态的入口(NPG 恢复走 OnResume,不复位)。
    void Engage()
    {
        if (!EnsureResolved())
        {
            spdlog::error("GGTB::AutoFishing: 关键地址未解析 client={:x} localUser={:x} send={}",
                          gameClientPtrAddr_, localUserPtrAddr_, reinterpret_cast<void *>(sendFn_));
            enabled_ = false;
            return;
        }
        sessionStarted_   = false;
        reelsSinceCast_   = 0;
        nextActionMs_     = 0;
        lastMapId_        = 0;    // 重新建立换图基准
        castHoldUntilMs_  = 0;
        awaitingCharInfo_ = true; // 等角色信息可见后再 5s 首抛(刚进游戏/刚启用人物可能没加载稳)
        SetCatchGuard(true); // 跳过 521102 handler 的 WM_CLOSE 自关(纯发包中鱼防闪退)
        SetPoseGuard(true);  // 抛竿后不再被广播摆成钓鱼姿态
        spdlog::info("GGTB::AutoFishing: engaged interval={}s recast/{} bait=\"{}\"",
                     intervalSec_, recastEveryReels_, baitNameBuf_);
    }

    void Disengage()
    {
        if (sessionStarted_)
        {
            SendStop();
            sessionStarted_ = false;
        }
        SetCatchGuard(false);
        SetPoseGuard(false);
        spdlog::info("GGTB::AutoFishing: disengaged");
    }

    void WorkerLoop()
    {
        spdlog::info("GGTB::AutoFishing: worker thread started");
        while (!stop_.load())
        {
            if (enabled_ && !IsPausedByGuard())
                Tick();

            for (int i = 0; i < 10 && !stop_.load(); ++i)
                Sleep(10);
        }
        spdlog::info("GGTB::AutoFishing: worker thread exited");
    }

    // 抛竿 NPG 门:开启检查时,附近(castGuardDist_ 内)存在非白名单真人则禁止抛竿。
    // GetAroundPlayers 已排除自己 + 白名单玩家,故返回非空即"有需要回避的真人"。
    // 抛竿失败/被踢时整片读内存可能撕裂,GetAroundPlayers 自带 SEH,这里直接调。
    bool NearbyBlocksCast()
    {
        if (!castGuardNearby_)
            return false;
        return !GetAroundPlayers(GetLocalPlayerName(), castGuardDist_).empty();
    }

    void Tick()
    {
        // 换图:服务端钓鱼会话随场景作废。检测到换图先收杆(411023 复位)+ 复位本地会话
        // + 设 5s 冷却,等新场景坐标稳定再抛竿(避免带 (0,0) 抛竿被判违规踢线)。
        DetectMapChange();

        uint32_t baitSlot = ResolveBaitSlot();
        if (baitSlot == 0)
            return; // 没匹配的饵(名字空 / 背包无货),等填名或补货

        // 场景切换/角色重载窗口里 localUser 坐标会瞬时读成 (0,0),此刻发任何包都没意义
        // 且抛竿带 (0,0) 会被服务端判违规踢线。无效就整拍静默跳过、不推进 schedule,
        // 等状态恢复后自然续上(sessionStarted_ 保留)。
        if (!LocalStateValid())
            return;

        DWORD now = GetTickCount();

        // 启用/换图后:上面 LocalStateValid() 一旦通过即视为"角色信息可见",此刻才起
        // 5s 稳定期倒计时(人物刚加载坐标可能还在跳,过早抛竿会失败/踢线)。倒计时内不抛竿。
        if (awaitingCharInfo_)
        {
            castHoldUntilMs_  = now + 5000;
            awaitingCharInfo_ = false;
            spdlog::info("GGTB::AutoFishing: char info visible, first cast in 5s");
        }
        if (castHoldUntilMs_ != 0)
        {
            if (static_cast<int>(now - castHoldUntilMs_) < 0)
                return;
            castHoldUntilMs_ = 0;
        }

        if (!sessionStarted_)
        {
            // 抛竿+破姿态微移是一个原子动作,受 NPG 门保护:附近有非白名单真人就整拍跳过、
            // 保持 sessionStarted_=false,等人走开/进白名单后下一拍再抛(中鱼不走这条路)。
            if (NearbyBlocksCast())
                return;
            // 抛竿没真正发出去(指针未就绪/未连线/坐标无效)就别置位,下一拍重试。
            if (SendCast(baitSlot))
            {
                sessionStarted_ = true;
                reelsSinceCast_ = 0;
                ScheduleNext(now);
            }
            return;
        }

        if (static_cast<int>(now - nextActionMs_) < 0)
            return;

        // 到点:够 recast 次数就重新抛竿(补饵/续会话),否则收竿中鱼。
        if (recastEveryReels_ > 0 && reelsSinceCast_ >= recastEveryReels_)
        {
            // 重新抛竿同样是原子抛竿动作,受 NPG 门:附近有真人就不抛、也不 ScheduleNext,
            // 下一拍继续重试,等周围干净再续会话(此刻不收杆 —— 收杆只在非 recast 拍)。
            if (NearbyBlocksCast())
                return;
            // 重新抛竿没发出去就保留计数,下一拍再试,别误清成 0。
            if (SendCast(baitSlot))
                reelsSinceCast_ = 0;
        }
        else
        {
            SendReel();
            ++reelsSinceCast_;
        }
        ScheduleNext(now);
    }

    // 间隔基准 intervalSec_,再叠 0~3s 随机抖动,稀释固定周期指纹。
    void ScheduleNext(DWORD now)    {
        DWORD jitter = static_cast<DWORD>(rand() % 3000);
        nextActionMs_ = now + static_cast<DWORD>(intervalSec_) * 1000 + jitter;
        lastActionMs_.store(now, std::memory_order_relaxed);
    }

    // 换图检测:GetCurrentMapId() 在加载中可能撕裂读成 0,故只在 lastMapId_ 与当前值都
    // 非 0 且不同时才判为切换(过滤瞬态)。切换时收杆 + 复位会话 + 设 5s 冷却。
    void DetectMapChange()
    {
        uint32_t mapNow = GetCurrentMapId();
        if (mapNow == 0)
            return;
        if (lastMapId_ != 0 && mapNow != lastMapId_)
        {
            SendStop(); // 411023 收杆/动作复位(无坐标,换图瞬间坐标可能为 0 也安全)
            sessionStarted_   = false;
            reelsSinceCast_   = 0;
            castHoldUntilMs_  = 0;
            awaitingCharInfo_ = true; // 等新场景角色信息稳定(坐标非0)后再 5s 首抛
            spdlog::info("GGTB::AutoFishing: map {} -> {}, reel in, await char info then recast",
                         lastMapId_, mapNow);
        }
        lastMapId_ = mapNow;
    }

    // 扫背包按模糊名选饵。当前格仍有货且名字仍匹配 → 粘住(避免每拍换格);否则按
    // slotIndex 升序选下一个名字含 filter、count>0 的格。返回该格的 bagId —— 即 411047
    // bait 字段要的**背包槽位 wire 编码(=slotIndex+13)**,不是物品模板 id(IDA 实证:
    // CG_SendFishingBait_411047 body[0]=slotIndex+13,收鱼端 521102 再 -13 反推槽位)。
    // itemId 只存进 curBaitItemId_ 给 UI 显示。无匹配返回 0(占用格 bagId≥13,故 0 可当
    // "无饵"哨兵)。每拍调用 —— 中鱼扣饵令当前格 count 归零后自动跳到下一个匹配格,实现
    // "用完自动换饵"。GetBagItems 是 SEH 包裹的纯内存读,worker 可调。
    uint32_t ResolveBaitSlot()
    {
        std::string filter;
        {
            std::lock_guard<std::mutex> lk(baitMtx_);
            filter = baitFilter_;
        }
        if (filter.empty())
        {
            curBaitItemId_.store(0, std::memory_order_relaxed);
            curBaitCount_.store(0, std::memory_order_relaxed);
            return 0;
        }
        std::vector<BagItemInfo> items = GetBagItems();
        for (const BagItemInfo &it : items) // 粘住当前格
        {
            if (it.bagId == curBaitBagId_ && it.count > 0 &&
                it.name.find(filter) != std::string::npos)
            {
                curBaitItemId_.store(it.itemId, std::memory_order_relaxed);
                curBaitCount_.store(it.count, std::memory_order_relaxed);
                return it.bagId;
            }
        }
        for (const BagItemInfo &it : items) // 用完/丢失则换下一个匹配格
        {
            if (it.count > 0 && it.name.find(filter) != std::string::npos)
            {
                curBaitBagId_ = it.bagId;
                curBaitItemId_.store(it.itemId, std::memory_order_relaxed);
                curBaitCount_.store(it.count, std::memory_order_relaxed);
                return it.bagId;
            }
        }
        curBaitBagId_ = 0;
        curBaitItemId_.store(0, std::memory_order_relaxed);
        curBaitCount_.store(0, std::memory_order_relaxed);
        return 0;
    }

    bool SendCast(uint32_t baitSlot)
    {
        void *client    = DerefPtr(gameClientPtrAddr_);
        void *localUser = DerefPtr(localUserPtrAddr_);
        // 与游戏 Fishing_CastStateTick 一致:抛竿前先发 411023(动作复位/关上一局),
        // 否则服务端可能因上一局会话未结清而判异常。坐标已在 Tick::LocalStateValid
        // 校验过,SendCastSEH 内再自守一次(校验到发包之间坐标可能翻 0)。
        SendDwordSEH(sendFn_, client, 411023, 1);
        int rv = SendCastSEH(sendFn_, client, localUser, baitSlot, altBait_);
        if (rv > 0)
        {
            castCount_.fetch_add(1, std::memory_order_relaxed);
            spdlog::info("GGTB::AutoFishing: cast {} baitSlot={}", altBait_ ? 411174 : 411047, baitSlot);
            if (breakPose_)
            {
                // 本地 actorId = *(localUser+408)(抛竿包 w[3] 同一字段,服务端原样回显在
                // 钓鱼姿态广播里)。读不到就传 0 → NudgeBreakPose 跳过"等广播"直接微移兜底。
                uint32_t selfActorId = ReadLocalActorId(localUser);
                NudgeBreakPose(selfActorId); // 等钓鱼姿态广播到达后再确认微移,覆盖远端钓鱼姿态
            }
            return true;
        }
        return false;
    }

    static uint32_t ReadLocalActorId(void *localUser)
    {
        if (!localUser)
            return 0;
        __try
        {
            return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(localUser) + 408);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    // 抛竿后微移,破除"远端看到的钓鱼姿态"。服务端会把抛竿玩家广播成钓鱼 motion(27),
    // 别的客户端因此看到我们在钓鱼(本地姿态已被 SetPoseGuard 抹掉,远端看得到)。
    //
    // 时序门(关键): 必须等服务端真的把"我"广播成钓鱼姿态之后再移动。抢在广播之前移动
    // 会被随后到来的钓鱼广播重新盖回钓鱼姿态。recv 端 511058(entity-action)case 3/13 =
    // 钓鱼动作即"远端被告知我在钓鱼"的那一刻 —— 轮询 NetLog::GetLastSelfFishingPoseTickMs
    // (按本地 actorId 过滤),看到一条比抛竿更新的广播才放行;等不到(~1.5s)就兜底直接移动
    // (有的版本可能不回显本地、或被 SetPoseGuard 链路影响,不能死等)。
    //
    // 确认移动(关键): 光发一条 MoveTo 不够 —— 实测过 CG_MOVE 目标点跟抛竿位置一模一样
    // (引擎判定原地/同 tile 直接吞掉),等于没动,远端还在钓鱼姿态。所以放行后立刻记录
    // 当前 tile (baseX,baseY) 作基准,从周围 1 格起向外递进半径,逐方向试 MoveTo,每次发包
    // 后短轮询 GetLocalPosition,直到 floor(pos) 不再等于 base tile 才算确认移动、收手。
    //
    // 可达判定复用 AutoFlee 的 IsTileWalkable(引擎碰撞表 Map__IsBlocked)。MoveTo 即引擎
    // SetAfterAction,AutoFlee 已在 worker 线程调它,非主线程安全;它是移动函数、非钓鱼
    // 函数,不违反本模块"不碰钓鱼内部函数"的原则。
    // 注意(无法静态验证、需实测):若微移会让服务端取消钓鱼会话(中鱼循环停),UI 关掉
    // "抛竿后微移破除远端钓鱼姿态"即可。
    void NudgeBreakPose(uint32_t selfActorId)
    {
        // ---- 时序门: 等服务端把"我"广播成钓鱼姿态再动 ----
        // 以进入本函数的时刻为界,只认之后到达的钓鱼姿态广播(抛竿包刚发出,广播随后才回)。
        const DWORD gateStart = GetTickCount();
        if (selfActorId != 0)
        {
            bool sawBroadcast = false;
            while (GetTickCount() - gateStart <= 1500) // 兜底上限 ~1.5s,等不到就直接移动
            {
                if (stop_.load())
                    return;
                DWORD poseMs = NetLog::GetLastSelfFishingPoseTickMs(selfActorId);
                // poseMs 是该广播到达的 tick;比 gateStart 新即"本次抛竿后才广播的"。
                if (poseMs != 0 && static_cast<int>(poseMs - gateStart) >= 0)
                {
                    sawBroadcast = true;
                    break;
                }
                Sleep(10);
            }
            spdlog::info("GGTB::AutoFishing: pose-break gate {} ({}ms)",
                         sawBroadcast ? "broadcast seen" : "timeout, nudging anyway",
                         static_cast<unsigned long>(GetTickCount() - gateStart));
        }

        // ---- 广播已到(或等超时兜底): 此刻才记录基准位置, 然后确认移动 ----
        float lx = 0.0f, ly = 0.0f, lz = 0.0f;
        if (!GetLocalPosition(lx, ly, lz))
            return;
        const int baseX = static_cast<int>(std::floor(lx));
        const int baseY = static_cast<int>(std::floor(ly));

        // 8 邻接方向 (顺时针)。随机起点避免每次都先往同一方向,远端轨迹更自然。
        static const int kDirX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
        static const int kDirY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        const int startDir = rand() % 8;

        const DWORD budgetStart = GetTickCount();
        const DWORD budgetMs    = 3000; // 总预算: 3s 还没动就放弃这拍

        // 半径从 1 向外递进: 先试最近的一格,走不动(同 tile 被吞)再往远试。
        for (int r = 1; r <= 8; ++r)
        {
            for (int di = 0; di < 8; ++di)
            {
                if (stop_.load())
                    return;
                if (GetTickCount() - budgetStart > budgetMs)
                {
                    spdlog::info("GGTB::AutoFishing: pose-break nudge timed out, no move");
                    return;
                }

                const int dir = (startDir + di) % 8;
                const int tx  = baseX + kDirX[dir] * r;
                const int ty  = baseY + kDirY[dir] * r;
                if (!IsTileWalkable(tx, ty))
                    continue;

                float fx = static_cast<float>(tx) + 0.5f; // tile 中心,避免落边界格
                float fy = static_cast<float>(ty) + 0.5f;
                if (!MoveTo(fx, fy, /*action=*/1, /*targetId=*/0))
                    continue;

                // 短轮询确认位置真的变了 (~300ms 窗口)。变了就算这拍成功,收手。
                for (int w = 0; w < 30; ++w)
                {
                    if (stop_.load())
                        return;
                    Sleep(10);
                    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
                    if (!GetLocalPosition(cx, cy, cz))
                        continue;
                    if (static_cast<int>(std::floor(cx)) != baseX ||
                        static_cast<int>(std::floor(cy)) != baseY)
                    {
                        spdlog::info("GGTB::AutoFishing: pose-break confirmed move "
                                     "({},{})->({:.1f},{:.1f}) r={}",
                                     baseX, baseY, cx, cy, r);
                        return;
                    }
                }
                // 这格发了但没动(被吞),继续下一方向/更大半径。
            }
        }
        spdlog::info("GGTB::AutoFishing: pose-break nudge gave up (all dirs blocked/ignored)");
    }

    // 发包前的就绪+坐标有效性检查。指针缺一 / 坐标 (0,0) 都判无效。读坐标要 SEH 包
    // (场景切换瞬间 localUser 可能正在撕裂)。
    bool LocalStateValid() const
    {
        void *cl = DerefPtr(gameClientPtrAddr_);
        void *lu = DerefPtr(localUserPtrAddr_);
        if (!sendFn_ || !cl || !lu)
            return false;
        return CoordsValidSEH(lu);
    }

    static bool CoordsValidSEH(void *localUser)
    {
        __try
        {
            uint8_t *lu = static_cast<uint8_t *>(localUser);
            uint32_t x = *reinterpret_cast<uint32_t *>(lu + 412);
            uint32_t y = *reinterpret_cast<uint32_t *>(lu + 416);
            return !(x == 0 && y == 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void SendReel()
    {
        void *client = DerefPtr(gameClientPtrAddr_);
        int rv = SendDwordSEH(sendFn_, client, 412048, 0);
        if (rv > 0)
            reelCount_.fetch_add(1, std::memory_order_relaxed);
    }

    void SendStop()
    {
        void *client = DerefPtr(gameClientPtrAddr_);
        SendDwordSEH(sendFn_, client, 411023, 1);
    }

    static void *DerefPtr(uintptr_t addr)
    {
        if (!addr) return nullptr;
        return *reinterpret_cast<void **>(addr);
    }

    // 521102 收鱼 handler(GC_OnFishingCatch_521102 @ 0x891810)的自关保护补丁。
    // 纯发包中鱼时客户端不在合法钓鱼态,handler 会 SendMessageA(主窗口, WM_CLOSE)
    // 把游戏关掉。把 0x891824 的 jnz(75)就地改成 jmp(EB),无条件跳过那处 WM_CLOSE。
    // 见 PatternResolver "FishingCatchGuardJnz"。幂等:on==当前态直接返回。
    void SetCatchGuard(bool on)
    {
        if (on == catchGuardPatched_)
            return;
        uintptr_t addr = PatternResolver::Get("FishingCatchGuardJnz");
        if (!addr)
        {
            spdlog::error("GGTB::AutoFishing: FishingCatchGuardJnz 未解析");
            return;
        }
        DWORD oldProt = 0;
        if (!VirtualProtect(reinterpret_cast<void *>(addr), 1, PAGE_EXECUTE_READWRITE, &oldProt))
        {
            spdlog::error("GGTB::AutoFishing: VirtualProtect failed @ {:x}", addr);
            return;
        }
        if (!catchGuardCaptured_)
        {
            catchGuardOrig_     = *reinterpret_cast<BYTE *>(addr);
            catchGuardCaptured_ = true;
        }
        *reinterpret_cast<BYTE *>(addr) = on ? static_cast<BYTE>(0xEB) : catchGuardOrig_;
        VirtualProtect(reinterpret_cast<void *>(addr), 1, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(addr), 1);
        catchGuardPatched_ = on;
        spdlog::info("GGTB::AutoFishing: catch-close guard {} @ {:x}",
                     on ? "patched (jnz->jmp)" : "restored", addr);
    }

    // 去掉"抛竿后进入钓鱼姿态"。GC_DispatchActorAction @ 0x94ED10 的 case 3/13
    // (钓鱼动作)在 0x94EFE0 处 `call Actor_SetFishingPose`,把本地玩家 actor+404
    // 摆成钓鱼 motion(27)。把这 5 字节 call(E8 rel32)整段 NOP(90*5)即可让抛竿
    // 后不再赋姿态。见 PatternResolver "FishingPoseBroadcastCall"。幂等:on==当前态
    // 直接返回。副作用:附近真人钓鱼者经此路径的逐动作重摆姿也跳过(仅外观,仅启用期间)。
    void SetPoseGuard(bool on)
    {
        if (on == poseGuardPatched_)
            return;
        uintptr_t addr = PatternResolver::Get("FishingPoseBroadcastCall");
        if (!addr)
        {
            spdlog::error("GGTB::AutoFishing: FishingPoseBroadcastCall 未解析");
            return;
        }
        DWORD oldProt = 0;
        if (!VirtualProtect(reinterpret_cast<void *>(addr), 5, PAGE_EXECUTE_READWRITE, &oldProt))
        {
            spdlog::error("GGTB::AutoFishing: VirtualProtect failed @ {:x}", addr);
            return;
        }
        if (!poseGuardCaptured_)
        {
            std::memcpy(poseGuardOrig_, reinterpret_cast<void *>(addr), 5);
            poseGuardCaptured_ = true;
        }
        if (on)
            std::memset(reinterpret_cast<void *>(addr), 0x90, 5); // 5× NOP
        else
            std::memcpy(reinterpret_cast<void *>(addr), poseGuardOrig_, 5);
        VirtualProtect(reinterpret_cast<void *>(addr), 5, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(addr), 5);
        poseGuardPatched_ = on;
        spdlog::info("GGTB::AutoFishing: fishing-pose guard {} @ {:x}",
                     on ? "patched (call->nop)" : "restored", addr);
    }

    // ---- SEH 包裹的实际发送(场景切换时 client/localUser 可能瞬时撕裂)----
    // 这些函数内不得有带析构的 C++ 对象(C2712)。返回 >0 已发, 0 noop(空指针/未连),
    // -1 异常。Net__SendPacket_Plaintext 自身 gate client+12,未连线时本来就 noop。
    //
    // 缓冲必须放大到 1KB:Net__SendPacket_Plaintext 加密开启时(*(client+13))会就地
    // 调 sub_5CA910 —— memmove(body→body+8) 前插 8 字节时间戳+校验,再 sub_5CB920 就地
    // 变换,把 body 区只增不减地改写回**调用方这同一个 buffer**。24 字节明文实测膨胀到 40
    // (sendlog WIRE_SEND=0x28)。游戏自己用 client 内部 0x400 缓冲,我们若用栈上小数组就
    // 会被写爆 → 栈溢出闪退(这才是"一抛竿就闪退"的真因,与之前那个竞争 Detour 无关)。
    static constexpr int kSendBufBytes = 1024; // == 引擎 dword_D6589C 量级,够膨胀

    static int SendCastSEH(fnSendPacketPT fn, void *client, void *localUser,
                           uint32_t baitSlot, bool altBait)
    {
        if (!fn || !client || !localUser)
            return 0;
        __try
        {
            uint8_t *lu = static_cast<uint8_t *>(localUser);
            uint32_t coordX = *reinterpret_cast<uint32_t *>(lu + 412); // this[103]
            uint32_t coordY = *reinterpret_cast<uint32_t *>(lu + 416); // this[104]
            // 自守:坐标 (0,0) 绝不抛竿(场景重载窗口的踢线真因)。当作没发,Tick 重试。
            if (coordX == 0 && coordY == 0)
                return 0;
            uint8_t  buf[kSendBufBytes];
            memset(buf, 0, sizeof(buf));
            uint32_t *w = reinterpret_cast<uint32_t *>(buf);
            w[0] = 24;
            w[1] = altBait ? 411174u : 411047u;
            w[2] = baitSlot; // 背包槽位 wire 编码(slotIndex+13),非物品模板 id —— 见 ResolveBaitSlot
            w[3] = *reinterpret_cast<uint32_t *>(lu + 408);
            w[4] = coordX;
            w[5] = coordY;
            return fn(client, nullptr, buf, 24);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    static int SendDwordSEH(fnSendPacketPT fn, void *client, uint32_t proto, uint32_t value)
    {
        if (!fn || !client)
            return 0;
        __try
        {
            uint8_t  buf[kSendBufBytes];
            memset(buf, 0, sizeof(buf));
            uint32_t *w = reinterpret_cast<uint32_t *>(buf);
            w[0] = 12;
            w[1] = proto;
            w[2] = value;
            return fn(client, nullptr, buf, 12);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }
};

} // namespace GGTB
