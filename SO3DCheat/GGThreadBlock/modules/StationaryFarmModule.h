#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "../util/UserConfig.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>
#include <thread>

namespace GGTB
{

class StationaryFarmModule : public IModule
{
  public:
    StationaryFarmModule() : IModule(u8"定点挂机")
    {
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~StationaryFarmModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    bool CanAutoPause() const override { return false; }

    void OnRender() override
    {
        const bool armed = IsArmed();

        if (!armed)
        {
            ImGui::TextDisabled(u8"等待角色加载稳定 (登录完成后 20s)");
            // Disable the whole control block — no reads, no writes to 123.dll
            // until the grace period has elapsed.
            ImGui::BeginDisabled();
        }

        bool prev = enabled_;
        ImGui::Checkbox(u8"启用定点挂机##StationaryFarm", &enabled_);

        if (armed && enabled_ && !prev)
        {
            int cx = 0, cy = 0;
            if (TryReadCoords(cx, cy))
            {
                savedX_ = cx;
                savedY_ = cy;
                hasSaved_ = true;
                spdlog::info("GGTB::StationaryFarm: locked position ({}, {})", savedX_, savedY_);
            }
            else
            {
                enabled_ = false;
                spdlog::warn("GGTB::StationaryFarm: cannot read coords, disabled");
            }
        }
        else if (!enabled_ && prev)
        {
            hasSaved_ = false;
            spdlog::info("GGTB::StationaryFarm: unlocked position");
        }

        if (enabled_ && hasSaved_)
        {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               u8"锁定坐标: (%d, %d)", savedX_, savedY_);
        }
        else if (armed)
        {
            DWORD now = GetTickCount();
            if (now - lastReadMs_ >= 1000 || lastReadMs_ == 0)
            {
                int cx = 0, cy = 0;
                if (TryReadCoords(cx, cy))
                {
                    displayX_ = cx;
                    displayY_ = cy;
                    displayValid_ = true;
                }
                else
                {
                    displayValid_ = false;
                }
                lastReadMs_ = now;
            }
            if (displayValid_)
                ImGui::Text(u8"当前坐标: (%d, %d)", displayX_, displayY_);
            else
                ImGui::TextDisabled(u8"(无法读取坐标)");
        }

        if (!armed)
            ImGui::EndDisabled();
    }

    std::string ConfigKey() const override { return "StationaryFarm"; }

    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        if (hasSaved_)
        {
            j["savedX"] = savedX_;
            j["savedY"] = savedY_;
            j["hasSaved"] = true;
        }
        else
        {
            j["hasSaved"] = false;
        }
    }

    void LoadState(const nlohmann::json &j) override
    {
        bool hadSaved = j.value("hasSaved", false);
        if (hadSaved)
        {
            savedX_ = j.value("savedX", 0);
            savedY_ = j.value("savedY", 0);
            hasSaved_ = true;
        }
        IModule::LoadState(j);
    }

  private:
    static constexpr uintptr_t kCoordPtrOffset      = 0x5BFE34;
    static constexpr const char *kModuleName        = "123.dll";

    static uintptr_t GetCoordStructPtr()
    {
        HMODULE hMod = GetModuleHandleA(kModuleName);
        if (!hMod)
            return 0;
        uintptr_t base = reinterpret_cast<uintptr_t>(hMod);
        uintptr_t ptrAddr = base + kCoordPtrOffset;
        uintptr_t pStruct = *reinterpret_cast<uintptr_t *>(ptrAddr);
        return pStruct;
    }

    static bool TryReadCoords(int &outX, int &outY)
    {
        __try
        {
            uintptr_t pStruct = GetCoordStructPtr();
            if (!pStruct)
                return false;
            outX = *reinterpret_cast<int *>(pStruct + 0);
            outY = *reinterpret_cast<int *>(pStruct + 4);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool TryWriteCoords(int x, int y)
    {
        __try
        {
            uintptr_t pStruct = GetCoordStructPtr();
            if (!pStruct)
                return false;
            *reinterpret_cast<int *>(pStruct + 0) = x;
            *reinterpret_cast<int *>(pStruct + 4) = y;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WorkerLoop()
    {
        // Delay ANY access to 123.dll (read or write) until the user's
        // character is REALLY loaded in-game. "LocalUserPtr != 0" is not a
        // good enough signal — the pointer can be non-null on the character-
        // select screen while the engine is still wiring things up. Instead
        // we key the grace period off UserConfig::IsReady(), which flips
        // true only after GetLocalPlayerName() returns a non-empty name
        // (i.e. the character is actually in the world). Once ready, wait an
        // extra 20s before touching the coord struct.
        //
        // UI also checks IsArmed() before calling TryReadCoords(), so both
        // the "lock position" click path and the live-coord display respect
        // the same grace window.
        constexpr DWORD kPostReadyDelayMs = 20000;
        bool  armedLogged = false;

        while (!stop_.load())
        {
            if (!UserConfig::IsReady())
            {
                readyTickMs_.store(0);
                armed_.store(false);
                armedLogged = false;
            }
            else
            {
                DWORD first = readyTickMs_.load();
                if (first == 0)
                {
                    first = GetTickCount();
                    readyTickMs_.store(first);
                }

                DWORD elapsed = GetTickCount() - first;
                if (elapsed >= kPostReadyDelayMs)
                {
                    if (!armedLogged)
                    {
                        spdlog::info("GGTB::StationaryFarm: armed 20s after UserConfig ready");
                        armedLogged = true;
                    }
                    armed_.store(true);

                    if (enabled_ && hasSaved_)
                    {
                        if (!TryWriteCoords(savedX_, savedY_))
                            spdlog::warn("GGTB::StationaryFarm: write failed");
                    }
                }
                else
                {
                    armed_.store(false);
                }
            }

            for (int i = 0; i < 10 && !stop_.load(); ++i)
                Sleep(100);
        }
    }

    bool IsArmed() const { return armed_.load(); }

    int  savedX_   = 0;
    int  savedY_   = 0;
    bool hasSaved_ = false;

    // UI display cache — throttled to once per second so we don't hammer the
    // game pointer on every frame while the user hasn't locked a position.
    int   displayX_     = 0;
    int   displayY_     = 0;
    bool  displayValid_ = false;
    DWORD lastReadMs_   = 0;

    std::atomic<bool>  stop_{false};
    std::atomic<bool>  armed_{false};
    std::atomic<DWORD> readyTickMs_{0};
    std::thread        worker_;
};

} // namespace GGTB
