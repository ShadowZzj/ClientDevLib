#pragma once
#include <QSslConfiguration>
#include <QString>

namespace zzj::network::qt {
struct TlsFiles {
    QString ca, certificate, privateKey;
};
// Throws on missing/invalid credentials. Never ignores verification errors.
QSslConfiguration loadMutualTls(const TlsFiles& files);
} // namespace zzj::network::qt
