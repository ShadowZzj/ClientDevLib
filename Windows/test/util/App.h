#pragma once
#include "ChiliTimer.h"
#include <Windows/util/Graphics/Camera.h>
#include <Windows/util/Graphics/Graphics.h>
#include <Windows/util/Graphics/ImguiManager.h>
#include <Windows/util/Graphics/PointLight.h>
#include <Windows/util/Graphics/Window.h>
#include <set>

namespace zzj
{
class App
{
  public:
    App(int width, int height, const std::string &name);
    // master frame / message loop
    int Go();
    ~App();

  private:
    void DoFrame();
    void SpawnSimulationWindow() noexcept;
    void SpawnBoxWindowManagerWindow() noexcept;
    void SpawnBoxWindows() noexcept;

  private:
    ImguiManager imgui;
    Window wnd;
    Graphics gfx;
    ChiliTimer timer;
    Camera cam;
    std::vector<std::unique_ptr<class Drawable>> drawables;
    std::vector<class Box *> boxes;
    float speed_factor                 = 1.0f;
    static constexpr size_t nDrawables = 180;
    PointLight light;
    std::optional<int> comboBoxIndex;
    std::set<int> boxControlIds;
};
}; // namespace zzj