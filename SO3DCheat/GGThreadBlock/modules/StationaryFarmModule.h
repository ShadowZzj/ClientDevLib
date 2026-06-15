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
        s_instance.store(this);
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~StationaryFarmModule() override
    {
        StationaryFarmModule *expected = this;
        s_instance.compare_exchange_strong(expected, nullptr);
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    bool CanAutoPause() const override { return false; }

    // broker 远程下发定点坐标(自动复活页面「推送坐标到游戏」)。只写坐标,不动
    // enable 开关 —— 是否启用定点挂机仍由用户在游戏内自己勾。
    //   * 已启用: worker 下一拍就把新坐标写进游戏,角色随即被拉到新点。
    //   * 未启用: 坐标存进 savedX_/savedY_ 并打 pendingExternal_,下次用户勾启用时
    //             用这个坐标而不是「快照当前位置」,免得 web 推的点被覆盖。
    static bool ApplyRemoteCoords(int x, int y)
    {
        StationaryFarmModule *self = s_instance.load();
        if (!self)
            return false;
        self->savedX_.store(x);
        self->savedY_.store(y);
        self->hasSaved_.store(true);
        self->pendingExternal_.store(true);
        spdlog::info("GGTB::StationaryFarm: remote coords set ({}, {})", x, y);
        return true;
    }

    static bool GetSavedCoords(int &x, int &y)
    {
        StationaryFarmModule *self = s_instance.load();
        if (!self || !self->hasSaved_.load())
            return false;
        x = self->savedX_.load();
        y = self->savedY_.load();
        return true;
    }

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
            // web 刚推过坐标(pendingExternal_)→ 用推来的点,不快照当前位置。
            if (pendingExternal_.exchange(false) && hasSaved_.load())
            {
                spdlog::info("GGTB::StationaryFarm: locked remote position ({}, {})",
                             savedX_.load(), savedY_.load());
            }
            else
            {
                int cx = 0, cy = 0;
                if (TryReadCoords(cx, cy))
                {
                    savedX_.store(cx);
                    savedY_.store(cy);
                    hasSaved_.store(true);
                    spdlog::info("GGTB::StationaryFarm: locked position ({}, {})", cx, cy);
                }
                else
                {
                    enabled_ = false;
                    spdlog::warn("GGTB::StationaryFarm: cannot read coords, disabled");
                }
            }
        }
        else if (!enabled_ && prev)
        {
            hasSaved_.store(false);
            spdlog::info("GGTB::StationaryFarm: unlocked position");
        }

        if (enabled_ && hasSaved_.load())
        {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               u8"锁定坐标: (%d, %d)", savedX_.load(), savedY_.load());
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
        if (hasSaved_.load())
        {
            j["savedX"] = savedX_.load();
            j["savedY"] = savedY_.load();
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
            savedX_.store(j.value("savedX", 0));
            savedY_.store(j.value("savedY", 0));
            hasSaved_.store(true);
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

                    if (enabled_ && hasSaved_.load())
                    {
                        if (!TryWriteCoords(savedX_.load(), savedY_.load()))
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

    // 单实例(Setting 只注册一份),供 broker 远程命令 ApplyRemoteCoords 找到自己。
    static inline std::atomic<StationaryFarmModule *> s_instance{nullptr};

    std::atomic<int>  savedX_{0};
    std::atomic<int>  savedY_{0};
    std::atomic<bool> hasSaved_{false};
    // web 刚下发坐标、还没被「启用」消费的标记。见 ApplyRemoteCoords / OnRender。
    std::atomic<bool> pendingExternal_{false};

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
