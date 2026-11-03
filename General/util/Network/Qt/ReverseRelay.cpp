#include "ReverseRelay.h"
#include "FramedChannel.h"
#include "TcpHalfClose.h"
#ifdef _WIN32
#include "Windows/util/Network/Qt/NativeTcpListener.h"
#include "Windows/util/Network/Qt/NativeTcpStream.h"
#else
#include <QTcpServer>
#include <QTcpSocket>
#include <unistd.h>
#endif
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QNetworkProxy>
#include <QQueue>
#include <QSet>
#include <QSslCertificate>
#include <QSslError>
#include <QSslKey>
#include <QSslSocket>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <functional>
#include <limits>

namespace zzj::network::qt {
namespace {
constexpr quint16 ProtocolVersion = 1;
constexpr quint32 StreamWindow = 64 * 1024;
constexpr qint64 DataChunk = 16 * 1024;
constexpr qint64 ControlReserve = 64 * 1024;
constexpr int MaxControlFrames = 1024;
constexpr int MaxPendingPeers = 4;
constexpr int AuthenticationTimeoutMs = 10000;
constexpr int HeartbeatIntervalMs = 5000;
constexpr int HeartbeatTimeoutMs = 30000;
enum Frame : quint8 { Auth = 1, AuthOk, Open, OpenOk, Data, Fin, Reset, Window, Ping, Pong };

#ifdef _WIN32
using StreamTransport = NativeTcpStream;
using ListenerBase = NativeTcpListener;
void closeDescriptor(qintptr descriptor) { closesocket(static_cast<SOCKET>(descriptor)); }
bool adopt(StreamTransport* socket, qintptr descriptor) { return socket->adopt(descriptor); }
void closeWrite(StreamTransport* socket) { socket->flushWriteClose(); }
#else
using StreamTransport = QTcpSocket;
using ListenerBase = QTcpServer;
void closeDescriptor(qintptr descriptor) { ::close(static_cast<int>(descriptor)); }
bool adopt(StreamTransport* socket, qintptr descriptor) { return socket->setSocketDescriptor(descriptor); }
void closeWrite(StreamTransport* socket) {
    if (socket->bytesToWrite() == 0) {
        QString ignored;
        if (!shutdownWrite(socket, &ignored)) socket->abort();
    }
}
#endif

class DescriptorListener final : public ListenerBase {
  public:
    using ListenerBase::ListenerBase;
    std::function<void(qintptr)> accept;
    bool bind(const QHostAddress& host, quint16 port) {
#ifdef _WIN32
        return listenExclusive(host, port);
#else
        return listen(host, port);
#endif
    }
  protected:
    void incomingConnection(qintptr descriptor) override {
        if (accept) accept(descriptor);
        else closeDescriptor(descriptor);
    }
};

bool validPeerId(const QString& id) {
    const auto bytes = id.toUtf8();
    if (bytes.isEmpty() || bytes.size() > 255 || QString::fromUtf8(bytes) != id) return false;
    for (const auto ch : id)
        if (ch.isNull() || ch.isSpace() || ch.category() == QChar::Other_Control) return false;
    return true;
}
bool validHost(const QHostAddress& host) {
    return host.protocol() == QAbstractSocket::IPv4Protocol || host.protocol() == QAbstractSocket::IPv6Protocol;
}
bool validTls(const QSslConfiguration& tls) {
    return QSslSocket::supportsSsl() && !tls.localCertificate().isNull() &&
        !tls.privateKey().isNull() && !tls.caCertificates().isEmpty();
}
QByteArray authentication(const QString& peerId) {
    QByteArray result(2, Qt::Uninitialized);
    qToBigEndian<quint16>(ProtocolVersion, result.data());
    result += peerId.toUtf8();
    return result;
}
QByteArray creditPayload(quint32 amount) {
    QByteArray result(4, Qt::Uninitialized);
    qToBigEndian<quint32>(amount, result.data());
    return result;
}

struct Stream final : QObject {
    explicit Stream(quint64 number, QObject* parent) : QObject(parent), id(number), socket(new StreamTransport(this)) {
#ifndef _WIN32
        socket->setReadBufferSize(StreamWindow);
#endif
    }
    quint64 id;
    StreamTransport* socket;
    quint32 sendCredit{};
    quint32 receiveCredit{};
    quint32 pendingCredit{};
    quint32 receiveQueued{};
    bool opened{};
    bool localEof{};
    bool localFinSent{};
    bool remoteFin{};
};

// Shared state machine for both roles. All application data is opaque and only
// this connection's monotonically increasing stream IDs are routing metadata.
class Session final : public QObject {
  public:
    Session(bool server, const QSslConfiguration& configuration, const QString& peerId,
            RelayCounters* counters, QObject* parent)
        : QObject(parent), server_(server), tls_(new QSslSocket(this)), expectedAuth_(authentication(peerId)), counters_(counters) {
        tls_->setProxy(QNetworkProxy::NoProxy);
        auto requiredTls = configuration;
        requiredTls.setPeerVerifyMode(QSslSocket::VerifyPeer);
        requiredTls.setProtocol(QSsl::TlsV1_2OrLater);
        tls_->setSslConfiguration(requiredTls);
        tls_->setReadBufferSize(FramedChannel::MaxPayload + 13);
        clock_.start();
        authTimer_.setSingleShot(true);
        authTimer_.setInterval(AuthenticationTimeoutMs);
        connect(&authTimer_, &QTimer::timeout, this, [this] { finish(QStringLiteral("Reverse authentication timed out")); });
        heartbeat_.setInterval(HeartbeatIntervalMs);
        connect(&heartbeat_, &QTimer::timeout, this, [this] {
            if (clock_.elapsed() - lastReceiveMs_ > HeartbeatTimeoutMs) {
                finish(QStringLiteral("Reverse heartbeat timed out")); return;
            }
            queue(Ping, 0);
        });
        connect(tls_, &QSslSocket::encrypted, this, [this] { encrypted(); });
        connect(tls_, &QSslSocket::sslErrors, this, [this](const QList<QSslError>&) {
            finish(QStringLiteral("Reverse TLS certificate verification failed"));
        });
        connect(tls_, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            finish(QStringLiteral("Reverse TLS transport failed: %1").arg(tls_->errorString()));
        });
        connect(tls_, &QSslSocket::disconnected, this, [this] { finish(QStringLiteral("Reverse connection disconnected")); });
    }
    ~Session() override { shutdown(); }
    std::function<void(bool)> readyChanged;
    std::function<bool()> activate;
    std::function<void(QString)> event;
    std::function<void(QString)> ended;
    QByteArray certificatePin;
    QHostAddress targetHost;
    quint16 targetPort{};
    bool ready() const { return authenticated_ && !stopped_; }
    int streamCount() const { return streams_.size(); }
    bool acceptReverse(qintptr descriptor) {
        if (!tls_->setSocketDescriptor(descriptor)) {
            closeDescriptor(descriptor); return false;
        }
        authTimer_.start();
        tls_->startServerEncryption();
        return !stopped_;
    }
    void dial(const QString& host, quint16 port, const QString& peerName) {
        authTimer_.start();
        tls_->connectToHostEncrypted(host, port, peerName);
    }
    void shutdown() {
        if (stopped_) return;
        stopped_ = true;
        authTimer_.stop(); heartbeat_.stop();
        const auto streams = streams_.values();
        streams_.clear(); controls_.clear();
        for (auto* stream : streams) { stream->socket->abort(); stream->deleteLater(); }
        tls_->abort();
    }
    void acceptIngress(qintptr descriptor) {
        if (!ready() || streams_.size() >= ReverseRelayServer::MaxStreams ||
            lastStream_ == (std::numeric_limits<quint64>::max)()) {
            closeDescriptor(descriptor); return;
        }
        const auto id = ++lastStream_;
        auto* stream = makeStream(id);
        const QPointer<Session> self(this);
        const QPointer<Stream> guard(stream);
        // Publish every allocated ID, including failed local adoptions. This
        // preserves the exact consecutive Open sequence checked by the peer.
        if (!queue(Open, id)) { closeDescriptor(descriptor); return; }
        if (!adopt(stream->socket, descriptor)) {
            if (self && guard && streams_.contains(id)) retire(id, true);
            return;
        }
        if (!self || !guard || !streams_.contains(id)) return;
        ++counters_->streamsOpened;
        if (self && event) event(QStringLiteral("Ingress stream %1 opened").arg(id));
    }
  private:
    struct Control { quint8 type; quint64 id; QByteArray payload; };
    Stream* makeStream(quint64 id) {
        auto* stream = new Stream(id, this);
        streams_.insert(id, stream);
        QTimer::singleShot(AuthenticationTimeoutMs, stream, [this, id] {
            if (auto* s = streams_.value(id); s && !s->opened) retire(id, true);
        });
        connect(stream->socket, &StreamTransport::readyRead, this, [this] { schedulePump(); });
        connect(stream->socket, &StreamTransport::readChannelFinished, this, [this, id] {
            if (auto* s = streams_.value(id)) { s->localEof = true; schedulePump(); }
        });
        connect(stream->socket, &StreamTransport::bytesWritten, this, [this, id](qint64 count) {
            auto* s = streams_.value(id);
            if (!s || stopped_) return;
            if (count < 0 || static_cast<quint64>(count) > s->receiveQueued) {
                finish(QStringLiteral("Invalid backend write accounting")); return;
            }
            s->receiveQueued -= static_cast<quint32>(count);
            s->pendingCredit += static_cast<quint32>(count);
#ifndef _WIN32
            if (s->remoteFin && s->socket->bytesToWrite() == 0) closeWrite(s->socket);
#endif
            schedulePump();
        });
        connect(stream->socket, &StreamTransport::disconnected, this, [this, id] {
            auto* s = streams_.value(id);
            if (s) retire(id, !(s->localFinSent && s->remoteFin && s->receiveQueued == 0));
        });
#ifdef _WIN32
        connect(stream->socket, &StreamTransport::errorOccurred, this, [this, id](const QString&) { retire(id, true); });
#else
        connect(stream->socket, &QTcpSocket::errorOccurred, this, [this, id](QAbstractSocket::SocketError error) {
            if (error != QAbstractSocket::RemoteHostClosedError) retire(id, true);
        });
#endif
        return stream;
    }
    void encrypted() {
        if (stopped_) return;
        if (tls_->peerCertificate().isNull() || (server_ &&
            tls_->peerCertificate().digest(QCryptographicHash::Sha256) != certificatePin)) {
            finish(QStringLiteral("Reverse peer certificate does not match the permitted connector")); return;
        }
        channel_ = new FramedChannel(tls_, this);
        connect(channel_, &FramedChannel::frameReceived, this, [this](quint8 type, quint64 id, const QByteArray& payload) {
            receive(type, id, payload);
        });
        connect(channel_, &FramedChannel::protocolError, this, [this](const QString& message) { finish(message); });
        connect(channel_, &FramedChannel::writeAvailable, this, [this] { schedulePump(); });
        if (!server_) queue(Auth, 0, expectedAuth_);
    }
    void announceReady() {
        authenticated_ = true;
        const QPointer<Session> self(this);
        // The server publishes admission before its public authentication event,
        // so event observers see the actual active peer through ready().
        if (activate && !activate()) {
            if (self && !stopped_) finish(QStringLiteral("Reverse peer admission declined"));
            return;
        }
        if (!self || !ready()) return;
        authTimer_.stop();
        lastReceiveMs_ = clock_.elapsed();
        heartbeat_.start();
        schedulePump();
        if (event) event(QStringLiteral("Reverse peer authenticated"));
        // A direct event receiver may stop/restart the owner. deleteLater leaves
        // this QPointer live, so lifetime alone does not imply readiness.
        if (self && ready() && readyChanged) readyChanged(true);
    }
    void finish(const QString& reason) {
        if (stopped_) return;
        const bool wasReady = authenticated_;
        shutdown();
        const QPointer<Session> self(this);
        if (wasReady && readyChanged) readyChanged(false);
        if (!self) return;
        if (event) event(QStringLiteral("Reverse link closed: %1").arg(reason));
        if (self && ended) ended(reason);
    }
    bool queue(quint8 type, quint64 id, const QByteArray& payload = {}) {
        if (stopped_) return false;
        if (controls_.size() >= MaxControlFrames) {
            finish(QStringLiteral("Reverse control queue limit exceeded")); return false;
        }
        controls_.enqueue({type, id, payload});
        schedulePump();
        return true;
    }
    void schedulePump() {
        if (stopped_ || pumpScheduled_) return;
        pumpScheduled_ = true;
        QTimer::singleShot(0, this, [this] { pumpScheduled_ = false; pump(); });
    }
    bool send(quint8 type, quint64 id, const QByteArray& payload = {}) {
        if (stopped_ || !channel_) return false;
        const QPointer<Session> self(this);
        if (!channel_->send(type, id, payload)) {
            if (self) finish(QStringLiteral("Reverse frame write failed"));
            return false;
        }
        return self && !stopped_;
    }
    void pump() {
        if (stopped_ || !channel_) return;
        while (!controls_.isEmpty()) {
            if (channel_->availablePayloadBytes() < qMax<qint64>(1, controls_.head().payload.size())) return;
            const auto frame = controls_.dequeue();
            if (!send(frame.type, frame.id, frame.payload)) return;
        }
        if (!authenticated_) return;
        // IDs form an ordered snapshot so one busy stream cannot continuously
        // consume every writable byte ahead of an already waiting sibling.
        auto ids = streams_.keys();
        std::sort(ids.begin(), ids.end());
        if (!ids.isEmpty()) std::rotate(ids.begin(), ids.begin() + roundRobin_ % ids.size(), ids.end());
        ++roundRobin_;
        bool moreData{};
        for (const auto id : ids) {
            auto* stream = streams_.value(id);
            if (!stream || !stream->opened) continue;
            if (stream->pendingCredit && channel_->availablePayloadBytes() >= 4) {
                const auto amount = stream->pendingCredit;
                stream->pendingCredit = 0;
                stream->receiveCredit += amount;
                if (!send(Window, id, creditPayload(amount))) return;
            }
            const auto room = channel_->availablePayloadBytes() - ControlReserve;
            const auto count = (std::min)({DataChunk, room, static_cast<qint64>(stream->sendCredit), stream->socket->bytesAvailable()});
            if (count > 0) {
                const QPointer<Stream> guard(stream);
                const auto bytes = stream->socket->read(count);
                if (!guard || !streams_.contains(id)) continue;
                // All room/credit checks happen before read. A rejected write
                // terminates the session; consumed bytes are never silently retried.
                if (bytes.isEmpty() || bytes.size() > count) { finish(QStringLiteral("Invalid backend read")); return; }
                stream->sendCredit -= static_cast<quint32>(bytes.size());
                if (!send(Data, id, bytes)) return;
                if (server_) counters_->bytesToConnector += bytes.size();
                else counters_->bytesFromConnector += bytes.size();
                moreData = true;
            }
            if (stream->localEof && !stream->localFinSent && stream->socket->bytesAvailable() == 0) {
                stream->localFinSent = true;
                if (!queue(Fin, id)) return;
            }
        }
        if (moreData || !controls_.isEmpty()) schedulePump();
    }
    void retire(quint64 id, bool reset) {
        auto* stream = streams_.take(id);
        if (!stream) return;
        const auto queued = stream->receiveQueued;
        stream->socket->abort();
        stream->deleteLater();
        const QPointer<Session> self(this);
        if (reset && !stopped_) queue(Reset, id);
        if (self && !stopped_ && event)
            event(QStringLiteral("Stream %1 closed%2; pending backend bytes=%3").arg(id).arg(reset ? QStringLiteral(" with reset") : QString()).arg(queued));
    }
    void receive(quint8 type, quint64 id, const QByteArray& payload) {
        if (stopped_) return;
        lastReceiveMs_ = clock_.elapsed();
        if (!authenticated_) {
            if (id != 0 || (server_ ? type != Auth || payload != expectedAuth_ : type != AuthOk || !payload.isEmpty())) {
                finish(QStringLiteral("Invalid reverse authentication/version/peer identity")); return;
            }
            if (server_ && !queue(AuthOk, 0)) return;
            announceReady();
            return;
        }
        if ((type == Ping || type == Pong) && id == 0 && payload.isEmpty()) {
            if (type == Ping) queue(Pong, 0);
            return;
        }
        if (id == 0 || type < Open || type > Window ||
            ((type == Open || type == OpenOk || type == Fin || type == Reset) && !payload.isEmpty()) ||
            (type == Data && (payload.isEmpty() || payload.size() > DataChunk)) ||
            (type == Window && (payload.size() != 4 || qFromBigEndian<quint32>(payload.constData()) == 0 ||
                                qFromBigEndian<quint32>(payload.constData()) > StreamWindow))) {
            finish(QStringLiteral("Invalid reverse frame")); return;
        }
        if (type == Open) {
            if (server_ || lastStream_ == (std::numeric_limits<quint64>::max)() || id != lastStream_ + 1) {
                finish(QStringLiteral("Invalid/reused reverse stream ID")); return;
            }
            lastStream_ = id;
            if (streams_.size() >= ReverseRelayServer::MaxStreams) { queue(Reset, id); return; }
            auto* stream = makeStream(id);
            connect(stream->socket, &StreamTransport::connected, this, [this, id] {
                if (auto* s = streams_.value(id)) {
                    s->opened = true;
                    s->pendingCredit = StreamWindow;
                    ++counters_->streamsOpened;
                    if (!queue(OpenOk, id)) return;
                    schedulePump();
                }
            });
            stream->socket->connectToHost(targetHost, targetPort);
            return;
        }
        auto* stream = streams_.value(id);
        if (!stream) {
            // Reset and FIN may cross already queued Data/Window in opposite
            // directions. IDs never repeat, so closed past IDs are safe tombstones.
            if (id <= lastStream_) return;
            finish(QStringLiteral("Reverse frame refers to an unknown future stream")); return;
        }
        if (type == Reset) { retire(id, false); return; }
        if (type == OpenOk) {
            if (!server_ || stream->opened) { finish(QStringLiteral("Unexpected reverse OpenOk")); return; }
            stream->opened = true;
            stream->pendingCredit = StreamWindow;
            schedulePump(); return;
        }
        if (!stream->opened) { finish(QStringLiteral("Reverse data before backend OpenOk")); return; }
        if (type == Window) {
            const auto amount = qFromBigEndian<quint32>(payload.constData());
            if (amount > StreamWindow - stream->sendCredit) { finish(QStringLiteral("Reverse send credit exceeded")); return; }
            stream->sendCredit += amount;
            schedulePump(); return;
        }
        if (type == Fin) {
            if (stream->remoteFin) { finish(QStringLiteral("Duplicate reverse FIN")); return; }
            stream->remoteFin = true;
            closeWrite(stream->socket);
            return;
        }
        if (stream->remoteFin || static_cast<quint32>(payload.size()) > stream->receiveCredit) {
            finish(QStringLiteral("Reverse receive credit exceeded or Data after FIN")); return;
        }
        stream->receiveCredit -= static_cast<quint32>(payload.size());
        stream->receiveQueued += static_cast<quint32>(payload.size());
        if (server_) counters_->bytesFromConnector += payload.size();
        else counters_->bytesToConnector += payload.size();
        const QPointer<Session> self(this);
        const QPointer<Stream> guard(stream);
        const auto written = stream->socket->write(payload);
        if (self && guard && streams_.contains(id) && written != payload.size()) retire(id, true);
    }
    bool server_{};
    QSslSocket* tls_;
    QPointer<FramedChannel> channel_;
    QByteArray expectedAuth_;
    RelayCounters* counters_;
    QHash<quint64, Stream*> streams_;
    QQueue<Control> controls_;
    QElapsedTimer clock_;
    QTimer authTimer_, heartbeat_;
    qint64 lastReceiveMs_{};
    quint64 lastStream_{};
    quint64 roundRobin_{};
    bool authenticated_{}, stopped_{}, pumpScheduled_{};
};
} // namespace

struct ReverseRelayServer::Impl {
    explicit Impl(ReverseRelayServer* owner) : owner(owner), ingress(new DescriptorListener(owner)), reverse(new DescriptorListener(owner)) {
        ingress->accept = [this](qintptr descriptor) {
            if (session && session->ready()) session->acceptIngress(descriptor);
            else closeDescriptor(descriptor);
        };
        reverse->accept = [this](qintptr descriptor) {
            if (!running || session || pending.size() >= MaxPendingPeers) {
                closeDescriptor(descriptor);
                emit this->owner->event(QStringLiteral("Rejected reverse peer: active peer or pending authentication limit"));
                return;
            }
            auto* candidate = new Session(true, settings.tls, settings.peerId, &counters, this->owner);
            pending.insert(candidate);
            candidate->certificatePin = settings.peerCertificateSha256;
            const QPointer<ReverseRelayServer> guard(this->owner);
            candidate->activate = [this, guard, candidate] {
                if (!guard) return false;
                if (!running || !pending.contains(candidate) || session) {
                    return false;
                }
                pending.remove(candidate);
                session = candidate;
                // Authentication won the single active slot. Pending peers are
                // unpublished before shutdown, which suppresses their callbacks.
                const auto others = pending.values();
                pending.clear();
                for (auto* other : others) { other->shutdown(); other->deleteLater(); }
                return true;
            };
            candidate->readyChanged = [this, guard, candidate](bool ready) {
                if (guard && session == candidate) emit guard->readyChanged(ready);
            };
            candidate->event = [guard](const QString& message) { if (guard) emit guard->event(message); };
            candidate->ended = [this, guard, candidate](const QString& message) {
                if (!guard) return;
                if (session == candidate) session = nullptr;
                pending.remove(candidate);
                candidate->deleteLater();
                emit guard->failed(message);
            };
            const bool accepted = candidate->acceptReverse(descriptor);
            if (guard && !accepted && pending.remove(candidate)) {
                candidate->shutdown(); candidate->deleteLater();
            }
        };
    }
    ReverseRelayServer* owner;
    DescriptorListener *ingress, *reverse;
    ReverseRelayServerSettings settings;
    RelayCounters counters;
    QPointer<Session> session;
    QSet<Session*> pending;
    bool running{};
};

ReverseRelayServer::ReverseRelayServer(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>(this)) {}
ReverseRelayServer::~ReverseRelayServer() { blockSignals(true); stop(); }
bool ReverseRelayServer::start(const ReverseRelayServerSettings& settings, QString* error) {
    const QPointer<ReverseRelayServer> self(this);
    stop();
    if (!self) return false;
    if (!validHost(settings.ingressHost) || !validHost(settings.reverseHost) || !validPeerId(settings.peerId) ||
        settings.peerCertificateSha256.size() != 32 || !validTls(settings.tls)) {
        if (error) *error = QStringLiteral("Invalid reverse server hosts, peer identity/pin, or mutual TLS configuration");
        return false;
    }
    impl_->settings = settings;
    impl_->counters = {};
    if (!impl_->reverse->bind(settings.reverseHost, settings.reversePort) || !impl_->ingress->bind(settings.ingressHost, settings.ingressPort)) {
        if (error) *error = impl_->reverse->isListening() ? impl_->ingress->errorString() : impl_->reverse->errorString();
        impl_->reverse->close(); impl_->ingress->close(); return false;
    }
    impl_->running = true;
    emit event(QStringLiteral("Reverse listeners started ingress=%1 reverse=%2").arg(ingressPort()).arg(reversePort()));
    return true;
}
void ReverseRelayServer::stop() {
    const bool wasReady = ready();
    impl_->running = false;
    impl_->ingress->close(); impl_->reverse->close();
    const auto pending = impl_->pending.values();
    impl_->pending.clear();
    if (auto* session = impl_->session.data()) {
        impl_->session = nullptr;
        session->shutdown(); session->deleteLater();
    }
    for (auto* candidate : pending) { candidate->shutdown(); candidate->deleteLater(); }
    if (wasReady) emit readyChanged(false);
}
quint16 ReverseRelayServer::ingressPort() const { return impl_->ingress->serverPort(); }
quint16 ReverseRelayServer::reversePort() const { return impl_->reverse->serverPort(); }
bool ReverseRelayServer::ready() const { return impl_->running && impl_->session && impl_->session->ready(); }
int ReverseRelayServer::activeStreamCount() const { return impl_->session ? impl_->session->streamCount() : 0; }
RelayCounters ReverseRelayServer::counters() const { return impl_->counters; }

struct ReverseRelayClient::Impl {
    explicit Impl(ReverseRelayClient* owner) : owner(owner), reconnect(new QTimer(owner)) {
        reconnect->setSingleShot(true);
        QObject::connect(reconnect, &QTimer::timeout, owner, [this] { dial(); });
    }
    void dial() {
        if (!running || session) return;
        auto* candidate = new Session(false, settings.tls, settings.peerId, &counters, owner);
        session = candidate;
        candidate->targetHost = settings.localTargetHost;
        candidate->targetPort = settings.localTargetPort;
        const QPointer<ReverseRelayClient> guard(owner);
        candidate->readyChanged = [this, guard](bool ready) {
            if (!guard) return;
            if (ready) reconnectDelayMs = 250;
            emit guard->readyChanged(ready);
        };
        candidate->event = [guard](const QString& message) { if (guard) emit guard->event(message); };
        candidate->ended = [this, guard, candidate](const QString& message) {
            if (!guard) return;
            if (session == candidate) session = nullptr;
            candidate->deleteLater();
            if (running) {
                reconnect->start(reconnectDelayMs);
                reconnectDelayMs = (std::min)(8000, reconnectDelayMs * 2);
            }
            emit guard->failed(message);
        };
        candidate->dial(settings.host, settings.port, settings.peerName);
    }
    ReverseRelayClient* owner;
    QTimer* reconnect;
    ReverseRelayClientSettings settings;
    RelayCounters counters;
    QPointer<Session> session;
    int reconnectDelayMs{250};
    bool running{};
};
ReverseRelayClient::ReverseRelayClient(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>(this)) {}
ReverseRelayClient::~ReverseRelayClient() { blockSignals(true); stop(); }
bool ReverseRelayClient::start(const ReverseRelayClientSettings& settings, QString* error) {
    const QPointer<ReverseRelayClient> self(this);
    stop();
    if (!self) return false;
    if (settings.host.isEmpty() || settings.peerName.isEmpty() || !settings.port || !validPeerId(settings.peerId) ||
        !validHost(settings.localTargetHost) || !settings.localTargetPort || !validTls(settings.tls)) {
        if (error) *error = QStringLiteral("Invalid reverse client POP host/name, peer identity, fixed backend, or mutual TLS configuration");
        return false;
    }
    impl_->settings = settings;
    impl_->counters = {};
    impl_->reconnectDelayMs = 250;
    impl_->running = true;
    impl_->reconnect->start(0);
    return true;
}
void ReverseRelayClient::stop() {
    const bool wasReady = ready();
    impl_->running = false;
    impl_->reconnect->stop();
    if (auto* session = impl_->session.data()) {
        impl_->session = nullptr;
        session->shutdown(); session->deleteLater();
    }
    if (wasReady) emit readyChanged(false);
}
bool ReverseRelayClient::ready() const { return impl_->running && impl_->session && impl_->session->ready(); }
int ReverseRelayClient::activeStreamCount() const { return impl_->session ? impl_->session->streamCount() : 0; }
RelayCounters ReverseRelayClient::counters() const { return impl_->counters; }
} // namespace zzj::network::qt
