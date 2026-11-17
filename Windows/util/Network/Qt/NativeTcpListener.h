#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <QTcpServer>

namespace zzj::network::qt {
// Subclasses consume raw accepted SOCKETs through incomingConnection and NativeTcpStream::adopt.
class NativeTcpListener : public QTcpServer {
public:
    explicit NativeTcpListener(QObject* parent = nullptr);
    ~NativeTcpListener() override;
    bool listenExclusive(const QHostAddress& bindAddress, quint16 port);
    QString errorString() const { return nativeError_.isEmpty() ? QTcpServer::errorString() : nativeError_; }
private:
    bool winsockStarted_{};
    QString nativeError_;
};
} // namespace zzj::network::qt
