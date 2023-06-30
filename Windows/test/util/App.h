#pragma once
#include "ChiliTimer.h"
#include <Windows/util/Graphics/Graphics.h>
#include <Windows/util/Graphics/Window.h>

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

  private:
    Window wnd;
    Graphics gfx;
    ChiliTimer timer;
    std::vector<std::unique_ptr<class Drawable>> drawables;
    static constexpr size_t nDrawables = 180;
};
}; // namespace zzj