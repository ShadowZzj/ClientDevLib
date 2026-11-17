#include "FontHelper.h"
#include <iostream>
#include <string>
#include <Windows/util/Process/ProcessHelper.h>
#include <filesystem>
using namespace zzj;
BOOL CALLBACK EnumFamCallBack(LPLOGFONTW lplf, LPNEWTEXTMETRIC lpntm, DWORD FontType, LPVOID aFontCount);
void zzj::Font::EnumFontFamilies(HDC hdc)
{
	::EnumFontFamiliesW(hdc, (LPCWSTR)NULL,
		(FONTENUMPROCW)EnumFamCallBack, (LPARAM)NULL);
}
BOOL CALLBACK EnumFamCallBack(LPLOGFONTW lplf, LPNEWTEXTMETRIC lpntm, DWORD FontType, LPVOID aFontCount)
{
	std::wstring str = lplf->lfFaceName;
	std::wcout << str << std::endl;
	return true;


}
void zzj::Font::EnumFonts(HDC hdc) {
	::EnumFonts(hdc, NULL, (FONTENUMPROC)EnumFamCallBack, NULL);
}

int zzj::Font::InstallFontTemp(const char* file)
{
	int ret= AddFontResourceExA(file, FR_PRIVATE, NULL);
	return ret;
}

int zzj::Font::InstallFontPermanent(const char* file)
{
	wchar_t windowsFontPath[MAX_PATH]{};
	Process process;
	process.envHelper.ExpandEnvVariable(L"%windir%", windowsFontPath, MAX_PATH);
	lstrcatW(windowsFontPath, L"\\Fonts");
	try
	{
		std::filesystem::copy(file,windowsFontPath);
		InstallFontTemp(file);
	}
	catch (const std::exception&)
	{
		
	}
	return 0;
}
