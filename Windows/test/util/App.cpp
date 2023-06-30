#include "App.h"
#include <Windows/util/Graphics/Drawable/Box.h>
#include <Windows/util/Graphics/Drawable/Melon.h>
#include <Windows/util/Graphics/Drawable/Pyramid.h>
#include <Windows/util/Graphics/Drawable/Sheet.h>
#include <Windows/util/Graphics/Drawable/SkinnedBox.h>
#include <Windows/util/Graphics/Math.h>
#include <memory>
#include <windows/util/Graphics/GDIPlusManager.h>

using namespace zzj;
GDIPlusManager gdipm;
App::App(int width, int height, const std::string &name) : wnd(width, height, name.c_str()), gfx(wnd.GetHWND())
{
    class Factory
    {
      public:
        Factory(Graphics &gfx) : gfx(gfx)
        {
        }
        std::unique_ptr<Drawable> operator()()
        {
            switch (typedist(rng))
            {

            case 0:
                return std::make_unique<Pyramid>(gfx, rng, adist, ddist, odist, rdist);
            case 1:
                return std::make_unique<Box>(gfx, rng, adist, ddist, odist, rdist, bdist);
            case 2:
                return std::make_unique<Melon>(gfx, rng, adist, ddist, odist, rdist, longdist, latdist);
            case 3:
                return std::make_unique<Sheet>(gfx, rng, adist, ddist, odist, rdist);
            case 4:
                return std::make_unique<SkinnedBox>(gfx, rng, adist, ddist, odist, rdist);
            default:
                assert(false && "bad drawable type in factory");
                return {};
            }
        }

      private:
        Graphics &gfx;
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> adist{0.0f, zzj::PI * 2.0f};
        std::uniform_real_distribution<float> ddist{0.0f, zzj::PI * 0.5f};
        std::uniform_real_distribution<float> odist{0.0f, zzj::PI * 0.08f};
        std::uniform_real_distribution<float> rdist{6.0f, 20.0f};
        std::uniform_real_distribution<float> bdist{0.4f, 3.0f};
        std::uniform_int_distribution<int> latdist{5, 20};
        std::uniform_int_distribution<int> longdist{10, 40};
        std::uniform_int_distribution<int> typedist{0, 4};
    };

    drawables.reserve(nDrawables);
    std::generate_n(std::back_inserter(drawables), nDrawables, Factory(gfx));
    gfx.SetProjection(DirectX::XMMatrixPerspectiveLH(1.0f, 3.0f / 4.0f, 0.5f, 40.0f));
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
    const auto dt = timer.Mark();
    gfx.ClearBuffer(0.07f, 0.0f, 0.12f);
    for (auto &d : drawables)
    {
        d->Update(wnd.kbd.KeyIsPressed(VK_SPACE) ? 0.0f : dt);
        d->Draw(gfx);
    }
    gfx.EndFrame();
}
App::~App()
{
}