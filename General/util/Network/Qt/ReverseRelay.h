#pragma once
#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QSslConfiguration>
#include <QString>
#include <memory>

namespace zzj::network::qt {
struct RelayCounters {
    quint64 streamsOpened{};
    quint64 bytesToConnector{};
    quint64 bytesFromConnector{};
};
struct ReverseRelayServerSettings {
    QHostAddress ingressHost{QHostAddress::LocalHost};
    QHostAddress reverseHost{QHostAddress::LocalHost};
    quint16 ingressPort{};
    quint16 reversePort{};
    QSslConfiguration tls;
    QString peerId;
    // Raw 32-byte SHA-256 digest of the permitted connector's DER certificate.
    QByteArray peerCertificateSha256;
};
struct ReverseRelayClientSettings {
    QString host, peerName, peerId;
    quint16 port{};
    QSslConfiguration tls;
    // One fixed, numeric backend. Remote Open frames never contain a target.
    QHostAddress localTargetHost{QHostAddress::LocalHost};
    quint16 localTargetPort{};
};

// Opaque TCP byte relay. TLS carried by each ingress stream remains end-to-end;
// the independently authenticated reverse connection multiplexes these streams.
// All methods and callbacks must run on this QObject's event-loop thread.
class ReverseRelayServer final : public QObject {
    Q_OBJECT
  public:
    static constexpr int MaxStreams = 128;
    explicit ReverseRelayServer(QObject* parent = nullptr);
    ~ReverseRelayServer() override;
    bool start(const ReverseRelayServerSettings& settings, QString* error = nullptr);
    void stop();
    quint16 ingressPort() const;
    quint16 reversePort() const;
    bool ready() const;
    int activeStreamCount() const;
    RelayCounters counters() const;
  signals:
    void readyChanged(bool ready);
    void event(QString message);
    void failed(QString error);
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Dials the POP; no connector-side listener is required for the reverse link.
// While started, reconnects with a 250 ms to 8 s bounded exponential delay.
class ReverseRelayClient final : public QObject {
    Q_OBJECT
  public:
    explicit ReverseRelayClient(QObject* parent = nullptr);
    ~ReverseRelayClient() override;
    bool start(const ReverseRelayClientSettings& settings, QString* error = nullptr);
    void stop();
    bool ready() const;
    int activeStreamCount() const;
    RelayCounters counters() const;
  signals:
    void readyChanged(bool ready);
    void event(QString message);
    void failed(QString error);
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace zzj::network::qt
