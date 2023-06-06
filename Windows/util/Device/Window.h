#pragma once
#include "Keyboard.h"
#include "Mouse.h"
#include <General/util/Exception/Exception.h>
#include <optional>
#include <windows.h>
#include <string>
#include <memory>
#include "Graphics.h"
namespace zzj
{
class Window
{
  private:
    // singleton manages registration/cleanup of window class
    class WindowClass
    {
      public:
        static const char *GetName() noexcept;
        static HINSTANCE GetInstance() noexcept;

      private:
        WindowClass() noexcept;
        ~WindowClass();
        WindowClass(const WindowClass &) = delete;
        WindowClass &operator=(const WindowClass &) = delete;
        static constexpr const char *wndClassName   = "ZZJ Direct3D Engine Window";
        static WindowClass wndClass;
        HINSTANCE hInst;
    };

  public:
    Window(int width, int height, const std::string& name);
    ~Window();
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;
    void SetTitle(const std::string &title);
    static std::optional<int> ProcessMessages();
    Graphics &Gfx();
  private:
    static LRESULT CALLBACK HandleMsgSetup(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    static LRESULT CALLBACK HandleMsgThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT HandleMsg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

  public:
    Keyboard kbd;
    Mouse mouse;

  private:
    int width;
    int height;
    HWND hWnd;
    std::unique_ptr<Graphics> pGfx;
};
}; // namespace zzj
