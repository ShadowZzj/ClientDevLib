#include "GameSetting.h"

void GameSetting::Render(bool &open)
{
    ImGui::Begin("SealCheat", &open);
    static bool b1 = false;
    ImGui::Checkbox("b1", &b1);

    ImGui::End();
}