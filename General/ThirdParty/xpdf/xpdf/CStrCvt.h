#pragma once
#include <string>
using namespace std;

//u8string to wstring
std::wstring utf8_to_wstring(const std::string& str);

//wstring to u8string
std::string wstring_to_utf8(const std::wstring& str);



wstring string2wstring(string str);

string wstring2string(wstring wstr);