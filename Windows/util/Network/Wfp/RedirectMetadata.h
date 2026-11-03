#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace zzj::network::wfp
{
struct RedirectMetadata
{
    // Context is the driver/service's versioned ABI, records are WFP opaque
    // bytes. Never confuse them or deserialize records as application structs.
    std::vector<std::uint8_t> context;
    std::vector<std::uint8_t> records;

    static int Query(SOCKET connectedSocket, RedirectMetadata& output);
    static int ApplyRecords(SOCKET outboundSocket, const std::vector<std::uint8_t>& records);
    // Opt in before receiving UDP redirects. AF_INET6 also enables IPv4
    // metadata when the socket is dual stack. Close the socket on failure.
    static int EnableDatagramMetadata(SOCKET socket, int addressFamily);
    // UDP metadata exists on the first datagram of a redirected flow. Keep it
    // in that flow's session and forward records on the first outbound message.
    static int FromDatagram(const WSAMSG& message, RedirectMetadata& output);
};

struct ReceivedDatagram
{
    sockaddr_storage sender{};
    int senderLength = sizeof(sockaddr_storage);
    DWORD bytes = 0;
    RedirectMetadata redirect;
};

// Caller starts Winsock and owns the socket. These preserve SOCKET width and
// return zero or a Winsock error (including WSAEWOULDBLOCK on nonblocking use).
int ReceiveRedirectedDatagram(SOCKET socket, std::uint8_t* buffer, std::size_t size,
                              ReceivedDatagram& output);
int SendRedirectedDatagram(SOCKET socket, const sockaddr* destination, int destinationLength,
                           const std::uint8_t* buffer, std::size_t size,
                           const std::vector<std::uint8_t>& records, DWORD& bytesSent);
} // namespace zzj::network::wfp
