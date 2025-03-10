#pragma once
#include <General/util/Exception/Exception.h>
#include <Windows/util/Device/Keyboard.h>
#include <Windows/util/Device/Mouse.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <memory>
#include <optional>
#include <string>
#include <windows.h>

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
    Window(int width, int height, const std::string &name);
    ~Window();
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;
    void SetTitle(const std::string &title);
    HWND GetHWND() const noexcept;
    void EnableCursor() noexcept;
    void DisableCursor() noexcept;
    bool CursorEnabled() const noexcept;
    static std::optional<int> ProcessMessages();

  private:
    void ConfineCursor() noexcept;
    void FreeCursor() noexcept;
    void ShowCursor() noexcept;
    void HideCursor() noexcept;
    void EnableImGuiMouse() noexcept;
    void DisableImGuiMouse() noexcept;
    static LRESULT CALLBACK HandleMsgSetup(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    static LRESULT CALLBACK HandleMsgThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT HandleMsg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

  public:
    Keyboard kbd;
    Mouse mouse;

  private:
    bool cursorEnabled = true;
    int width;
    int height;
    HWND hWnd;
    std::vector<BYTE> rawBuffer;
};
}; // namespace zzj
