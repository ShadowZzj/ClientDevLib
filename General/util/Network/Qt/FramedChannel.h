#pragma once
#include <QByteArray>
#include <QIODevice>
#include <QObject>
#include <QPointer>

namespace zzj::network::qt {
// Bounded message envelope over any sequential QIODevice. No product message types.
class FramedChannel : public QObject {
    Q_OBJECT
  public:
    static constexpr qsizetype MaxPayload = 1024 * 1024;
    static constexpr qint64 MaxQueuedBytes = 4 * 1024 * 1024;
    static constexpr qint64 TlsWriteReserveBytes = 64 * 1024;
    explicit FramedChannel(QIODevice* transport, QObject* parent = nullptr);
    bool send(quint8 type, quint64 channelId, const QByteArray& payload = {});
    qint64 queuedBytes() const;
    qint64 availablePayloadBytes() const;
  signals:
    void frameReceived(quint8 type, quint64 channelId, QByteArray payload);
    void protocolError(QString error);
    void writeAvailable();

  private:
    void consume();
    void fail(const QString& error);
    QPointer<QIODevice> transport_;
    QByteArray buffer_;
    bool failed_{};
};
} // namespace zzj::network::qt
