#include "TlsConfiguration.h"
#include <QFile>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <stdexcept>

namespace zzj::network::qt {
namespace {
QByteArray read(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024)
        throw std::runtime_error("Cannot read TLS credential file");
    return f.readAll();
}
} // namespace
QSslConfiguration loadMutualTls(const TlsFiles& files) {
    if (!QSslSocket::supportsSsl())
        throw std::runtime_error("No usable Qt TLS backend");
    const auto authorities = QSslCertificate::fromData(read(files.ca), QSsl::Pem);
    const auto chain = QSslCertificate::fromData(read(files.certificate), QSsl::Pem);
    const auto keyBytes = read(files.privateKey);
    QSslKey key(keyBytes, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
    if (key.isNull())
        key = QSslKey(keyBytes, QSsl::Ec, QSsl::Pem, QSsl::PrivateKey);
    if (authorities.isEmpty() || chain.isEmpty() || key.isNull())
        throw std::runtime_error("Invalid TLS certificate or private key");
    auto configuration = QSslConfiguration::defaultConfiguration();
    configuration.setProtocol(QSsl::TlsV1_2OrLater);
    configuration.setCaCertificates(authorities);
    configuration.setLocalCertificateChain(chain);
    configuration.setPrivateKey(key);
    configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
    configuration.setPeerVerifyDepth(5);
    return configuration;
}
} // namespace zzj::network::qt
