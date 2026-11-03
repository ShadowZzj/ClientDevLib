#pragma once

#include <General/ThirdParty/json.hpp>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace zzj::json {
using Json = nlohmann::json;

// Parse with the existing nlohmann implementation. Never expose input fragments
// from parser exceptions, because configuration/control messages carry secrets.
inline Json parseObject(std::string_view bytes, std::size_t maximumBytes = 1024 * 1024) {
    if (!maximumBytes || bytes.size() > maximumBytes)
        throw std::runtime_error("JSON object exceeds size limit");
    Json document;
    try {
        document = Json::parse(bytes.begin(), bytes.end(),
            [](int depth, Json::parse_event_t, Json&) {
                if (depth > 1024)
                    throw std::runtime_error("JSON nesting limit exceeded");
                return true;
            });
    } catch (const Json::exception&) {
        throw std::runtime_error("Invalid JSON object");
    }
    if (!document.is_object())
        throw std::runtime_error("JSON document must be an object");
    return document;
}

inline Json readObjectFile(const std::filesystem::path& path, std::size_t maximumBytes = 1024 * 1024) {
    const auto streamLimit = static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)());
    if (!maximumBytes || maximumBytes >= streamLimit)
        throw std::runtime_error("Invalid JSON file read limit");
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
        throw std::runtime_error("Cannot read JSON file");
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximumBytes)
        throw std::runtime_error("JSON file exceeds read limit");
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open JSON file");
    std::string bytes(maximumBytes + 1, '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto count = file.gcount();
    if (file.bad() || count < 0 || static_cast<std::size_t>(count) > maximumBytes ||
        (!file.eof() && file.peek() != std::char_traits<char>::eof()))
        throw std::runtime_error("JSON file exceeds read limit");
    bytes.resize(static_cast<std::size_t>(count));
    return parseObject(bytes, maximumBytes);
}
} // namespace zzj::json
