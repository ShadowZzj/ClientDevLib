#include "App.h"
using namespace zzj;
App::App(int width, int height, const std::string &name)
	:
	wnd(width, height, name.c_str())
{}

int App::Go()
{
	while( true )
	{
		// process all messages pending, but to not block for new messages
		if( const auto ecode = Window::ProcessMessages() )
		{
			// if return optional has value, means we're quitting so return exit code
			return *ecode;
		}
		DoFrame();
	} 
}

void App::DoFrame()
{
    wnd.Gfx().ClearBuffer(0.0f, 0.0f, 0.5f);
	wnd.Gfx().DrawTestTriangle(1);
	wnd.Gfx().EndFrame();
}