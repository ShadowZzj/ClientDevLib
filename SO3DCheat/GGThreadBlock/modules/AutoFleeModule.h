#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "../util/UserConfig.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <thread>

namespace GGTB
{

// 自动退避: 实时监控当前选中怪物 (g_TargetCreatureId) 与本地玩家的水平距离,
// 一旦小于阈值就沿"远离怪物"方向选一个最远可走 tile 走过去。
//
// 可达性判定走引擎自己的碰撞表 (Map__IsBlocked @ 0xA97080),通过 IsTileWalkable
// 和 RaycastFurthestWalkable 间接调用。RaycastFurthestWalkable 沿射线步进取最远
// 仍可走的 tile,所以选出的目标点天然不会落在高山/水域/障碍格里。
//
// 走包路径复用 GGTB::MoveTo,即引擎的 SetAfterAction;一切 A* / 高度 / 阻挡都由
// 引擎自己处理,这层不掺和。
//
// 线程模型: 独立 worker,不依赖 D3D9Hook EndScene 是否在 render。UI 关闭后仍然
// 工作。NPG 可以暂停我们(拷贝 AutoPickup 的 pausedByGuard_ 套路),原因:附近有
// 真人时频繁让角色"丢包跳走"反而最像挂。
class AutoFleeModule : public IModule
{
  public:
    AutoFleeModule() : IModule(u8"自动退避打怪")
    {
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~AutoFleeModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    bool CanAutoPause() const override { return true; }

    void OnRender() override
    {
        ImGui::Checkbox(u8"启用##AutoFlee", &enabled_);

        ImGui::SliderFloat(u8"触发距离(格)##AutoFlee", &triggerDistance_, 1.0f, 30.0f, "%.1f");
        ImGui::SliderFloat(u8"退避目标距离(格)##AutoFlee", &fleeDistance_,    3.0f, 50.0f, "%.1f");
        ImGui::SliderInt  (u8"轮询间隔(ms)##AutoFlee",   &pollIntervalMs_,    50,  1000);
        ImGui::SliderInt  (u8"重发节流(ms)##AutoFlee",   &resendCooldownMs_, 200, 5000);

        // Snapshot for UI under lock.
        uint32_t targetId      = 0;
        float    targetDist    = 0.0f;
        float    lastFleeX     = 0.0f, lastFleeY = 0.0f;
        DWORD    lastFleeMs    = 0;
        DWORD    lastTickMs    = 0;
        int      lastReason    = 0;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            targetId    = lastTargetId_;
            targetDist  = lastTargetDist_;
            lastFleeX   = lastFleeX_;
            lastFleeY   = lastFleeY_;
            lastFleeMs  = lastFleeMs_;
            lastTickMs  = lastTickMs_;
            lastReason  = lastSkipReason_;
        }

        ImGui::Separator();
        if (!enabled_)
        {
            ImGui::TextDisabled(u8"未启用");
            return;
        }
        if (IsPausedByGuard())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               u8"已被 NPG 暂停 (附近有玩家)");
            return;
        }

        if (targetId == 0)
        {
            ImGui::TextDisabled(u8"未选中怪物 (g_TargetCreatureId == 0)");
        }
        else
        {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               u8"目标 ID=%u  距离=%.1f", targetId, targetDist);
        }

        if (lastFleeMs)
        {
            DWORD age = GetTickCount() - lastFleeMs;
            ImGui::Text(u8"上次退避到 (%.0f, %.0f), %lums 前", lastFleeX, lastFleeY,
                        static_cast<unsigned long>(age));
        }

        if (lastReason)
        {
            const char *rs = ReasonText(lastReason);
            ImGui::TextDisabled(u8"上轮跳过: %s", rs);
        }

        DWORD tickAge = lastTickMs ? (GetTickCount() - lastTickMs) : 0;
        ImGui::TextDisabled(u8"距上轮 %lums", static_cast<unsigned long>(tickAge));
    }

    std::string ConfigKey() const override { return "AutoFlee"; }

    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["triggerDistance"]  = triggerDistance_;
        j["fleeDistance"]     = fleeDistance_;
        j["pollIntervalMs"]   = pollIntervalMs_;
        j["resendCooldownMs"] = resendCooldownMs_;
    }

    void LoadState(const nlohmann::json &j) override
    {
        triggerDistance_  = j.value("triggerDistance",  triggerDistance_);
        fleeDistance_     = j.value("fleeDistance",     fleeDistance_);
        pollIntervalMs_   = j.value("pollIntervalMs",   pollIntervalMs_);
        resendCooldownMs_ = j.value("resendCooldownMs", resendCooldownMs_);
        IModule::LoadState(j);
    }

  private:
    enum SkipReason
    {
        kSkipNone           = 0,
        kSkipNoTarget       = 1,
        kSkipTargetNotFound = 2,
        kSkipNoLocalPos     = 3,
        kSkipOutOfRange     = 4, // 怪在阈值外,正常情况
        kSkipMapNotReady    = 5,
        kSkipBlockedAtFoot  = 6, // 起点 tile 都不可走 (理论不会发生)
        kSkipNoFleeSpot     = 7, // 找不到合适退避点
        kSkipResendThrottle = 8, // 节流中
        kSkipMoveFailed     = 9,
    };

    static const char *ReasonText(int r)
    {
        switch (r)
        {
        case kSkipNoTarget:       return u8"无目标";
        case kSkipTargetNotFound: return u8"目标 ID 在 mgr 里查不到";
        case kSkipNoLocalPos:     return u8"读不到本地坐标";
        case kSkipOutOfRange:     return u8"目标在安全距离外";
        case kSkipMapNotReady:    return u8"地图未就绪";
        case kSkipBlockedAtFoot:  return u8"脚下 tile 不可走 (engine race)";
        case kSkipNoFleeSpot:     return u8"找不到可走退避点";
        case kSkipResendThrottle: return u8"重发节流中";
        case kSkipMoveFailed:     return u8"MoveTo 失败 (引擎拒绝)";
        default:                  return u8"";
        }
    }

    void WorkerLoop()
    {
        spdlog::info("GGTB::AutoFlee: worker thread started");
        while (!stop_.load())
        {
            if (enabled_ && !IsPausedByGuard() && UserConfig::IsReady())
                Tick();

            for (int i = 0; i < 10 && !stop_.load(); ++i)
                Sleep(pollIntervalMs_ / 10 + 1);
        }
        spdlog::info("GGTB::AutoFlee: worker thread exited");
    }

    int CountOpenNeighbors(int tx, int ty)
    {
        int open = 0;
        for (int oy = -1; oy <= 1; ++oy)
        {
            for (int ox = -1; ox <= 1; ++ox)
            {
                if (ox == 0 && oy == 0)
                    continue;
                if (IsTileWalkable(tx + ox, ty + oy))
                    ++open;
            }
        }
        return open;
    }

    bool PickBestFleeTile(float lx, float ly, float mx, float my,
                          float currentDist, int &outX, int &outY)
    {
        int sx = static_cast<int>(std::floor(lx));
        int sy = static_cast<int>(std::floor(ly));

        float awayX = lx - mx;
        float awayY = ly - my;
        float awayLen = std::sqrt(awayX * awayX + awayY * awayY);
        if (awayLen > 0.0001f)
        {
            awayX /= awayLen;
            awayY /= awayLen;
        }
        else
        {
            awayX = 1.0f;
            awayY = 0.0f;
        }

        const float pi = 3.14159265358979323846f;
        const float safeRadius =
            fleeDistance_ > triggerDistance_ + 1.0f ? fleeDistance_ : triggerDistance_ + 1.0f;
        const float radii[] = {
            safeRadius,
            safeRadius + 2.0f,
            safeRadius + 4.0f,
            safeRadius + 7.0f,
        };
        const int angleSamples = 32;
        const float baseAngle = std::atan2(awayY, awayX);

        float bestScore = -std::numeric_limits<float>::infinity();
        bool found = false;
        int bestX = sx;
        int bestY = sy;

        for (float radius : radii)
        {
            for (int ai = 0; ai < angleSamples; ++ai)
            {
                float angle = baseAngle + (2.0f * pi * static_cast<float>(ai)) /
                                             static_cast<float>(angleSamples);
                float dirX = std::cos(angle);
                float dirY = std::sin(angle);
                int tx = static_cast<int>(std::floor(mx + dirX * radius));
                int ty = static_cast<int>(std::floor(my + dirY * radius));

                if (!IsTileWalkable(tx, ty))
                    continue;

                float cx = static_cast<float>(tx) + 0.5f;
                float cy = static_cast<float>(ty) + 0.5f;
                float mdx = cx - mx;
                float mdy = cy - my;
                float monsterDistSq = mdx * mdx + mdy * mdy;
                if (monsterDistSq < safeRadius * safeRadius)
                    continue;

                float sdx = cx - lx;
                float sdy = cy - ly;
                float selfDistSq = sdx * sdx + sdy * sdy;
                if (selfDistSq < 4.0f)
                    continue;

                float monsterDist = std::sqrt(monsterDistSq);
                float selfDist = std::sqrt(selfDistSq);
                float moveDirX = sdx / selfDist;
                float moveDirY = sdy / selfDist;
                float awayDot = moveDirX * awayX + moveDirY * awayY;
                int openness = CountOpenNeighbors(tx, ty);
                if (openness == 0)
                    continue;

                int rayX = sx;
                int rayY = sy;
                bool rayOk = RaycastFurthestWalkable(sx, sy, tx, ty, rayX, rayY);
                bool lineClear = rayOk && rayX == tx && rayY == ty;

                float safety = monsterDist - safeRadius;
                float progress = monsterDist - currentDist;
                float score = 0.0f;
                score += safety * 4.0f;
                score += progress * 3.0f;
                score += awayDot * 10.0f;
                score += static_cast<float>(openness) * 1.5f;
                score -= selfDist * 1.2f;
                score += lineClear ? 8.0f : -6.0f;
                if (awayDot < 0.0f)
                    score += awayDot * 20.0f;

                if (score > bestScore)
                {
                    bestScore = score;
                    bestX = tx;
                    bestY = ty;
                    found = true;
                }
            }
        }

        outX = bestX;
        outY = bestY;
        return found;
    }

    void Tick()
    {
        DWORD now = GetTickCount();

        uint32_t targetId = GetTargetCreatureId();
        if (targetId == 0)
        {
            UpdateState(now, 0, 0.0f, kSkipNoTarget);
            return;
        }

        CreatureSnapshot mob{};
        if (!LookupCreatureById(targetId, mob))
        {
            UpdateState(now, targetId, 0.0f, kSkipTargetNotFound);
            return;
        }

        float lx = 0, ly = 0, lz = 0;
        if (!GetLocalPosition(lx, ly, lz))
        {
            UpdateState(now, targetId, 0.0f, kSkipNoLocalPos);
            return;
        }

        // 用水平面 (X, Y) 测距 — Z 是高度,跨高度差的距离不应该让我们退避。
        float dx = mob.x - lx;
        float dy = mob.y - ly;
        float d  = std::sqrt(dx * dx + dy * dy);

        // 总是更新当前距离以便 UI 显示。
        if (d >= triggerDistance_)
        {
            UpdateState(now, targetId, d, kSkipOutOfRange);
            return;
        }

        // 节流: 距上次成功发包不到 cooldown,跳过本轮。
        if (lastFleeMs_ != 0 && now - lastFleeMs_ < static_cast<DWORD>(resendCooldownMs_))
        {
            UpdateState(now, targetId, d, kSkipResendThrottle);
            return;
        }

        // 选择退避方向: 怪 -> 我的单位向量 (远离怪)。
        float ux, uy;
            // 重叠极端情况: 任意挑一个方向,避免除零。
            ux = 1.0f;
        // 转 tile 坐标 (引擎用 floor 取整)。
        int sx = static_cast<int>(std::floor(lx));
        int sy = static_cast<int>(std::floor(ly));
        if (!IsTileWalkable(sx, sy))
        {
            UpdateState(now, targetId, d, kSkipBlockedAtFoot);
            return;
        }

        int outX = sx, outY = sy;
        if (!PickBestFleeTile(lx, ly, mob.x, mob.y, d, outX, outY))
        {
            UpdateState(now, targetId, d, kSkipNoFleeSpot);
            return;
        }

        // 走出去太近 — 比如周围全是阻挡,只走得了 0~1 格,不值得发包。
        int stepX = outX - sx;
        int stepY = outY - sy;
        int stepDistSq = stepX * stepX + stepY * stepY;
        if (stepDistSq < 4) // <2 tile,放弃这一轮
        {
            UpdateState(now, targetId, d, kSkipNoFleeSpot);
            return;
        }

        // MoveTo 用 float 世界坐标; 选 tile 中心 (+0.5,+0.5) 防止落到边界 tile。
        float fx = static_cast<float>(outX) + 0.5f;
        float fy = static_cast<float>(outY) + 0.5f;

        bool moved = MoveTo(fx, fy, /*action=*/1, /*targetId=*/0);
        if (!moved)
        {
            UpdateState(now, targetId, d, kSkipMoveFailed);
            return;
        }

        spdlog::info("GGTB::AutoFlee: target={} dist={:.1f} flee=({:.1f},{:.1f})",
                     targetId, d, fx, fy);

        std::lock_guard<std::mutex> lk(mutex_);
        lastTickMs_      = now;
        lastFleeMs_      = now;
        lastTargetId_    = targetId;
        lastTargetDist_  = d;
        lastFleeX_       = fx;
        lastFleeY_       = fy;
        lastSkipReason_  = kSkipNone;
    }

    void UpdateState(DWORD now, uint32_t targetId, float dist, int reason)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        lastTickMs_     = now;
        lastTargetId_   = targetId;
        lastTargetDist_ = dist;
        lastSkipReason_ = reason;
    }

    // ---- Tunables ----
    float triggerDistance_  = 5.0f;   // 怪靠近到 N 格内就触发
    float fleeDistance_     = 12.0f;  // 退避目标点到自己的理想距离
    int   pollIntervalMs_   = 200;    // 轮询周期
    int   resendCooldownMs_ = 800;    // 重发节流; 不要每轮都发新 MoveTo

    // ---- State (lock for cross-thread reads) ----
    std::mutex mutex_;
    DWORD      lastTickMs_      = 0;
    DWORD      lastFleeMs_      = 0;
    uint32_t   lastTargetId_    = 0;
    float      lastTargetDist_  = 0.0f;
    float      lastFleeX_       = 0.0f;
    float      lastFleeY_       = 0.0f;
    int        lastSkipReason_  = 0;

    std::atomic<bool> stop_{false};
    std::thread       worker_;
};

} // namespace GGTB
