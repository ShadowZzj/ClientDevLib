#pragma once
#include "ChiliTimer.h"
#include <Windows/util/Graphics/Camera.h>
#include <Windows/util/Graphics/Drawable/Mesh.h>
#include <Windows/util/Graphics/Graphics.h>
#include <Windows/util/Graphics/ImguiManager.h>
#include <Windows/util/Graphics/PointLight.h>
#include <Windows/util/Graphics/Window.h>
#include <Windows/util/Graphics/Drawable/Mesh.h>
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
    void ShowImguiDemoWindow();

  private:
    ImguiManager imgui;
    Window wnd;
    ChiliTimer timer;
    float speed_factor = 1.0f;
    Camera cam;
    Graphics gfx;
    PointLight light;
    std::unique_ptr<zzj::Model> nano;
};
}; // namespace zzj