#include "ImguiManager.h"
#include <imgui/imgui.h>

using namespace zzj;
ImguiManager::ImguiManager()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
}

ImguiManager::~ImguiManager()
{
    ImGui::DestroyContext();
}
