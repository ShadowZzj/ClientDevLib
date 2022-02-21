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

//��stringת����wstring  
wstring string2wstring(string str)
{
    wstring result;
    //��ȡ��������С��������ռ䣬��������С���ַ�����  
    int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.size(), NULL, 0);
    wchar_t* buffer = new wchar_t[len + 1];
    //���ֽڱ���ת���ɿ��ֽڱ���  
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.size(), buffer, len);
    buffer[len] = '\0';             //����ַ�����β  
    //ɾ��������������ֵ  
    result.append(buffer);
    delete[] buffer;
    return result;
}

//��wstringת����string  
string wstring2string(wstring wstr)
{
    string result;
    //��ȡ��������С��������ռ䣬��������С�°��ֽڼ����  
    int len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), NULL, 0, NULL, NULL);
    char* buffer = new char[len + 1];
    //���ֽڱ���ת���ɶ��ֽڱ���  
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), buffer, len, NULL, NULL);
    buffer[len] = '\0';
    //ɾ��������������ֵ  
    result.append(buffer);
    delete[] buffer;
    return result;
}

#endif