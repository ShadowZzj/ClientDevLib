#pragma once
#include <QByteArray>
#include <QString>

namespace zzj::security::qt {
// Thin wrappers around Qt's PBKDF2 and operating-system random source.
QString hashPassword(const QByteArray& password);
bool verifyPassword(const QByteArray& password, const QString& encoded);
QByteArray randomToken();
QByteArray tokenSha256(const QByteArray& token);
} // namespace zzj::security::qt
