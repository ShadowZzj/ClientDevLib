#include "StringUtil.h"
std::wstring NSStringToStringW(NSString *Str)
{
    @autoreleasepool {
    NSStringEncoding pEncode = CFStringConvertEncodingToNSStringEncoding(kCFStringEncodingUTF32LE);
    NSData *pSData           = [Str dataUsingEncoding:pEncode];

    return std::wstring((wchar_t *)[pSData bytes], [pSData length] / sizeof(wchar_t));
    }
}

NSString *StringWToNSString(const std::wstring &Str)
{
    @autoreleasepool {
    NSString *pString =
        [[NSString alloc] initWithBytes:(char *)Str.data()
                                 length:Str.size() * sizeof(wchar_t)
                               encoding:CFStringConvertEncodingToNSStringEncoding(kCFStringEncodingUTF32LE)];
    return pString;
    }
}

std::string Wstr2Str(std::wstring wStr)
{
    @autoreleasepool {
        NSString *pString = StringWToNSString(wStr);
        return [pString UTF8String];
    }
}

std::wstring Str2Wstr(std::string str)
{
    @autoreleasepool {
        NSString *pString = [NSString stringWithUTF8String:str.c_str()];
        return NSStringToStringW(pString);
    }
}




