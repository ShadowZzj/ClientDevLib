#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <cstdint>

namespace GGTB
{

// 自动寻路 —— 输入世界坐标 (X,Y),点「走过去」让角色分段走到目标。
//
// 引擎原生点地移动(SetAfterAction -> TraceMove)是逐帧直线插值,撞到第一格障碍就停,
// 没有任何避障/寻路。所以远距离单次 MoveTo 会半路卡死。本模块走 StartPathTo:DLL 内
// 一个 worker 线程在碰撞表上跑网格 A* 算出绕障路径,string-pull 平滑后逐拐点 MoveTo,
// 卡住就 replan。凸障碍、U 型凹墙都能绕,只有真不可达/超出搜索框才放弃。详见
// CLocalPlayer.h StartPathTo 注释。
//
// 纯按钮动作,无"启用"勾选、无持久化。CanAutoPause=false:自动走路本身就是正常玩家
// 行为,不是会泄露给附近真人的作弊补丁,不该被 NPG 拍停(而且走一半被拍停反而更怪)。
class AutoPathModule : public IModule
{
  public:
    AutoPathModule() : IModule(u8"自动寻路") {}

    ~AutoPathModule() override { StopPath(); }

    bool CanAutoPause() const override { return false; }
    Tab  GetTab() const override { return Tab::Teleport; }

    void OnRender() override
    {
        ImGui::TextUnformatted(u8"自动寻路");

        ImGui::PushItemWidth(90.0f);
        ImGui::InputFloat("X##autopath", &inputX_, 0.0f, 0.0f, "%.1f");
        ImGui::SameLine();
        ImGui::InputFloat("Y##autopath", &inputY_, 0.0f, 0.0f, "%.1f");
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button(u8"填当前坐标##autopath"))
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (GetLocalPosition(x, y, z))
            {
                inputX_ = x;
                inputY_ = y;
            }
        }

        if (ImGui::Button(u8"走过去##autopath", ImVec2(120.0f, 0.0f)))
        {
            bool ok = StartPathTo(inputX_, inputY_, /*action=*/1, /*targetId=*/0);
            spdlog::info("GGTB::AutoPath: StartPathTo({:.1f},{:.1f}) ok={}", inputX_, inputY_, ok);
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"停止##autopath", ImVec2(120.0f, 0.0f)))
            StopPath();

        // 进度快照:状态 + 已发段数 + 当前坐标 -> 目标坐标。每帧读一次(mutex 保护)。
        PathProgress pg = GetPathProgress();
        switch (pg.state)
        {
        case PathState::Walking:
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               u8"寻路中 段%d  (%.1f,%.1f) -> (%.1f,%.1f)",
                               pg.segments, pg.curX, pg.curY, pg.targetX, pg.targetY);
            break;
        case PathState::Arrived:
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               u8"已到达 (%.1f,%.1f)  共%d段", pg.curX, pg.curY, pg.segments);
            break;
        case PathState::Failed:
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               u8"失败: %s", pg.error.empty() ? u8"(未知)" : pg.error.c_str());
            break;
        default:
            ImGui::TextDisabled(u8"空闲 —— 输入坐标后点「走过去」");
            break;
        }
        ImGui::TextDisabled(u8"网格 A* 自动绕障,只有真不可达才放弃");
    }

  private:
    float inputX_ = 0.0f;
    float inputY_ = 0.0f;
};

} // namespace GGTB
