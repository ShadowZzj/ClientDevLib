#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <QObject>
#include <QByteArray>
#include <QHostAddress>
#include <QString>

class QWinEventNotifier;

namespace zzj::network::qt {
// Native Windows event-driven TCP stream. FIN affects the receive half only;
// queued writes and the send half remain usable until explicitly closed.
class NativeTcpStream final : public QObject {
    Q_OBJECT
public:
    static constexpr qint64 MaxWriteQueue = 1024 * 1024;
    static constexpr qint64 MaxReadQueue = 64 * 1024;
    explicit NativeTcpStream(QObject* parent = nullptr);
    ~NativeTcpStream() override;
    // Consumes the accepted SOCKET even if event setup fails; never detach a QTcpSocket.
    bool adopt(qintptr nativeConnectedSocket);
    void connectToHost(const QHostAddress& numeric, quint16 port);
    void setRedirectRecords(const QByteArray& records);
    qint64 write(const QByteArray& data);
    QByteArray read(qint64 maximum = 65536);
    qint64 bytesAvailable() const { return input_.size(); }
    qint64 bytesToWrite() const { return output_.size(); }
    void flushWriteClose();
    void abort();
    QString errorString() const { return error_; }
    qintptr socketDescriptor() const { return socket_ == INVALID_SOCKET ? -1 : static_cast<qintptr>(socket_); }
    QHostAddress peerAddress() const { return peerAddress_; }
    quint16 peerPort() const { return peerPort_; }
signals:
    void connected();
    void readyRead();
    void bytesWritten(qint64 count);
    void readChannelFinished();
    void disconnected();
    void errorOccurred(QString error);
private:
    bool attach(SOCKET socket, bool connecting);
    void onNetworkEvent();
    void drainRead();
    void flushWrites();
    void finishIfComplete();
    void scheduleRead();
    void fail(const QString& operation, int code);
    void closeNative(bool notify);
    void resetBuffers();
    void updatePeer();
    SOCKET socket_{INVALID_SOCKET};
    WSAEVENT event_{WSA_INVALID_EVENT};
    QWinEventNotifier* notifier_{};
    QByteArray input_;
    QByteArray output_;
    QByteArray redirectRecords_;
    QHostAddress peerAddress_;
    quint16 peerPort_{};
    QString error_;
    bool winsockStarted_{};
    bool connecting_{};
    bool connected_{};
    bool peerFin_{};
    bool closeIndicated_{};
    bool writeCloseRequested_{};
    bool writeFin_{};
    bool writing_{};
    bool readScheduled_{};
    bool closeScheduled_{};
    quint64 epoch_{};
};
} // namespace zzj::network::qt
