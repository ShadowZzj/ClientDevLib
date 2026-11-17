#include "NativeTcpListener.h"
#include <cstring>

namespace zzj::network::qt {
NativeTcpListener::NativeTcpListener(QObject* parent) : QTcpServer(parent) {
    WSADATA data{};
    const int error = WSAStartup(MAKEWORD(2, 2), &data);
    winsockStarted_ = error == 0;
    if (error) nativeError_ = QStringLiteral("Initialize TCP listener Winsock failed: %1").arg(error);
}
NativeTcpListener::~NativeTcpListener() { close(); if (winsockStarted_) WSACleanup(); }
bool NativeTcpListener::listenExclusive(const QHostAddress& address, quint16 port) {
    close();
    if (!winsockStarted_ || address.isNull()) { nativeError_ = QStringLiteral("Invalid TCP listen address"); return false; }
    const bool v4 = address.protocol() == QAbstractSocket::IPv4Protocol;
    const int family = v4 ? AF_INET : AF_INET6;
    const SOCKET native = WSASocketW(family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (native == INVALID_SOCKET) { nativeError_ = QStringLiteral("Create TCP listener failed: %1").arg(WSAGetLastError()); return false; }
    auto fail = [&](const QString& operation) {
        const int error = WSAGetLastError(); closesocket(native);
        nativeError_ = QStringLiteral("%1 failed: %2").arg(operation).arg(error); return false;
    };
    BOOL exclusive = TRUE;
    if (setsockopt(native, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0)
        return fail(QStringLiteral("Set exclusive TCP port ownership"));
    if (!v4) {
        DWORD v6Only = 0;
        if (setsockopt(native, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6Only), sizeof(v6Only)) != 0)
            return fail(QStringLiteral("Enable dual-stack TCP listener"));
    }
    int result{};
    if (v4) {
        sockaddr_in bind{}; bind.sin_family = AF_INET; bind.sin_port = htons(port); bind.sin_addr.s_addr = htonl(address.toIPv4Address());
        result = ::bind(native, reinterpret_cast<const sockaddr*>(&bind), sizeof(bind));
    } else {
        sockaddr_in6 bind{}; bind.sin6_family = AF_INET6; bind.sin6_port = htons(port);
        const auto bytes = address.toIPv6Address(); std::memcpy(&bind.sin6_addr, bytes.c, 16);
        bool numeric{}; bind.sin6_scope_id = address.scopeId().toUInt(&numeric);
        if (!address.scopeId().isEmpty() && !numeric) { closesocket(native); nativeError_ = QStringLiteral("TCP scope must be numeric"); return false; }
        result = ::bind(native, reinterpret_cast<const sockaddr*>(&bind), sizeof(bind));
    }
    if (result != 0) return fail(QStringLiteral("Bind TCP listener"));
    if (::listen(native, SOMAXCONN) != 0) return fail(QStringLiteral("Listen TCP"));
    u_long nonBlocking = 1;
    if (ioctlsocket(native, FIONBIO, &nonBlocking) != 0) return fail(QStringLiteral("Set nonblocking TCP listener"));
    if (!setSocketDescriptor(static_cast<qintptr>(native))) {
        nativeError_ = QTcpServer::errorString(); closesocket(native); return false;
    }
    nativeError_.clear();
    return true;
}
} // namespace zzj::network::qt
