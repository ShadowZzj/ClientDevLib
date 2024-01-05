#include <codecvt>
#include <iostream>
#include <vector>
#include <boost/locale.hpp>
#include <spdlog/spdlog.h>
#include <General/util/StrUtil.h>
int main()
{
    system("chcp 65001");
    // 假设你的输入是一个包含Big5编码的std::vector<uint8_t>
    std::vector<uint8_t> big5Data = {static_cast<uint8_t>('\xa4'), static_cast<uint8_t>('\x40'),
                                     static_cast<uint8_t>('\xc0'), static_cast<uint8_t>('\xbb')};

    std::string big5String = "\xa4\x40\xc0\xbb\x00\x00";
    // Convert Big5 string to Unicode wide string using Boost Locale
    std::string unicodeString = boost::locale::conv::to_utf<char>(big5String, "Big5");

   // Convert wide string to UTF - 8 std::wstring_convert<std::codecvt_utf8<wchar_t>> utf8Converter;
    //std::string narrowString = zzj::str::w2utf8(unicodeString);

    // Use spdlog to log the narrow string
    spdlog::info("Unicode String (Narrow): {}", unicodeString);

    return 0;
}