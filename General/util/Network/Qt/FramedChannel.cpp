#include "FramedChannel.h"
#include <QSslSocket>
#include <QtEndian>
#include <algorithm>

namespace zzj::network::qt {
FramedChannel::FramedChannel(QIODevice* transport, QObject* parent) : QObject(parent), transport_(transport) {
    connect(transport_, &QIODevice::readyRead, this, &FramedChannel::consume);
    connect(transport_, &QIODevice::bytesWritten, this, [this](qint64) { emit writeAvailable(); });
    if (auto* tls = qobject_cast<QSslSocket*>(transport_.data()))
        connect(tls, &QSslSocket::encryptedBytesWritten, this, [this](qint64) { emit writeAvailable(); });
}
qint64 FramedChannel::queuedBytes() const {
    if (!transport_)
        return 0;
    const auto* tls = qobject_cast<QSslSocket*>(transport_.data());
    return transport_->bytesToWrite() + (tls ? tls->encryptedBytesToWrite() : 0);
}
qint64 FramedChannel::availablePayloadBytes() const {
    if (failed_ || !transport_ || !transport_->isWritable())
        return 0;
    const qint64 overhead = qobject_cast<QSslSocket*>(transport_.data()) ? TlsWriteReserveBytes : 0;
    return qMax<qint64>(0, MaxQueuedBytes - queuedBytes() - overhead - 13);
}
bool FramedChannel::send(quint8 type, quint64 id, const QByteArray& payload) {
    if (failed_ || !transport_ || !transport_->isWritable() || payload.size() > MaxPayload)
        return false;
    const qint64 length = 13 + payload.size();
    if (payload.size() > availablePayloadBytes())
        return false;
    QByteArray frame(length, Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(9 + payload.size()), frame.data());
    frame[4] = static_cast<char>(type);
    qToBigEndian<quint64>(id, frame.data() + 5);
    std::copy(payload.cbegin(), payload.cend(), frame.begin() + 13);
    const QPointer<FramedChannel> self(this);
    const auto written = transport_->write(frame);
    if (!self)
        return false;
    if (written != length) {
        fail("Transport write failed");
        return false;
    }
    return true;
}
void FramedChannel::fail(const QString& error) {
    if (failed_)
        return;
    failed_ = true;
    buffer_.clear();
    const QPointer<FramedChannel> self(this);
    if (transport_)
        transport_->close();
    if (self)
        emit protocolError(error);
}
void FramedChannel::consume() {
    while (!failed_ && transport_) {
        const qsizetype room = MaxPayload + 13 - buffer_.size();
        if (room > 0 && transport_->bytesAvailable() > 0)
            buffer_ += transport_->read(qMin<qint64>(room, 64 * 1024));
        bool progressed = false;
        while (!failed_ && buffer_.size() >= 4) {
            const auto length = qFromBigEndian<quint32>(buffer_.constData());
            if (length < 9 || length > MaxPayload + 9) {
                fail("Invalid message length");
                return;
            }
            if (buffer_.size() < static_cast<qsizetype>(length) + 4)
                break;
            const quint8 type = static_cast<quint8>(buffer_[4]);
            const quint64 id = qFromBigEndian<quint64>(buffer_.constData() + 5);
            const auto payload = buffer_.mid(13, length - 9);
            buffer_.remove(0, length + 4);
            progressed = true;
            const QPointer<FramedChannel> self(this);
            emit frameReceived(type, id, payload);
            if (!self || !transport_)
                return;
        }
        if (failed_ || transport_->bytesAvailable() == 0)
            return;
        if (!progressed && buffer_.size() >= MaxPayload + 13) {
            fail("Receive buffer limit exceeded");
            return;
        }
    }
}
} // namespace zzj::network::qt
