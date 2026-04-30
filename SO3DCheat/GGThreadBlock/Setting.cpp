#include "Setting.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>

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
}

void Setting::Render(bool &open)
{
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_FirstUseEver);
    ImGui::Begin("GGThreadBlock", &open, ImGuiWindowFlags_NoCollapse);

    for (auto &mod : modules_)
    {
        if (ImGui::CollapsingHeader(mod->GetName().c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushID(mod->GetName().c_str());
            mod->OnRender();
            ImGui::PopID();
        }
    }

    ImGui::End();
}

void Setting::End()
{
    for (auto &mod : modules_)
        mod->OnShutdown();
    spdlog::info("GGTB::Setting::End, all modules shutdown");
}

} // namespace GGTB
