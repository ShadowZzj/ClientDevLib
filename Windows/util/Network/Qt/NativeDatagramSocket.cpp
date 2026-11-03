#include "NativeDatagramSocket.h"
#include <QWinEventNotifier>
#include <QPointer>
#include <cstring>

namespace zzj::network::qt {
NativeDatagramSocket::NativeDatagramSocket(QObject* parent) : QObject(parent) {
    WSADATA data{};
    const int error = WSAStartup(MAKEWORD(2, 2), &data);
    winsockStarted_ = error == 0;
    if (error) error_ = QStringLiteral("Initialize UDP Winsock failed: %1").arg(error);
}
NativeDatagramSocket::~NativeDatagramSocket() { close(); if (winsockStarted_) WSACleanup(); }
void NativeDatagramSocket::close() {
    ++epoch_;
    if (notifier_) { notifier_->setEnabled(false); notifier_->deleteLater(); notifier_ = nullptr; }
    if (socket_ != INVALID_SOCKET) { WSAEventSelect(socket_, nullptr, 0); closesocket(socket_); socket_ = INVALID_SOCKET; }
    if (event_ != WSA_INVALID_EVENT) { WSACloseEvent(event_); event_ = WSA_INVALID_EVENT; }
    port_ = 0;
}
void NativeDatagramSocket::fail(const QString& operation, int error) {
    error_ = QStringLiteral("%1 failed: %2").arg(operation).arg(error);
    const QString message = error_;
    close();
    emit errorOccurred(message);
}
bool NativeDatagramSocket::bindExclusive(const QHostAddress& address, quint16 port) {
    close();
    if (!winsockStarted_ || address.isNull()) { error_ = QStringLiteral("Invalid UDP bind address"); return false; }
    const bool v4 = address.protocol() == QAbstractSocket::IPv4Protocol;
    const int family = v4 ? AF_INET : AF_INET6;
    socket_ = WSASocketW(family, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket_ == INVALID_SOCKET) { fail(QStringLiteral("Create UDP listener"), WSAGetLastError()); return false; }
    BOOL exclusive = TRUE;
    if (setsockopt(socket_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0) {
        fail(QStringLiteral("Set exclusive UDP port ownership"), WSAGetLastError()); return false;
    }
    if (!v4) {
        DWORD v6Only = 0;
        if (setsockopt(socket_, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6Only), sizeof(v6Only)) != 0) {
            fail(QStringLiteral("Enable dual-stack UDP listener"), WSAGetLastError()); return false;
        }
    }
    int result{};
    if (v4) {
        sockaddr_in bind{}; bind.sin_family = AF_INET; bind.sin_port = htons(port); bind.sin_addr.s_addr = htonl(address.toIPv4Address());
        result = ::bind(socket_, reinterpret_cast<const sockaddr*>(&bind), sizeof(bind));
    } else {
        sockaddr_in6 bind{}; bind.sin6_family = AF_INET6; bind.sin6_port = htons(port);
        const auto bytes = address.toIPv6Address(); std::memcpy(&bind.sin6_addr, bytes.c, 16);
        bool numeric{}; bind.sin6_scope_id = address.scopeId().toUInt(&numeric);
        if (!address.scopeId().isEmpty() && !numeric) { fail(QStringLiteral("UDP scope must be numeric"), WSAEINVAL); return false; }
        result = ::bind(socket_, reinterpret_cast<const sockaddr*>(&bind), sizeof(bind));
    }
    if (result != 0) { fail(QStringLiteral("Bind UDP listener"), WSAGetLastError()); return false; }
    sockaddr_storage actual{}; int size = sizeof(actual);
    if (getsockname(socket_, reinterpret_cast<sockaddr*>(&actual), &size) != 0) { fail(QStringLiteral("Read UDP port"), WSAGetLastError()); return false; }
    port_ = ntohs(v4 ? reinterpret_cast<sockaddr_in*>(&actual)->sin_port : reinterpret_cast<sockaddr_in6*>(&actual)->sin6_port);
    event_ = WSACreateEvent();
    if (event_ == WSA_INVALID_EVENT || WSAEventSelect(socket_, event_, FD_READ | FD_WRITE | FD_CLOSE) == SOCKET_ERROR) {
        fail(QStringLiteral("Select UDP events"), WSAGetLastError()); return false;
    }
    notifier_ = new QWinEventNotifier(event_, this);
    connect(notifier_, &QWinEventNotifier::activated, this, [this](HANDLE) { activate(); });
    error_.clear();
    return true;
}
void NativeDatagramSocket::activate() {
    if (!notifier_ || socket_ == INVALID_SOCKET) return;
    const quint64 epoch = epoch_;
    notifier_->setEnabled(false);
    WSANETWORKEVENTS events{};
    if (WSAEnumNetworkEvents(socket_, event_, &events) != 0) { fail(QStringLiteral("Read UDP events"), WSAGetLastError()); return; }
    const QPointer<NativeDatagramSocket> self(this);
    if (events.lNetworkEvents & FD_CLOSE) { fail(QStringLiteral("UDP socket closed"), events.iErrorCode[FD_CLOSE_BIT]); return; }
    if (events.lNetworkEvents & FD_READ) {
        if (events.iErrorCode[FD_READ_BIT]) { fail(QStringLiteral("UDP read event"), events.iErrorCode[FD_READ_BIT]); return; }
        emit readyRead();
        if (!self || epoch != epoch_ || socket_ == INVALID_SOCKET) return;
    }
    if (events.lNetworkEvents & FD_WRITE) {
        if (events.iErrorCode[FD_WRITE_BIT]) { fail(QStringLiteral("UDP write event"), events.iErrorCode[FD_WRITE_BIT]); return; }
        emit writeAvailable();
        if (!self || epoch != epoch_ || socket_ == INVALID_SOCKET) return;
    }
    notifier_->setEnabled(true);
}
} // namespace zzj::network::qt
