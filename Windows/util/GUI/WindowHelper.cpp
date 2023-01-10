#include "WindowHelper.h"
void zzj::Window::Bind(HWND _window)
{
	window = _window;
    zzj::GuiDevice::Bind(GuiDevice::GetHDC(_window));
}
BOOL zzj::Window::MoveWindowPT(int x, int y, int cx, int cy,bool isPt)
{
	return MoveWindowPT(window, x, y, cx, cy,isPt);
}

BOOL zzj::Window::CenterWindow()
{
	if (!IsWindowValid())
		return false;
	ShowWindow(window,SW_NORMAL);
	RECT rtDesk;
	RECT rtDlg;
	::GetWindowRect(::GetDesktopWindow(), &rtDesk);
	::GetWindowRect(window,&rtDlg);
	int deskWidth = rtDesk.right - rtDesk.left;
	int deskHeight = rtDesk.bottom - rtDesk.top;
	int windowWidth = rtDlg.right - rtDlg.left;
	int windowHeight = rtDlg.bottom - rtDlg.top;
	int iXpos = deskWidth / 2 - windowWidth / 2;
	int iYpos = deskHeight / 2 - windowHeight / 2;
	SetWindowPos(window,NULL, iXpos, iYpos, 0, 0, SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOZORDER);
	return true;
}

BOOL zzj::Window::MoveWindowPT(HWND window, int x, int y, int cx, int cy,bool isPt)
{
	using namespace zzj;
	if (!window)
		return false;
    GuiDevice device(GuiDevice::GetHDC(window));
    GuiDevice::ComposedXY location;
    GuiDevice::ComposedXY size;
	if (isPt) {
		location = device.PTToPixel({ x,y });
		size = device.PTToPixel({ cx,cy });
	}
	else {
		location = { x,y };
		size = { cx,cy };
	}
	::MoveWindow(window, location.x, location.y, size.x, size.y, 1);
	return true;
}

BOOL zzj::Window::MoveWindowPT(HWND father,int controlId, int x, int y, int cx, int cy,bool isPt)
{
	HWND window = GetDlgItem(father, controlId);
	return MoveWindowPT(window, x, y, cx, cy, isPt);
}

bool zzj::Window::IsWindowValid()
{
	return window ? true : false;
}
