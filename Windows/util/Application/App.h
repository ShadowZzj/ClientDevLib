#pragma once
#include <Windows/util/Device/Window.h>

namespace zzj
{
class App
{
  public:
    App(int width, int height, const std::string &name);
    // master frame / message loop
    int Go();

  private:
    void DoFrame();

  private:
    Window wnd;
};
}; // namespace zzj