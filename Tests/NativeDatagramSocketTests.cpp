#include <Windows/util/Network/Qt/NativeDatagramSocket.h>
#include <Windows/util/Network/Qt/NativeSocketAddress.h>
#include <Windows/util/Network/Wfp/RedirectMetadata.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QList>
#include <cstdio>

using zzj::network::qt::NativeDatagramSocket;
using namespace zzj::network::wfp;
namespace {
int checks{};
bool Check(bool value, const char* name) {
    ++checks;
    if (!value) std::fprintf(stderr, "FAIL: %s\n", name);
    return value;
}
bool MetadataOptIn(SOCKET socket, bool dualStack) {
    const auto enabled = [&](int level, int option, DWORD expected) {
        DWORD value{}; int size = sizeof(value);
        return getsockopt(socket, level, option, reinterpret_cast<char*>(&value), &size) == 0 && value == expected;
    };
    bool ok = Check(enabled(IPPROTO_IP, IP_WFP_REDIRECT_RECORDS, 0) &&
        enabled(IPPROTO_IP, IP_WFP_REDIRECT_CONTEXT, 0), "Ordinary UDP leaves IPv4 WFP metadata disabled");
    if (dualStack)
        ok &= Check(enabled(IPPROTO_IPV6, IPV6_WFP_REDIRECT_RECORDS, 0) &&
            enabled(IPPROTO_IPV6, IPV6_WFP_REDIRECT_CONTEXT, 0), "Ordinary UDP leaves IPv6 WFP metadata disabled");
    ok &= Check(RedirectMetadata::EnableDatagramMetadata(socket, dualStack ? AF_INET6 : AF_INET) == 0,
        "Explicitly opt in to UDP WFP metadata");
    ok &= Check(enabled(IPPROTO_IP, IP_WFP_REDIRECT_RECORDS, 1) &&
        enabled(IPPROTO_IP, IP_WFP_REDIRECT_CONTEXT, 1), "IPv4 records and context enabled, including dual stack");
    if (dualStack)
        ok &= Check(enabled(IPPROTO_IPV6, IPV6_WFP_REDIRECT_RECORDS, 1) &&
            enabled(IPPROTO_IPV6, IPV6_WFP_REDIRECT_CONTEXT, 1), "Dual-stack IPv6 records and context enabled");
    ok &= Check(RedirectMetadata::EnableDatagramMetadata(socket, AF_UNSPEC) == WSAEAFNOSUPPORT,
        "Unsupported metadata address family rejected");
    ok &= Check(RedirectMetadata::EnableDatagramMetadata(INVALID_SOCKET, AF_INET) == WSAENOTSOCK,
        "Invalid metadata socket returns Winsock error");
    return ok;
}
bool TwoWaves(bool dualStack) {
    NativeDatagramSocket receiver;
    if (!Check(receiver.bindExclusive(dualStack ? QHostAddress::AnyIPv6 : QHostAddress::AnyIPv4, 0), "Bind native UDP")) return false;
    NativeDatagramSocket competitor;
    bool ok = Check(!competitor.bindExclusive(QHostAddress::AnyIPv4, receiver.localPort()), "Exclusive UDP port rejects takeover");
    ok &= MetadataOptIn(static_cast<SOCKET>(receiver.socketDescriptor()), dualStack);
    const SOCKET sender = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (!Check(sender != INVALID_SOCKET, "Create sender")) return false;
    sockaddr_in target{}; target.sin_family = AF_INET; target.sin_port = htons(receiver.localPort()); target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const auto send = [&](const QByteArray& data) {
        DWORD sent{};
        const int error = SendRedirectedDatagram(sender, reinterpret_cast<const sockaddr*>(&target), sizeof(target),
            reinterpret_cast<const std::uint8_t*>(data.constData()), data.size(), {}, sent);
        ok &= Check(error == 0 && sent == static_cast<DWORD>(data.size()), "Native send, including empty datagram");
    };
    QEventLoop loop;
    QList<QByteArray> packets;
    int notifications{}; int receiveError{}; bool secondWave{};
    QObject::connect(&receiver, &NativeDatagramSocket::readyRead, &loop, [&] {
        ++notifications;
        for (;;) {
            std::uint8_t payload[256]; ReceivedDatagram packet;
            const int error = ReceiveRedirectedDatagram(static_cast<SOCKET>(receiver.socketDescriptor()), payload, sizeof(payload), packet);
            if (error == WSAEWOULDBLOCK) break;
            if (error) { receiveError = error; loop.quit(); return; }
            packets.push_back(QByteArray(reinterpret_cast<const char*>(payload), packet.bytes));
            if (packets.size() == 4) loop.quit();
        }
    });
    QObject::connect(&receiver, &NativeDatagramSocket::errorOccurred, &loop, [&](const QString&) { receiveError = -1; loop.quit(); });
    send(QByteArray("first")); send(QByteArray());
    QTimer::singleShot(120, &loop, [&] {
        secondWave = true;
        ok &= Check(packets.size() == 2, "First wave completely drained before next wave");
        send(QByteArray("second")); send(QByteArray());
    });
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    if (receiveError)
        std::fprintf(stderr, "UDP family=%s receiveError=%d packets=%lld notifications=%d\n",
            dualStack ? "dual" : "v4", receiveError, static_cast<long long>(packets.size()), notifications);
    ok &= Check(secondWave && notifications >= 2, "Second wave produces a fresh native notification");
    ok &= Check(receiveError == 0 && packets == QList<QByteArray>{"first", {}, "second", {}}, "Both waves retain payloads and zero-length packets");
    receiver.close();
    ok &= Check(receiver.socketDescriptor() == -1 && receiver.localPort() == 0, "Close clears descriptor and bound port");
    closesocket(sender);
    return ok;
}
bool CloseDuringRead() {
    NativeDatagramSocket receiver;
    if (!Check(receiver.bindExclusive(QHostAddress::AnyIPv4, 0), "Bind callback-close receiver")) return false;
    const SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in target{}; target.sin_family = AF_INET; target.sin_port = htons(receiver.localPort()); target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    QEventLoop loop; int notifications{};
    QObject::connect(&receiver, &NativeDatagramSocket::readyRead, &loop, [&] { ++notifications; receiver.close(); loop.quit(); });
    const bool sent = sendto(sender, "x", 1, 0, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == 1;
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    closesocket(sender);
    return Check(sent && notifications == 1 && receiver.socketDescriptor() == -1, "Close from synchronous read callback is safe");
}
bool PeerValidation() {
    using namespace zzj::network::qt;
    sockaddr_storage v4{}, mapped{}, scoped{}; int v4Length{}, mappedLength{}, scopedLength{};
    bool ok = Check(ToNativeAddress(QHostAddress("127.0.0.1"), 1000, v4, v4Length) &&
        ToNativeAddress(QHostAddress("::ffff:127.0.0.1"), 1000, mapped, mappedLength), "Numeric native address conversion");
    ok &= Check(SameNativeEndpoint(v4, v4Length, mapped, mappedLength), "Mapped IPv4 peer identity matches");
    reinterpret_cast<sockaddr_in6*>(&mapped)->sin6_port = htons(1001);
    ok &= Check(!SameNativeEndpoint(v4, v4Length, mapped, mappedLength), "Different peer source port rejected");
    ok &= Check(!SameNativeEndpoint(v4, sizeof(sockaddr_in) - 1, v4, v4Length), "Truncated peer sockaddr rejected");
    QHostAddress address("fe80::1"); address.setScopeId("2");
    ToNativeAddress(address, 1000, scoped, scopedLength); auto another = scoped;
    reinterpret_cast<sockaddr_in6*>(&another)->sin6_scope_id = 3;
    ok &= Check(!SameNativeEndpoint(scoped, scopedLength, another, scopedLength), "IPv6 peer interface scope enforced");
    address.setScopeId("UnresolvedInterface");
    ok &= Check(!ToNativeAddress(address, 1000, scoped, scopedLength), "Unresolved IPv6 interface scope rejected");
    return ok;
}
}
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    bool ok = TwoWaves(false);
    ok &= TwoWaves(true);
    ok &= CloseDuringRead();
    ok &= PeerValidation();
    std::printf("NativeDatagramSocket: %d checks, %s\n", checks, ok ? "passed" : "failed");
    return ok ? 0 : 1;
}
