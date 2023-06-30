#include "paint.h"
#include <iostream>
using namespace zzj;
GuiDevice::ComposedXY GuiDevice::GetDpi()
{
	ComposedXY xy;
	xy.x= GetDeviceCaps(hdc, LOGPIXELSX);
	xy.y= GetDeviceCaps(hdc, LOGPIXELSY);
	return xy;
}



GuiDevice::ComposedXY GuiDevice::DLUToPixel(GuiDevice::ComposedXY dlu)
{
	int baseX = GetBase().x;
	int baseY = GetBase().y;
	ComposedXY ret;
	ret.x = dlu.x * baseX / 4;
	ret.y = dlu.y * baseY / 4;
	return ret;
}

GuiDevice::ComposedXY GuiDevice::PTToPixel(ComposedXY pt)
{
	ComposedXY ret;
	ComposedXY dpi = GetDpi();

	ret.x = pt.x * dpi.x / 72;
	ret.y = pt.y * dpi.y / 72;
	return ret;
}

void zzj::GuiDevice::Bind(ScopedHDC _hdc)
{
	hdc = _hdc;
}

HDC GuiDevice::GetScreenHDC()
{
	return GetDC(0);
}

HDC GuiDevice::GetHDC(HWND wndHandle)
{
	return GetDC(wndHandle);
}

GuiDevice::ComposedXY GuiDevice::GetBase()
{
	HWND hwnd = WindowFromDC(hdc);
	RECT rc = { 0,0,4,8 };
	MapDialogRect(hwnd, &rc);
	ComposedXY ret;
	ret.x = rc.right;
	ret.y = rc.bottom;

	
	return ret;
}
