#include "PasswordHash.h"
#include <General/util/Security/ConstantTime.h>
#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QStringList>
#include <array>
#include <stdexcept>

namespace zzj::security::qt {
namespace {
constexpr int Iterations = 600000;
QByteArray randomBytes(int count) {
    std::array<quint32, 8> words{};
    QRandomGenerator::system()->fillRange(words.data(), static_cast<qsizetype>(words.size()));
    return QByteArray(reinterpret_cast<const char*>(words.data()), count);
}
bool equal(const QByteArray& a, const QByteArray& b) {
    return zzj::security::constantTimeEqual(
        std::string_view(a.constData(), static_cast<std::size_t>(a.size())),
        std::string_view(b.constData(), static_cast<std::size_t>(b.size())));
}
} // namespace
QString hashPassword(const QByteArray& password) {
    if (password.size() < 8 || password.size() > 1024 || password.contains('\0'))
        throw std::invalid_argument("Password must contain 8 to 1024 bytes");
    const auto salt = randomBytes(16);
    const auto key = QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password, salt, Iterations, 32);
    if (key.size() != 32)
        throw std::runtime_error("Password derivation failed");
    return QStringLiteral("pbkdf2-sha256$600000$") + QString::fromLatin1(salt.toHex()) +
        QLatin1Char('$') + QString::fromLatin1(key.toHex());
}
bool verifyPassword(const QByteArray& password, const QString& encoded) {
    const auto parts = encoded.split(QLatin1Char('$'));
    if (password.size() > 1024 || password.contains('\0') || parts.size() != 4 ||
        parts[0] != QLatin1String("pbkdf2-sha256") || parts[1] != QLatin1String("600000") ||
        parts[2].size() != 32 || parts[3].size() != 64)
        return false;
    const auto salt = QByteArray::fromHex(parts[2].toLatin1());
    const auto expected = QByteArray::fromHex(parts[3].toLatin1());
    if (salt.size() != 16 || expected.size() != 32 || salt.toHex() != parts[2].toLatin1() ||
        expected.toHex() != parts[3].toLatin1())
        return false;
    const auto actual = QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password, salt, Iterations, 32);
    return actual.size() == 32 && equal(actual, expected);
}
QByteArray randomToken() { return randomBytes(32).toHex(); }
QByteArray tokenSha256(const QByteArray& token) {
    return QCryptographicHash::hash(token, QCryptographicHash::Sha256);
}
} // namespace zzj::security::qt
