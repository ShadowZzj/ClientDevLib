#include "App.h"
#include <DirectXMath.h>
#include <General/util/File/File.h>
#include <Windows/util/Graphics/Math.h>
#include <boost/filesystem.hpp>
#include <imgui/imgui.h>
#include <memory>
#include <random>
#include <windows/util/Graphics/GDIPlusManager.h>

using namespace zzj;
GDIPlusManager gdipm;
App::App(int width, int height, const std::string &name)
    : wnd(width, height, name.c_str()), gfx(wnd.GetHWND(), width, height), light(gfx)
{
    gfx.SetProjection(DirectX::XMMatrixPerspectiveLH(1.0f, 9.0f / 16.0f, 0.5f, 40.0f));
}

int App::Go()
{
    while (true)
    {
        // process all messages pending, but to not block for new messages
        if (const auto ecode = Window::ProcessMessages())
        {
            // if return optional has value, means we're quitting so return exit code
            return *ecode;
        }
        DoFrame();
    }
}

void App::DoFrame()
{
    namespace dx  = DirectX;
    const auto dt = timer.Mark() * speed_factor;
    gfx.BeginFrame(0.07f, 0.0f, 0.12f);
    gfx.SetCamera(cam.GetMatrix());
    light.Bind(gfx, cam.GetMatrix());

    boost::filesystem::path currentExePath = zzj::GetExecutablePath();
    boost::filesystem::path modelPath      = currentExePath / "nano_textured/nanosuit.obj";

    if (!nano)
    {
        nano = std::make_unique<zzj::Model>(gfx, modelPath.string());
    }
    nano->Draw(gfx);
    nano->ShowWindow();

    light.Draw(gfx);

    while (auto e = wnd.kbd.ReadKey())
    {
        if (!e->IsPress())
        {
            continue;
        }

        switch (e->GetCode())
        {
        case VK_ESCAPE:
            if (wnd.CursorEnabled())
            {
                wnd.DisableCursor();
                wnd.mouse.EnableRaw();
            }
            else
            {
                wnd.EnableCursor();
                wnd.mouse.DisableRaw();
            }
            break;
        }
    }

    if (!wnd.CursorEnabled())
    {
        if (wnd.kbd.KeyIsPressed('W'))
        {
            cam.Translate({0.0f, 0.0f, dt});
        }
        if (wnd.kbd.KeyIsPressed('A'))
        {
            cam.Translate({-dt, 0.0f, 0.0f});
        }
        if (wnd.kbd.KeyIsPressed('S'))
        {
            cam.Translate({0.0f, 0.0f, -dt});
        }
        if (wnd.kbd.KeyIsPressed('D'))
        {
            cam.Translate({dt, 0.0f, 0.0f});
        }
        if (wnd.kbd.KeyIsPressed('R'))
        {
            cam.Translate({0.0f, dt, 0.0f});
        }
        if (wnd.kbd.KeyIsPressed('F'))
        {
            cam.Translate({0.0f, -dt, 0.0f});
        }
    }

    while (const auto delta = wnd.mouse.ReadRawDelta())
    {
        if (!wnd.CursorEnabled())
        {
            cam.Rotate((float)delta->x, (float)delta->y);
        }
    }

    // imgui windows
    cam.SpawnControlWindow();
    light.SpawnControlWindow();
    // ShowImguiDemoWindow();
    // present
    gfx.EndFrame();
}
App::~App()
{
}
void App::ShowImguiDemoWindow()
{
    static bool show_demo_window = true;
    if (show_demo_window)
    {
        ImGui::ShowDemoWindow(&show_demo_window);
    }
}