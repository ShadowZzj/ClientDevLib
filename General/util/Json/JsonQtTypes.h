#pragma once

#include "Json.h"
#include <QByteArray>
#include <QMetaType>
#include <QString>

// Only QString's UTF-8 conversion is adapted. JSON storage, parsing, access and
// serialization remain nlohmann::json throughout the application.
namespace nlohmann {
template <> struct adl_serializer<QString> {
    static void to_json(json& document, const QString& value) {
        const auto bytes = value.toUtf8();
        document = std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    }
    static void from_json(const json& document, QString& value) {
        const auto& text = document.get_ref<const std::string&>();
        value = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    }
};
} // namespace nlohmann

namespace zzj::json {
inline Json readObjectFile(const QString& path, std::size_t maximumBytes = 1024 * 1024) {
#ifdef _WIN32
    return readObjectFile(std::filesystem::path(path.toStdWString()), maximumBytes);
#else
    return readObjectFile(std::filesystem::u8path(path.toUtf8().toStdString()), maximumBytes);
#endif
}
inline Json parseObject(const QByteArray& bytes, std::size_t maximumBytes = 1024 * 1024) {
    return parseObject(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), maximumBytes);
}
inline QByteArray dumpBytes(const Json& document, int indent = -1) {
    return QByteArray::fromStdString(document.dump(indent));
}
} // namespace zzj::json

Q_DECLARE_METATYPE(zzj::json::Json)
