#include "NativeTcpStream.h"
#include "../Wfp/RedirectMetadata.h"
#include <QWinEventNotifier>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <cstring>

namespace zzj::network::qt {
NativeTcpStream::NativeTcpStream(QObject* parent) : QObject(parent) {
    WSADATA data{};
    const int error = WSAStartup(MAKEWORD(2, 2), &data);
    winsockStarted_ = error == 0;
    if (error) error_ = QStringLiteral("Winsock initialization failed: %1").arg(error);
}
NativeTcpStream::~NativeTcpStream() {
    closeNative(false);
    if (winsockStarted_) WSACleanup();
}
void NativeTcpStream::resetBuffers() {
    input_.clear(); output_.clear(); error_.clear();
    peerAddress_.clear(); peerPort_ = 0;
    connecting_ = connected_ = peerFin_ = closeIndicated_ = writeCloseRequested_ = writeFin_ = writing_ = readScheduled_ = false;
    closeScheduled_ = false;
    ++epoch_;
}
bool NativeTcpStream::attach(SOCKET socket, bool connecting) {
    socket_ = socket;
    event_ = WSACreateEvent();
    if (event_ == WSA_INVALID_EVENT) { fail(QStringLiteral("Create socket event"), WSAGetLastError()); return false; }
    if (WSAEventSelect(socket_, event_, FD_READ | FD_WRITE | FD_CONNECT | FD_CLOSE) == SOCKET_ERROR) {
        fail(QStringLiteral("Select socket events"), WSAGetLastError()); return false;
    }
    connecting_ = connecting;
    connected_ = !connecting;
    notifier_ = new QWinEventNotifier(event_, this);
    connect(notifier_, &QWinEventNotifier::activated, this, [this](HANDLE) { onNetworkEvent(); });
    return true;
}
bool NativeTcpStream::adopt(qintptr descriptor) {
    closeNative(false);
    resetBuffers();
    const SOCKET native = static_cast<SOCKET>(descriptor);
    if (native == INVALID_SOCKET || !winsockStarted_) {
        if (native != INVALID_SOCKET) closesocket(native);
        error_ = QStringLiteral("Invalid accepted TCP socket"); return false;
    }
    if (!attach(native, false)) return false;
    updatePeer();
    const QPointer<NativeTcpStream> self(this);
    emit connected();
    if (self && socket_ != INVALID_SOCKET) scheduleRead();
    return true;
}
void NativeTcpStream::connectToHost(const QHostAddress& address, quint16 port) {
    closeNative(false);
    resetBuffers();
    if (!winsockStarted_ || address.isNull() || port == 0 ||
        (address.protocol() != QAbstractSocket::IPv4Protocol && address.protocol() != QAbstractSocket::IPv6Protocol)) {
        fail(QStringLiteral("Invalid numeric TCP target"), WSAEINVAL); return;
    }
    const int family = address.protocol() == QAbstractSocket::IPv4Protocol ? AF_INET : AF_INET6;
    SOCKET native = WSASocketW(family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (native == INVALID_SOCKET) { fail(QStringLiteral("Create TCP socket"), WSAGetLastError()); return; }
    if (!attach(native, true)) return;
    if (!redirectRecords_.isEmpty()) {
        const auto* begin = reinterpret_cast<const uint8_t*>(redirectRecords_.constData());
        std::vector<uint8_t> records(begin, begin + redirectRecords_.size());
        const int error = wfp::RedirectMetadata::ApplyRecords(socket_, records);
        if (error) { fail(QStringLiteral("Apply WFP TCP redirect records"), error); return; }
    }
    int result{};
    if (family == AF_INET) {
        sockaddr_in destination{}; destination.sin_family = AF_INET; destination.sin_port = htons(port);
        destination.sin_addr.s_addr = htonl(address.toIPv4Address());
        result = ::connect(socket_, reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
    } else {
        sockaddr_in6 destination{}; destination.sin6_family = AF_INET6; destination.sin6_port = htons(port);
        const auto bytes = address.toIPv6Address();
        std::memcpy(&destination.sin6_addr, bytes.c, 16);
        bool validScope{};
        destination.sin6_scope_id = address.scopeId().toUInt(&validScope);
        if (!address.scopeId().isEmpty() && !validScope) { fail(QStringLiteral("IPv6 scope must be numeric"), WSAEINVAL); return; }
        result = ::connect(socket_, reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
    }
    if (result == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS) fail(QStringLiteral("Connect TCP"), error);
    } else {
        connecting_ = false; connected_ = true;
        updatePeer();
        const QPointer<NativeTcpStream> self(this);
        emit connected();
        if (self) { flushWrites(); if (self) scheduleRead(); }
    }
}
void NativeTcpStream::setRedirectRecords(const QByteArray& records) {
    if (socket_ != INVALID_SOCKET || records.size() > 65536) {
        fail(QStringLiteral("Redirect records must be configured before connect and fit 64 KiB"), WSAEINVAL);
        return;
    }
    redirectRecords_ = records;
}
qint64 NativeTcpStream::write(const QByteArray& data) {
    if (socket_ == INVALID_SOCKET || (!connecting_ && !connected_) || writeCloseRequested_ || writeFin_) {
        error_ = QStringLiteral("TCP send half is closed"); return -1;
    }
    if (data.size() > MaxWriteQueue - output_.size()) { fail(QStringLiteral("TCP write queue limit exceeded"), WSAENOBUFS); return -1; }
    const qint64 size = data.size();
    output_.append(data);
    const QPointer<NativeTcpStream> self(this);
    if (connected_) flushWrites();
    return self && socket_ != INVALID_SOCKET ? size : -1;
}
QByteArray NativeTcpStream::read(qint64 maximum) {
    if (maximum <= 0) return {};
    const qsizetype count = static_cast<qsizetype>((std::min)(maximum, static_cast<qint64>(input_.size())));
    QByteArray bytes = input_.left(count);
    input_.remove(0, count);
    const QPointer<NativeTcpStream> self(this);
    finishIfComplete();
    if (self && !peerFin_) scheduleRead();
    return bytes;
}
void NativeTcpStream::flushWriteClose() {
    if (socket_ == INVALID_SOCKET) return;
    writeCloseRequested_ = true;
    if (connected_) flushWrites();
}
void NativeTcpStream::abort() { closeNative(true); }
void NativeTcpStream::closeNative(bool notify) {
    const bool hadSocket = socket_ != INVALID_SOCKET;
    ++epoch_;
    if (notifier_) { notifier_->setEnabled(false); notifier_->deleteLater(); notifier_ = nullptr; }
    if (socket_ != INVALID_SOCKET) { WSAEventSelect(socket_, nullptr, 0); closesocket(socket_); socket_ = INVALID_SOCKET; }
    if (event_ != WSA_INVALID_EVENT) { WSACloseEvent(event_); event_ = WSA_INVALID_EVENT; }
    connecting_ = connected_ = false;
    output_.clear(); writing_ = readScheduled_ = closeScheduled_ = false;
    if (notify && hadSocket) emit disconnected();
}
void NativeTcpStream::fail(const QString& operation, int code) {
    error_ = QStringLiteral("%1 failed: %2").arg(operation).arg(code);
    const QString error = error_;
    const bool hadSocket = socket_ != INVALID_SOCKET;
    const QPointer<NativeTcpStream> self(this);
    closeNative(false);
    emit errorOccurred(error);
    if (self && hadSocket) emit disconnected();
}
void NativeTcpStream::onNetworkEvent() {
    if (socket_ == INVALID_SOCKET || !notifier_) return;
    const quint64 epoch = epoch_;
    notifier_->setEnabled(false);
    WSANETWORKEVENTS events{};
    if (WSAEnumNetworkEvents(socket_, event_, &events) == SOCKET_ERROR) { fail(QStringLiteral("Read socket events"), WSAGetLastError()); return; }
    const QPointer<NativeTcpStream> self(this);
    if (events.lNetworkEvents & FD_CONNECT) {
        const int error = events.iErrorCode[FD_CONNECT_BIT];
        if (error) { fail(QStringLiteral("Connect TCP"), error); return; }
        if (connecting_) {
            connecting_ = false; connected_ = true; updatePeer(); emit connected();
            if (!self || epoch != epoch_ || socket_ == INVALID_SOCKET) return;
        }
    }
    if (events.lNetworkEvents & FD_CLOSE) {
        const int error = events.iErrorCode[FD_CLOSE_BIT];
        if (error) { fail(QStringLiteral("TCP peer closed with error"), error); return; }
        closeIndicated_ = true;
    }
    if (events.lNetworkEvents & (FD_READ | FD_CLOSE)) {
        if (events.iErrorCode[FD_READ_BIT]) { fail(QStringLiteral("TCP read event"), events.iErrorCode[FD_READ_BIT]); return; }
        drainRead();
        if (!self || epoch != epoch_ || socket_ == INVALID_SOCKET) return;
    }
    if (events.lNetworkEvents & (FD_WRITE | FD_CONNECT)) {
        if (events.iErrorCode[FD_WRITE_BIT]) { fail(QStringLiteral("TCP write event"), events.iErrorCode[FD_WRITE_BIT]); return; }
        flushWrites();
        if (!self || epoch != epoch_ || socket_ == INVALID_SOCKET) return;
    }
    notifier_->setEnabled(true);
}
void NativeTcpStream::updatePeer() {
    sockaddr_storage address{};
    int size = sizeof(address);
    if (socket_ == INVALID_SOCKET || getpeername(socket_, reinterpret_cast<sockaddr*>(&address), &size) != 0) return;
    if (address.ss_family == AF_INET) {
        const auto* v4 = reinterpret_cast<const sockaddr_in*>(&address);
        peerAddress_ = QHostAddress(ntohl(v4->sin_addr.s_addr)); peerPort_ = ntohs(v4->sin_port);
    } else if (address.ss_family == AF_INET6) {
        const auto* v6 = reinterpret_cast<const sockaddr_in6*>(&address);
        Q_IPV6ADDR bytes{};
        std::memcpy(bytes.c, &v6->sin6_addr, 16);
        peerAddress_ = QHostAddress(bytes); peerPort_ = ntohs(v6->sin6_port);
        if (v6->sin6_scope_id != 0) peerAddress_.setScopeId(QString::number(v6->sin6_scope_id));
    }
}
void NativeTcpStream::drainRead() {
    if (!connected_ || peerFin_ || socket_ == INVALID_SOCKET) return;
    const qsizetype before = input_.size();
    bool eos{};
    while (input_.size() < MaxReadQueue) {
        char bytes[16384];
        const int capacity = static_cast<int>((std::min)(static_cast<qint64>(sizeof(bytes)), MaxReadQueue - input_.size()));
        const int count = recv(socket_, bytes, capacity, 0);
        if (count > 0) { input_.append(bytes, count); continue; }
        if (count == 0) { eos = true; break; }
        const int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) { eos = closeIndicated_; break; }
        fail(QStringLiteral("Receive TCP"), error); return;
    }
    const quint64 epoch = epoch_;
    const QPointer<NativeTcpStream> self(this);
    if (input_.size() > before) emit readyRead();
    if (!self || epoch != epoch_ || socket_ == INVALID_SOCKET) return;
    if (eos) {
        // Announce EOS before read() can complete both halves and emit disconnected.
        peerFin_ = true;
        emit readChannelFinished();
        if (!self || epoch != epoch_ || socket_ == INVALID_SOCKET) return;
    }
    finishIfComplete();
}
void NativeTcpStream::flushWrites() {
    if (writing_ || !connected_ || socket_ == INVALID_SOCKET) return;
    writing_ = true;
    qint64 written{};
    while (!output_.isEmpty()) {
        const int count = send(socket_, output_.constData(), static_cast<int>((std::min)(static_cast<qint64>(output_.size()), qint64{65536})), 0);
        if (count > 0) { output_.remove(0, count); written += count; continue; }
        const int error = count == 0 ? WSAECONNRESET : WSAGetLastError();
        if (error == WSAEWOULDBLOCK) break;
        writing_ = false; fail(QStringLiteral("Send TCP"), error); return;
    }
    writing_ = false;
    const quint64 epoch = epoch_;
    const QPointer<NativeTcpStream> self(this);
    if (written) emit bytesWritten(written);
    if (!self || epoch != epoch_ || socket_ == INVALID_SOCKET) return;
    if (writeCloseRequested_ && !writeFin_ && output_.isEmpty()) {
        if (::shutdown(socket_, SD_SEND) == SOCKET_ERROR) { fail(QStringLiteral("Close TCP send half"), WSAGetLastError()); return; }
        writeFin_ = true;
    }
    finishIfComplete();
}
void NativeTcpStream::finishIfComplete() {
    if (socket_ == INVALID_SOCKET || !peerFin_ || !writeFin_ || !input_.isEmpty() || !output_.isEmpty() || closeScheduled_) return;
    // read() must return its final bytes before a synchronous disconnected slot can revoke the stream.
    closeScheduled_ = true;
    const quint64 epoch = epoch_;
    QTimer::singleShot(0, this, [this, epoch] {
        if (epoch != epoch_) return;
        closeScheduled_ = false;
        if (socket_ != INVALID_SOCKET && peerFin_ && writeFin_ && input_.isEmpty() && output_.isEmpty()) closeNative(true);
    });
}
void NativeTcpStream::scheduleRead() {
    if (readScheduled_ || socket_ == INVALID_SOCKET || peerFin_ || !connected_) return;
    readScheduled_ = true;
    const quint64 epoch = epoch_;
    QTimer::singleShot(0, this, [this, epoch] {
        if (epoch != epoch_) return;
        readScheduled_ = false;
        drainRead();
    });
}
} // namespace zzj::network::qt
