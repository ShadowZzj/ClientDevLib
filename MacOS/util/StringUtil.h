#ifndef _MAC_STRINGUTIL_H_
#define _MAC_STRINGUTIL_H_

#import <Foundation/Foundation.h>
#include <string>

std::wstring NSStringToStringW(NSString *Str);
NSString *StringWToNSString(const std::wstring &Str);
std::string Wstr2Str(std::wstring wStr);
std::wstring Str2Wstr(std::string str);

#endif