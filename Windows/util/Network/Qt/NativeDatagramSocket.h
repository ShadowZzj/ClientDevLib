#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <QObject>
#include <QHostAddress>
#include <QString>

class QWinEventNotifier;

namespace zzj::network::qt {
// A native UDP event source. Consumers must use recv/WSARecvMsg and send/WSASendMsg;
// no QUdpSocket datagram-notification state is involved.
class NativeDatagramSocket final : public QObject {
    Q_OBJECT
public:
    explicit NativeDatagramSocket(QObject* parent = nullptr);
    ~NativeDatagramSocket() override;
    bool bindExclusive(const QHostAddress& bindAddress, quint16 port);
    qintptr socketDescriptor() const { return socket_ == INVALID_SOCKET ? -1 : static_cast<qintptr>(socket_); }
    quint16 localPort() const { return port_; }
    void close();
    QString errorString() const { return error_; }
signals:
    void readyRead();
    void writeAvailable();
    void errorOccurred(QString error);
private:
    void activate();
    void fail(const QString& operation, int error);
    SOCKET socket_{INVALID_SOCKET};
    WSAEVENT event_{WSA_INVALID_EVENT};
    QWinEventNotifier* notifier_{};
    QString error_;
    quint16 port_{};
    bool winsockStarted_{};
    quint64 epoch_{};
};
} // namespace zzj::network::qt
