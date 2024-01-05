#include "GameSetting.h"
#include "GameManager.h"
#include <General/util/File/File.h>
#include <Windows/util/Process/ProcessHelper.h>
#include <boost/filesystem.hpp>

static void PlaceHolder()
{
}

void GameSetting::InitImguiConfig()
{
    ImGuiStyle &style = ImGui::GetStyle();
    auto &colors      = style.Colors;

    style.ScrollbarRounding = 0.0f;
    style.WindowRounding    = 4.0f;

    colors[ImGuiCol_ResizeGrip]        = ImColor(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered] = ImColor(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripActive]  = ImColor(0, 0, 0, 0);

    colors[ImGuiCol_Button]        = ImColor(18, 18, 18, 100);
    colors[ImGuiCol_ButtonHovered] = ImColor(21, 21, 21, 100);
    colors[ImGuiCol_ButtonActive]  = ImColor(21, 21, 21, 100);

    colors[ImGuiCol_CheckMark] = ImColor(0, 189, 0, 255);

    colors[ImGuiCol_FrameBg]        = ImColor(24, 24, 24);
    colors[ImGuiCol_FrameBgHovered] = ImColor(26, 26, 26);
    colors[ImGuiCol_FrameBgActive]  = ImColor(26, 26, 26);

    // load chinese font and support 繁体中文
    ImGuiIO &io                            = ImGui::GetIO();
    boost::filesystem::path executablePath = zzj::GetDynamicLibPath(&PlaceHolder);
    boost::filesystem::path fontPath       = executablePath / "MINGLIU.TTF";
    spdlog::info("Font path: {}", fontPath.string());
}
void GameSetting::UninitImguiConfig()
{
}
void GameSetting::Render(bool &open)
{
    static bool isTempPause = true;
    static GameManager gameManager;
    ImGui::SetNextWindowBgAlpha(0.2f);
    ImGui::Begin("SealCheat", &open);
    ImVec2 size       = ImGui::GetWindowSize();
    ImVec2 pos        = ImGui::GetWindowPos();
    ImVec2 cursor_pos = ImGui::GetCursorPos();

    if (ImGui::Checkbox("AttackRangeEnable", &GameManager::attackRangeEnable))
    {
        spdlog::info("AttackRangeEnable changed to {}", GameManager::attackRangeEnable);
        if (GameManager::attackRangeEnable)
        {
            gameManager.EnableAttackRange();
        }
        else
        {
            gameManager.DisableAttackRange();
        }
    }

    if (GameManager::attackRangeEnable)
    {
        // slider from 1 to 10
        ImGui::SliderInt("AttackRange", &GameManager::attackRange, 1, 10);
    }

    if (ImGui::Checkbox("AttackSpeedEnable", &GameManager::attackSpeedEnable))
    {
        spdlog::info("AttackSpeedEnable changed to {}", GameManager::attackSpeedEnable);
        if (GameManager::attackSpeedEnable)
        {
            gameManager.EnableAttackSpeed();
        }
        else
        {
            gameManager.DisableAttackSpeed();
        }
    }

    if (GameManager::attackSpeedEnable)
    {
        // slider from 0.0f to 1.0f
        ImGui::SliderFloat("AttackSpeed", &GameManager::attackSpeed, 0.0f, 1.0f);
    }

    if (ImGui::Checkbox("SkillRangeEnable", &GameManager::skillRangeEnable))
    {
        spdlog::info("SkillRangeEnable changed to {}", GameManager::skillRangeEnable);
        if (GameManager::skillRangeEnable)
        {
            gameManager.EnableSkillRange();
        }
        else
        {
            gameManager.DisableSkillRange();
        }
    }

    if (ImGui::Checkbox("ItemNoCoolDownEnable", &GameManager::itemNoCoolDownEnable))
    {
        spdlog::info("ItemNoCoolDownEnable changed to {}", GameManager::itemNoCoolDownEnable);
        if (GameManager::itemNoCoolDownEnable)
        {
            gameManager.EnableItemNoCoolDown();
        }
        else
        {
            gameManager.DisableItemNoCoolDown();
        }
    }

    if (GameManager::itemNoCoolDownEnable)
    {
        // slider from 1 to 10
        ImGui::SliderInt("ItemCoolDown", &GameManager::itemCoolDown, 1, 100);
    }

    if (ImGui::Checkbox("SkillSpeedEnable", &GameManager::skillSpeedEnable))
    {
        spdlog::info("SkillSpeedEnable changed to {}", GameManager::skillSpeedEnable);
        if (GameManager::skillSpeedEnable)
        {
            gameManager.EnableSkillSpeed();
        }
        else
        {
            gameManager.DisableSkillSpeed();
        }
    }

    if (ImGui::Checkbox("MoveSpeedEnable", &GameManager::moveSpeedEnable))
    {
        spdlog::info("MoveSpeedEnable changed to {}", GameManager::moveSpeedEnable);
        if (GameManager::moveSpeedEnable)
        {
            gameManager.EnableMoveSpeed();
        }
        else
        {
            gameManager.DisableMoveSpeed();
        }
    }

    if (GameManager::moveSpeedEnable)
    {
        float currentMoveSpeed = GameManager::moveSpeed;
        if (GameManager::speedHackEnable)
        {
            currentMoveSpeed = currentMoveSpeed * GameManager::speedHack;
            if (currentMoveSpeed > 10.0f)
            {
                GameManager::moveSpeed = 10.0f/GameManager::speedHack;
            }
        }
        // slider from 1 to 10
        ImGui::SliderFloat("MoveSpeed", &GameManager::moveSpeed, 1.0f, 10.0f);
    }

    if (ImGui::Checkbox("SpeedHackEnable", &GameManager::speedHackEnable))
    {
		spdlog::info("SpeedHackEnable changed to {}", GameManager::speedHackEnable);
        if (GameManager::speedHackEnable)
        {
			gameManager.EnableSpeedHack();
		}
        else
        {
			gameManager.DisableSpeedHack();
		}
	}

    if (GameManager::speedHackEnable)
    {
        
		// slider from 1 to 10
        if (ImGui::SliderFloat("SpeedHack", &GameManager::speedHack, 1.0f, 10.0f))
        {
			gameManager.SetSpeed(GameManager::speedHack);
		}
        
	}
    static bool isAutoSwitch = true;
    ImGui::Checkbox("isAutoSwitch", &isAutoSwitch);
    auto aroundPlayersName = gameManager.GetAroundPlayersName();

    if (isAutoSwitch)
    {

        if (aroundPlayersName.size() > 0 && !isTempPause)
        {
            End();
            isTempPause = true;
        }
        else if (aroundPlayersName.size() == 0 && isTempPause)
        {
            GameManager::attackRangeEnable    = true;
            GameManager::attackSpeedEnable    = true;
            GameManager::skillRangeEnable     = true;
            GameManager::itemNoCoolDownEnable = true;
            GameManager::skillSpeedEnable     = true;
            GameManager::moveSpeedEnable      = true;
            GameManager::speedHackEnable      = true;

            isTempPause = false;
            gameManager.EnableMoveSpeed();
            gameManager.EnableAttackSpeed();
            gameManager.EnableAttackRange();
            gameManager.EnableSkillRange();
            gameManager.EnableItemNoCoolDown();
            gameManager.EnableSkillSpeed();
            gameManager.EnableSpeedHack();
        }
    }
    ImGui::Text("Around Players: %d", aroundPlayersName.size());
    ImGui::End();
}

void GameSetting::End()
{
    if (GameManager::attackRangeEnable)
    {
        GameManager::attackRangeEnable = false;
        GameManager gameManager;
        gameManager.DisableAttackRange();
    }
    if (GameManager::attackSpeedEnable)
    {
        GameManager::attackSpeedEnable = false;
        GameManager gameManager;
        gameManager.DisableAttackSpeed();
    }

    if (GameManager::skillRangeEnable)
    {
        GameManager::skillRangeEnable = false;
        GameManager gameManager;
        gameManager.DisableSkillRange();
    }

    if (GameManager::itemNoCoolDownEnable)
    {
        GameManager::itemNoCoolDownEnable = false;
        GameManager gameManager;
        gameManager.DisableItemNoCoolDown();
    }

    if (GameManager::skillSpeedEnable)
    {
        GameManager::skillSpeedEnable = false;
        GameManager gameManager;
        gameManager.DisableSkillSpeed();
    }

    if (GameManager::moveSpeedEnable)
    {
        GameManager::moveSpeedEnable = false;
        GameManager gameManager;
        gameManager.DisableMoveSpeed();
    }

    if (GameManager::speedHackEnable)
    {
        GameManager::speedHackEnable = false;
        GameManager gameManager;
        gameManager.DisableSpeedHack();
    }
}
