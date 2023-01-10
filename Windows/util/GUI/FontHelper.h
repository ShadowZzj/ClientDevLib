#pragma once
#include <Windows.h>
namespace zzj {
	class Font {
	public:
		static void EnumFontFamilies(HDC hdc);
		static void EnumFonts(HDC hdc);
		static int InstallFontTemp(const char* file);
		static int InstallFontPermanent(const char* file);
	};
}