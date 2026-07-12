#include "Setting.h"
#include "modules/ScreenOverlay.h"
#include "util/NetLog.h"
#include "util/UserConfig.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <cfloat>
#include <cstdio>
#include <vector>

namespace GGTB
{

void Setting::RegisterModule(std::shared_ptr<IModule> mod)
{
    modules_.push_back(std::move(mod));
}

void Setting::Init()
{
    ImGuiStyle &style = ImGui::GetStyle();
    auto &colors      = style.Colors;

    style.WindowRounding    = 6.0f;
    style.FrameRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;

    // 整体收紧:默认间距偏松,模块多时一屏放不下。把纵向 padding/spacing 压小,
    // 配合 RenderTab 的多列装箱,争取一般窗口大小下一屏看全。
    style.WindowPadding     = ImVec2(8.0f, 6.0f);
    style.FramePadding      = ImVec2(5.0f, 2.0f);
    style.ItemSpacing       = ImVec2(6.0f, 3.0f);
    style.ItemInnerSpacing  = ImVec2(4.0f, 3.0f);
    style.IndentSpacing     = 14.0f;
    style.ScrollbarSize     = 11.0f;
    style.GrabMinSize       = 9.0f;

    colors[ImGuiCol_WindowBg]      = ImVec4(0.08f, 0.08f, 0.10f, 0.92f);
    colors[ImGuiCol_TitleBg]       = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.14f, 0.20f, 1.00f);

    colors[ImGuiCol_FrameBg]        = ImVec4(0.14f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgActive]  = ImVec4(0.24f, 0.24f, 0.30f, 1.00f);

    colors[ImGuiCol_Button]        = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.26f, 0.34f, 1.00f);
    colors[ImGuiCol_ButtonActive]  = ImVec4(0.30f, 0.30f, 0.40f, 1.00f);

    colors[ImGuiCol_CheckMark]     = ImVec4(0.00f, 0.75f, 0.00f, 1.00f);
    colors[ImGuiCol_Header]        = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.24f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderActive]  = ImVec4(0.28f, 0.28f, 0.38f, 1.00f);

    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;

    char fontPath[MAX_PATH]{};
    if (GetWindowsDirectoryA(fontPath, MAX_PATH))
    {
        strcat_s(fontPath, "\\Fonts\\simhei.ttf");
        if (GetFileAttributesA(fontPath) != INVALID_FILE_ATTRIBUTES)
        {
            io.Fonts->AddFontFromFileTTF(fontPath, 16.0f, nullptr,
                                         io.Fonts->GetGlyphRangesChineseFull());
            spdlog::info("GGTB: loaded CJK font {}", fontPath);
        }
        else
        {
            spdlog::warn("GGTB: simhei.ttf not found, CJK glyphs unavailable");
        }
    }

    for (auto &mod : modules_)
        mod->OnInit();

    spdlog::info("GGTB::Setting::Init done, {} modules", modules_.size());
    initialized_.store(true);
}

nlohmann::json Setting::ComputeSnapshot() const
{
    nlohmann::json snap = nlohmann::json::object();
    for (auto &mod : modules_)
    {
        if (!mod) continue;
        auto k = mod->ConfigKey();
        if (k.empty()) continue;
        nlohmann::json mj;
        mod->SaveState(mj);
        snap[k] = std::move(mj);
    }
    return snap;
}

void Setting::RefreshProfiles()
{
    profiles_           = UserConfig::ListProfiles();
    activeProfile_      = UserConfig::ActiveProfileName();
    selectedProfileIdx_ = -1;
    for (int i = 0; i < static_cast<int>(profiles_.size()); ++i)
        if (profiles_[i] == activeProfile_) { selectedProfileIdx_ = i; break; }
}

void Setting::RenderModule(IModule *mod)
{
    ImGui::PushID(mod);
    // 标题不再单独占一行:模块名已并进各自的"启用"checkbox(见模块 OnRender),
    // 标题=勾选同行。模块之间的分隔线由 RenderTab 在列内绘制。
    // NPG lockout: while the guard is active the module still draws (user needs
    // to see state) but every widget is unclickable, so re-ticking a checkbox
    // can't re-apply the patch and leak the cheat effect to a nearby player.
    const bool guarded = mod->IsPausedByGuard();
    if (guarded)
        ImGui::BeginDisabled();
    mod->OnRender();
    if (guarded)
        ImGui::EndDisabled();
    ImGui::PopID();
}

void Setting::RenderHeader()
{
    for (auto &mod : modules_)
    {
        if (!mod || !mod->RenderInHeader()) continue;
        ImGui::PushID(mod.get());
        mod->OnRender();
        ImGui::PopID();
    }
}

void Setting::RenderTab(Tab tab)
{
    // compact = 纯单 checkbox 模块,横向流式排在顶部;full = 带 slider/表格的重模块,
    // 走下面的多列贪心装箱。
    std::vector<IModule *> compact;
    std::vector<IModule *> mods;
    for (auto &m : modules_)
    {
        if (!m || m->RenderInHeader() || m->GetTab() != tab)
            continue;
        if (m->IsCompact())
            compact.push_back(m.get());
        else
            mods.push_back(m.get());
    }

    if (compact.empty() && mods.empty())
    {
        ImGui::Spacing();
        ImGui::TextDisabled(u8"(暂无功能)");
        return;
    }

    // 顶部 compact 流式行:逐个画 checkbox,按"下一个 checkbox 预估宽度"判断是否
    // 还放得下,放不下就让它自然换行。GetName() == checkbox 可见文字,宽度估得准。
    if (!compact.empty())
    {
        const ImGuiStyle &style = ImGui::GetStyle();
        const float rightX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        for (size_t i = 0; i < compact.size(); ++i)
        {
            RenderModule(compact[i]);
            if (i + 1 >= compact.size())
                break;
            const float nextW = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x +
                                ImGui::CalcTextSize(compact[i + 1]->GetName().c_str()).x;
            const float lastX2 = ImGui::GetItemRectMax().x;
            if (lastX2 + style.ItemSpacing.x + nextW < rightX)
                ImGui::SameLine();
        }
        if (!mods.empty())
            ImGui::Separator();
    }

    if (mods.empty())
        return;

    // 每个 tab 的内容形态不同,列布局也按 tab 微调:
    //   * 常用/传送/PK 里混着带表格的重模块(NPG/AutoPickup/ESP),列太窄表格会挤,
    //     所以最多 2 列、单列要更宽。
    //   * 变态页全是 checkbox+slider 的小模块,可以塞到 3 列更紧凑。
    int   maxCols = 2;
    float minColW = 300.0f;
    if (tab == Tab::OP)
    {
        maxCols = 3;
        minColW = 235.0f;
    }

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float availX  = ImGui::GetContentRegionAvail().x;

    int cols = static_cast<int>((availX + spacing) / (minColW + spacing));
    if (cols < 1)       cols = 1;
    if (cols > maxCols) cols = maxCols;
    if (cols > static_cast<int>(mods.size()))
        cols = static_cast<int>(mods.size());

    const float colW = (availX - spacing * (cols - 1)) / cols;

    // 贪心装箱:按上一帧实测高度,把每个模块放进当前最矮的列。首帧没有历史高度时
    // 用一个保守估计,下一帧就自我校正。保持注册顺序遍历,列内顺序稳定不抖。
    constexpr float kDefaultModuleH = 60.0f;
    std::vector<std::vector<IModule *>> buckets(cols);
    std::vector<float>                  colH(cols, 0.0f);
    for (IModule *m : mods)
    {
        int best = 0;
        for (int c = 1; c < cols; ++c)
            if (colH[c] < colH[best])
                best = c;
        buckets[best].push_back(m);
        auto it = moduleHeights_.find(m);
        colH[best] += (it != moduleHeights_.end() ? it->second : kDefaultModuleH) + spacing;
    }

    for (int c = 0; c < cols; ++c)
    {
        if (c)
            ImGui::SameLine();
        char id[16];
        std::snprintf(id, sizeof(id), "##col%d", c);
        // 最后一列宽度给 0 = 吃掉剩余宽度,避免整数除法留下右边一条缝。
        const float w = (c == cols - 1) ? 0.0f : colW;
        ImGui::BeginChild(id, ImVec2(w, 0.0f), false);
        for (size_t i = 0; i < buckets[c].size(); ++i)
        {
            IModule    *m  = buckets[c][i];
            const float y0 = ImGui::GetCursorPosY();
            if (i)
                ImGui::Separator(); // 列内模块间的细分隔(替代原来占一行的标题分隔)
            RenderModule(m);
            moduleHeights_[m] = ImGui::GetCursorPosY() - y0;
        }
        ImGui::EndChild();
    }
}

void Setting::RenderConfigSidebar()
{
    ImGui::TextUnformatted(u8"配置");
    ImGui::Separator();

    // Debug 模式:控制是否把 send/recv 明文写进 net 日志文件。开关即时生效(全局),
    // 登录后随 config.json 持久化(per-character)。放在最上面,登录前也能临时切。
    bool debugLog = NetLog::IsLogEnabled();
    if (ImGui::Checkbox(u8"Debug 模式(记录网络日志)", &debugLog))
    {
        NetLog::SetLogEnabled(debugLog);
        UserConfig::MarkDirty(); // 登录后约 1s debounce 写回 config.json
    }
    ImGui::Separator();

    if (!UserConfig::IsReady())
    {
        ImGui::TextDisabled(u8"角色未登录");
        ImGui::TextDisabled(u8"登录后可管理配置");
        return;
    }

    if (dirty_)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                           u8"当前: %s *", activeProfile_.c_str());
    else
        ImGui::Text(u8"当前: %s", activeProfile_.c_str());

    if (ImGui::Button(u8"保存当前配置", ImVec2(-FLT_MIN, 0.0f)))
        UserConfig::SaveProfile(this, activeProfile_);
    if (dirty_)
        ImGui::TextDisabled(u8"有未保存改动");

    ImGui::Separator();
    ImGui::TextDisabled(u8"配置列表(双击加载)");

    ImGui::BeginChild("##plist", ImVec2(0.0f, 130.0f), true);
    for (int i = 0; i < static_cast<int>(profiles_.size()); ++i)
    {
        ImGui::PushID(i);
        const bool sel = (i == selectedProfileIdx_);
        if (ImGui::Selectable(profiles_[i].c_str(), sel))
            selectedProfileIdx_ = i;
        if (ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            UserConfig::LoadProfile(this, profiles_[i]);
        ImGui::PopID();
    }
    ImGui::EndChild();

    const bool hasSel = (selectedProfileIdx_ >= 0 &&
                         selectedProfileIdx_ < static_cast<int>(profiles_.size()));

    ImGui::BeginDisabled(!hasSel);
    if (ImGui::Button(u8"加载"))
        UserConfig::LoadProfile(this, profiles_[selectedProfileIdx_]);
    ImGui::SameLine();
    if (ImGui::Button(u8"删除"))
        UserConfig::DeleteProfile(this, profiles_[selectedProfileIdx_]);
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextDisabled(u8"新建 / 重命名");
    ImGui::PushItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##newname", u8"新配置", newNameBuf_, sizeof(newNameBuf_));
    ImGui::PopItemWidth();

    const bool hasNew = (newNameBuf_[0] != '\0');

    // 新建 = 把当前模块状态另存为新名字并设为活动(= 拷贝当前配置过去)。
    ImGui::BeginDisabled(!hasNew);
    if (ImGui::Button(u8"新建(复制当前)"))
    {
        if (UserConfig::SaveProfile(this, newNameBuf_))
            newNameBuf_[0] = '\0';
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasNew || !hasSel);
    if (ImGui::Button(u8"重命名选中"))
    {
        if (UserConfig::RenameProfile(this, profiles_[selectedProfileIdx_], newNameBuf_))
            newNameBuf_[0] = '\0';
    }
    ImGui::EndDisabled();
}

void Setting::Render(bool &open)
{
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::Begin("GGThreadBlock", &open,
                 ImGuiWindowFlags_NoDecoration |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoSavedSettings);

    const bool ready = UserConfig::IsReady();

    // 配置代际同步:登录加载 / 保存 / 加载 / 改名 / 删除 都会 bump generation。
    // 一旦变化,刷新 profile 列表并把"已保存基线"重置为当前状态(脏标记随之清零)。
    // 这是唯一能感知 HackThread 端登录加载的途径(那次加载不经过 Setting)。
    uint64_t gen = UserConfig::ConfigGeneration();
    if (gen != lastGen_)
    {
        lastGen_       = gen;
        RefreshProfiles();
        savedSnapshot_ = ComputeSnapshot();
    }
    dirty_ = ready && (ComputeSnapshot() != savedSnapshot_);

    RenderHeader();
    ImGui::Separator();

    ImGui::BeginChild("##cfgbar", ImVec2(210.0f, 0.0f), true);
    RenderConfigSidebar();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), false);
    if (ImGui::BeginTabBar("##tabs"))
    {
        if (ImGui::BeginTabItem(u8"常用")) { RenderTab(Tab::Common);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(u8"传送")) { RenderTab(Tab::Teleport); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(u8"变态")) { RenderTab(Tab::OP);       ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(u8"PK"))   { RenderTab(Tab::PK);       ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(u8"怪物")) { RenderTab(Tab::Monster);  ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::End();

    // 常驻 overlay:右上角复活按钮 + 中部提示文字。画在主窗口之外,不跟随内容滚动。
    ScreenOverlay::Render();
}

void Setting::End()
{
    for (auto &mod : modules_)
        mod->OnShutdown();
    spdlog::info("GGTB::Setting::End, all modules shutdown");
}

} // namespace GGTB
