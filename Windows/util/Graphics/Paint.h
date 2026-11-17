#pragma once
#include <Windows.h>
#include <gdiplus.h>

//If you see a argument requires ScopedHDC, you can pass HDC and don't need to ReleaseDC in your client code, this class will handle release internally.
namespace zzj {
	class ScopedHDC {
	public:
		ScopedHDC() {
			hdc = NULL;
		}
		ScopedHDC(HWND window) {
			ScopedHDC();
			HDC hdc = ::GetDC(window);
			Bind(hdc);
		}
		ScopedHDC(HDC hdc) :hdc(hdc) {}
		ScopedHDC(ScopedHDC& other) {
			other.StealDataTo(*this);
		}
		~ScopedHDC() { Release(); }

		ScopedHDC& operator=(ScopedHDC& other) {
			other.StealDataTo(*this);
			return *this;
		}
		operator HDC() {
			return hdc;
		}
		void Bind(HDC hdc) {
			if (hdc) {
				Release();
			}
			this->hdc = hdc;
		}
	private:
		HDC hdc;

		void Release() {
			ReleaseDC(WindowFromDC(hdc), hdc);
			hdc = NULL;
		}

		void StealDataTo(ScopedHDC& other) {
			other.Release();
			other.hdc = hdc;
			hdc = NULL;
		}
	};

	class GuiDevice {
	public:
		struct ComposedXY
		{
			int x;
			int y;
		};
		GuiDevice(ScopedHDC hdc) :hdc(hdc) {
		}
		GuiDevice(HWND _window):hdc(_window){
		}
		GuiDevice(){}
		ComposedXY GetDpi();
		ComposedXY DLUToPixel(ComposedXY dlu);
		ComposedXY PTToPixel(ComposedXY pt);
		void Bind(ScopedHDC _hdc);
		static HDC GetScreenHDC();
		static HDC GetHDC(HWND wndHandle);
	private:
		ComposedXY GetBase();
		ScopedHDC hdc;
	};

};

