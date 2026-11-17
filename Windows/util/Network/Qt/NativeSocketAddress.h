#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <QHostAddress>
#include <cstring>

namespace zzj::network::qt {
// Numeric-only conversion; named IPv6 interface scopes must be resolved by the caller.
inline bool ToNativeAddress(const QHostAddress& address, quint16 port, sockaddr_storage& native, int& length) {
    native = {}; length = 0;
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        auto* v4 = reinterpret_cast<sockaddr_in*>(&native);
        v4->sin_family = AF_INET; v4->sin_port = htons(port); v4->sin_addr.s_addr = htonl(address.toIPv4Address());
        length = sizeof(sockaddr_in); return true;
    }
    if (address.protocol() != QAbstractSocket::IPv6Protocol) return false;
    auto* v6 = reinterpret_cast<sockaddr_in6*>(&native);
    v6->sin6_family = AF_INET6; v6->sin6_port = htons(port);
    const auto bytes = address.toIPv6Address(); std::memcpy(&v6->sin6_addr, bytes.c, 16);
    bool numeric{}; v6->sin6_scope_id = address.scopeId().toUInt(&numeric);
    if (!address.scopeId().isEmpty() && !numeric) return false;
    length = sizeof(sockaddr_in6); return true;
}
inline bool NativeEndpoint(const sockaddr_storage& native, int length, QHostAddress& address, quint16& port) {
    if (native.ss_family == AF_INET && length >= sizeof(sockaddr_in)) {
        const auto* v4 = reinterpret_cast<const sockaddr_in*>(&native);
        address = QHostAddress(ntohl(v4->sin_addr.s_addr)); port = ntohs(v4->sin_port); return true;
    }
    if (native.ss_family != AF_INET6 || length < sizeof(sockaddr_in6)) return false;
    const auto* v6 = reinterpret_cast<const sockaddr_in6*>(&native);
    Q_IPV6ADDR bytes{}; std::memcpy(bytes.c, &v6->sin6_addr, 16);
    address = QHostAddress(bytes); port = ntohs(v6->sin6_port);
    if (v6->sin6_scope_id) address.setScopeId(QString::number(v6->sin6_scope_id));
    return true;
}
inline bool SameNativeEndpoint(const sockaddr_storage& a, int aLength, const sockaddr_storage& b, int bLength) {
    QHostAddress first, second; quint16 firstPort{}, secondPort{};
    return NativeEndpoint(a, aLength, first, firstPort) && NativeEndpoint(b, bLength, second, secondPort) &&
        firstPort == secondPort && first.isEqual(second, QHostAddress::ConvertV4MappedToIPv4) &&
        first.scopeId() == second.scopeId();
}
} // namespace zzj::network::qt
