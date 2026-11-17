#include "CStrCvt.h"


#include <locale>
#include <codecvt>
#pragma warning(disable:4996)


#ifdef _WIN32
#include <Windows.h>
#endif


//u8string to wstring
std::wstring utf8_to_wstring(const std::string& str)
{
    std::wstring_convert< std::codecvt_utf8_utf16<wchar_t> > strCnv;
    return strCnv.from_bytes(str);
}

//wstring to u8string
std::string wstring_to_utf8(const std::wstring& str)
{
    std::wstring_convert< std::codecvt_utf8_utf16<wchar_t> > strCnv;
    return strCnv.to_bytes(str);

}

#ifdef _WIN32

//将string转换成wstring  
wstring string2wstring(string str)
{
    wstring result;
    //获取缓冲区大小，并申请空间，缓冲区大小按字符计算  
    int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.size(), NULL, 0);
    wchar_t* buffer = new wchar_t[len + 1];
    //多字节编码转换成宽字节编码  
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.size(), buffer, len);
    buffer[len] = '\0';             //添加字符串结尾  
    //删除缓冲区并返回值  
    result.append(buffer);
    delete[] buffer;
    return result;
}

//将wstring转换成string  
string wstring2string(wstring wstr)
{
    string result;
    //获取缓冲区大小，并申请空间，缓冲区大小事按字节计算的  
    int len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), NULL, 0, NULL, NULL);
    char* buffer = new char[len + 1];
    //宽字节编码转换成多字节编码  
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), buffer, len, NULL, NULL);
    buffer[len] = '\0';
    //删除缓冲区并返回值  
    result.append(buffer);
    delete[] buffer;
    return result;
}

#endif