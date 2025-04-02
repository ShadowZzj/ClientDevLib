#include "Exception.h"
#include <General/util/StrUtil.h>
#include <json.hpp>

using namespace zzj;
using namespace nlohmann;
Exception::Exception(int line, const std::string &file, const std::string &func,
                     const std::string &msg, int code) noexcept
    : line(line), file(file), msg(msg), code(code)
{
}
const char *Exception::what() const noexcept
{
    try
    {
        json j;
        j["line"] = line;
        j["file"] = file;
        j["msg"] = str::ansi2utf8(msg);
        j["code"] = code;
        j["func"] = func;
        exceptionMessage = j.dump();
        return exceptionMessage.c_str();
    }
    catch (...)
    {
        return "Exception::what() failed";
    }
}

int zzj::Exception::GetCode() const { return code; }

std::string zzj::Exception::GetMsg() const { return msg; }

zzj::CryptoException::CryptoException(int line, const std::string &file, const std::string &func,
                                      const std::string &msg) noexcept
    : Exception(line, file, func, msg, 0)
{
}