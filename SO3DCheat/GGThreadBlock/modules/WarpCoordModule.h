#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "ScreenOverlay.h"
#include <cmath>
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

namespace GGTB
{

// 坐标传送 —— 输入地图格坐标 (x, y),点「传送」直接发 CG 411597 把角色传过去。
// 复刻街摊「搜索 -> 传送到摊主」里那条 warp 包(WarpToCoordinate,见 CLocalPlayer.cpp),
// 走引擎最底层明文发送,不裸拼协议外的东西。x/y 是整数格坐标(地图小格),不是世界
// 浮点坐标。纯一次性按钮动作,无持久化、无「启用」勾选,标题自绘(同城镇传送)。
// CanAutoPause=false:手动传送不是会泄露给附近玩家的作弊效果,不该被 NPG 禁掉。
class WarpCoordModule : public IModule
{
  public:
    WarpCoordModule() : IModule(u8"坐标传送") {}

    bool CanAutoPause() const override { return false; }
    Tab  GetTab() const override { return Tab::Teleport; }

    void OnRender() override
    {
        ImGui::TextUnformatted(u8"坐标传送");

        // 实时当前坐标。CG 411597 的目标 x/y 与玩家位置同处「地图格」整数空间:引擎
        // 街摊「传送到摊主」把摊主的世界浮点坐标 round 成整数发出,而世界浮点
        // (CUser+0x3C/+0x44) 与整数格坐标本就同一数值刻度(见 CLocalPlayer.cpp 技能
        // 射程注释),所以这里读 GetLocalPosition 的 X/Y 四舍五入即得可直接回填的传送格。
        int  curX = 0, curY = 0;
        bool havePos = ReadCurrentTile(curX, curY);
        if (havePos)
            ImGui::Text(u8"当前坐标: (%d, %d)", curX, curY);
        else
            ImGui::TextDisabled(u8"当前坐标: (读取失败)");
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"填当前##WarpCoord") && havePos)
        {
            x_ = curX;
            y_ = curY;
        }

        ImGui::SetNextItemWidth(90);
        ImGui::InputInt(u8"X##WarpCoord", &x_, 0, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::InputInt(u8"Y##WarpCoord", &y_, 0, 0);
        ImGui::SameLine();
        if (ImGui::Button(u8"传送##WarpCoord"))
        {
            bool ok = WarpToCoordinate(x_, y_);
            std::string toast =
                (ok ? u8"已传送到 (" : u8"传送失败 (") + std::to_string(x_) + ", " +
                std::to_string(y_) + ")";
            ScreenOverlay::ShowToast(toast.c_str(), 3000);
            spdlog::info("GGTB::WarpCoord: warp ({},{}) -> {}", x_, y_, ok ? "ok" : "fail");
        }

        ImGui::TextDisabled(u8"输入地图格坐标后点传送(整数,非世界浮点坐标)");
    }

  private:
    // 把本地玩家世界浮点坐标 round 成「地图格」整数 —— 与 CG 411597 目标坐标同空间。
    static bool ReadCurrentTile(int &tx, int &ty)
    {
        float fx = 0.0f, fy = 0.0f, fz = 0.0f;
        if (!GetLocalPosition(fx, fy, fz))
            return false;
        tx = static_cast<int>(std::lround(fx));
        ty = static_cast<int>(std::lround(fy));
        return true;
    }

    int x_ = 0;
    int y_ = 0;
};

} // namespace GGTB
