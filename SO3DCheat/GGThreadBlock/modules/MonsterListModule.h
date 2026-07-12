#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <Windows.h>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace GGTB
{

// 周围怪物/NPC 列表(只读内存)。走 GetNearbyNpcs —— 它遍历引擎 CreatureMgr 的
// around-player 链表,同时收怪物和 NPC(丢宠物/死亡),按引擎的 kind 字段
// (statTable+0xDC:7=NPC、8=宠物、10=树、其余=怪物)得出 isNpc。UI 据此打
// 「类型」标签,配「显示怪物 / 显示NPC」两个开关做筛选。纯查看:不发包、不打
// 补丁,故 CanAutoPause=false。
//
// 只在菜单打开(EndScene 渲染)时才需要刷新,所以不起独立 worker —— OnRender 里按
// kPollIntervalMs 节流走一次 GetNearbyNpcs,缓存到 snapshot_ 供当帧渲染,单线程无锁。
class MonsterListModule : public IModule
{
  public:
    MonsterListModule() : IModule(u8"怪物列表") {}

    bool CanAutoPause() const override { return false; }
    Tab  GetTab() const override { return Tab::Monster; }

    void OnRender() override
    {
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderFloat(u8"扫描距离(格,0=全部)##MonList", &maxDistance_,
                           0.0f, 150.0f, "%.0f");
        ImGui::SameLine();
        ImGui::TextDisabled(u8"(只读内存,不发包)");

        ImGui::Checkbox(u8"显示怪物", &showMonsters_);
        ImGui::SameLine();
        ImGui::Checkbox(u8"显示NPC", &showNpcs_);

        const DWORD now = GetTickCount();
        if (now - lastPollMs_ >= kPollIntervalMs)
        {
            snapshot_   = GetNearbyNpcs(maxDistance_);
            lastPollMs_ = now;
        }

        int npc = 0, mob = 0, other = 0;
        for (const auto &m : snapshot_)
            (m.attackable ? mob : (m.isNpc ? npc : other))++;
        ImGui::Text(u8"周围: %d (怪物 %d / NPC %d / 其他 %d)",
                    static_cast<int>(snapshot_.size()), mob, npc, other);

        constexpr ImGuiTableFlags kFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

        // 填满标签页剩余高度:本模块是「怪物」页里唯一的模块,独占整列。
        const float h = ImGui::GetContentRegionAvail().y;
        if (ImGui::BeginTable("##MonsterList", 7, kFlags, ImVec2(0.0f, h)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(u8"类型", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn(u8"名称", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(u8"等级", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn(u8"血量", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn(u8"距离", ImGuiTableColumnFlags_WidthFixed, 52.0f);
            ImGui::TableSetupColumn(u8"坐标", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableSetupColumn("ID",     ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            char buf[64];
            for (const auto &m : snapshot_)
            {
                // 三类:可攻击的怪物、可对话的 NPC(含传送点)、其余(树/未知)。
                // 「显示NPC」管 NPC,「显示怪物」管怪物 + 其余非 NPC。
                const bool isNpc = m.isNpc && !m.attackable;
                if (isNpc ? !showNpcs_ : !showMonsters_)
                    continue;

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                if (m.attackable)
                    ImGui::TextColored(ImVec4(1.00f, 0.65f, 0.40f, 1.00f), u8"怪物");
                else if (m.isNpc)
                    ImGui::TextColored(ImVec4(0.45f, 0.80f, 1.00f, 1.00f), "NPC");
                else
                    ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.00f), u8"其他");

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(m.name.empty() ? u8"(未知)" : m.name.c_str());

                ImGui::TableNextColumn();
                if (m.level) ImGui::Text("%u", m.level);
                else         ImGui::TextDisabled("-");

                ImGui::TableNextColumn();
                RenderHpCell(m.hp, m.maxHp, buf, sizeof(buf));

                ImGui::TableNextColumn();
                ImGui::Text("%.1f", m.distance);

                ImGui::TableNextColumn();
                ImGui::Text("%.0f, %.0f", m.x, m.y);

                ImGui::TableNextColumn();
                ImGui::Text("%u", m.creatureId);
            }
            ImGui::EndTable();
        }
    }

  private:
    static constexpr DWORD kPollIntervalMs = 150;

    // 大数血量缩写:>=1亿 用「亿」,>=1万 用「万」,否则原值。稻草人等活动怪 HP 可到
    // 2 亿,直接铺数字会撑爆列宽。
    static void FormatHp(int64_t v, char *buf, size_t cap)
    {
        if (v >= 100000000)  std::snprintf(buf, cap, u8"%.2f亿", v / 100000000.0);
        else if (v >= 10000) std::snprintf(buf, cap, u8"%.1f万", v / 10000.0);
        else                 std::snprintf(buf, cap, "%lld", static_cast<long long>(v));
    }

    static void RenderHpCell(int64_t hp, int64_t maxHp, char *buf, size_t cap)
    {
        if (hp < 0) hp = 0;
        if (maxHp > 0 && maxHp >= hp)
        {
            char cur[32], mx[32];
            FormatHp(hp, cur, sizeof(cur));
            FormatHp(maxHp, mx, sizeof(mx));
            std::snprintf(buf, cap, "%s / %s", cur, mx);
            const float frac =
                static_cast<float>(static_cast<double>(hp) / static_cast<double>(maxHp));
            ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0.0f), buf);
        }
        else if (hp > 0)
        {
            FormatHp(hp, buf, cap);
            ImGui::TextUnformatted(buf);
        }
        else
        {
            // 多数 NPC 没有战斗模板,hp/maxHp 皆 0 —— 别画一条空血条。
            ImGui::TextDisabled("-");
        }
    }

    float                  maxDistance_ = 0.0f; // 0 = 不按距离过滤(AOI 内全部)
    bool                   showMonsters_ = true;
    bool                   showNpcs_     = true;
    DWORD                  lastPollMs_  = 0;
    std::vector<NearbyNpc> snapshot_;
};

} // namespace GGTB
