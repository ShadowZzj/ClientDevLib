#include "RedirectMetadata.h"

#include <mstcpip.h>
#include <algorithm>
#include <cstring>
#include <limits>

namespace zzj::network::wfp
{
namespace
{
constexpr std::size_t MaxMetadataSize = 65536;
int QueryBlob(SOCKET socket, DWORD command, std::vector<std::uint8_t>& output)
{
    output.clear();
    DWORD capacity = 4096;
    for (unsigned int attempt = 0; attempt < 2; ++attempt)
    {
        std::vector<std::uint8_t> data(capacity);
        DWORD returned = 0;
        if (WSAIoctl(socket, command, nullptr, 0, data.data(), capacity, &returned, nullptr, nullptr) == 0)
        {
            if (returned == 0 || returned > capacity)
                return WSAEINVAL;
            data.resize(returned);
            output = std::move(data);
            return 0;
        }
        const int error = WSAGetLastError();
        if (error != ERROR_INSUFFICIENT_BUFFER || returned <= capacity || returned > MaxMetadataSize)
            return error;
        capacity = returned;
    }
    return WSAEMSGSIZE;
}
template <typename T>
int Extension(SOCKET socket, GUID id, T& function)
{
    DWORD returned = 0;
    return WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &id, sizeof(id), &function, sizeof(function),
                    &returned, nullptr, nullptr) == 0 ? 0 : WSAGetLastError();
}
std::size_t Align(std::size_t value)
{
    constexpr std::size_t alignment = sizeof(SIZE_T);
    return (value + alignment - 1) & ~(alignment - 1);
}
} // namespace

int RedirectMetadata::Query(SOCKET socket, RedirectMetadata& output)
{
    output = {};
    RedirectMetadata temporary;
    int error = QueryBlob(socket, SIO_QUERY_WFP_CONNECTION_REDIRECT_CONTEXT, temporary.context);
    if (error)
        return error;
    error = QueryBlob(socket, SIO_QUERY_WFP_CONNECTION_REDIRECT_RECORDS, temporary.records);
    if (error)
        return error;
    output = std::move(temporary);
    return 0;
}
int RedirectMetadata::ApplyRecords(SOCKET socket, const std::vector<std::uint8_t>& records)
{
    if (records.empty() || records.size() > MaxMetadataSize)
        return WSAEINVAL;
    DWORD returned = 0;
    return WSAIoctl(socket, SIO_SET_WFP_CONNECTION_REDIRECT_RECORDS,
                    const_cast<std::uint8_t*>(records.data()), static_cast<DWORD>(records.size()),
                    nullptr, 0, &returned, nullptr, nullptr) == 0 ? 0 : WSAGetLastError();
}

int RedirectMetadata::EnableDatagramMetadata(SOCKET socket, int addressFamily)
{
    if (addressFamily != AF_INET && addressFamily != AF_INET6)
        return WSAEAFNOSUPPORT;
    int type{};
    int size = sizeof(type);
    if (getsockopt(socket, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &size) != 0)
        return WSAGetLastError();
    if (type != SOCK_DGRAM)
        return WSAEINVAL;
    const DWORD enabled = 1;
    const auto enable = [&](int level, int records, int context) {
        if (setsockopt(socket, level, records, reinterpret_cast<const char*>(&enabled), sizeof(enabled)) != 0 ||
            setsockopt(socket, level, context, reinterpret_cast<const char*>(&enabled), sizeof(enabled)) != 0)
            return WSAGetLastError();
        return 0;
    };
    if (addressFamily == AF_INET)
        return enable(IPPROTO_IP, IP_WFP_REDIRECT_RECORDS, IP_WFP_REDIRECT_CONTEXT);
    const int error = enable(IPPROTO_IPV6, IPV6_WFP_REDIRECT_RECORDS, IPV6_WFP_REDIRECT_CONTEXT);
    if (error)
        return error;
    DWORD v6Only{};
    size = sizeof(v6Only);
    if (getsockopt(socket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char*>(&v6Only), &size) != 0)
        return WSAGetLastError();
    return v6Only ? 0 : enable(IPPROTO_IP, IP_WFP_REDIRECT_RECORDS, IP_WFP_REDIRECT_CONTEXT);
}

int RedirectMetadata::FromDatagram(const WSAMSG& message, RedirectMetadata& output)
{
    output = {};
    if (message.dwFlags & MSG_CTRUNC)
        return WSAEMSGSIZE;
    if (message.Control.len == 0)
        return 0;
    if (!message.Control.buf || message.Control.len > MaxMetadataSize)
        return WSAEINVAL;
    RedirectMetadata temporary;
    std::size_t offset = 0;
    bool recordsSeen = false;
    bool contextSeen = false;
    while (offset < message.Control.len)
    {
        const std::size_t remaining = message.Control.len - offset;
        if (remaining < sizeof(WSACMSGHDR))
            return WSAEINVAL;
        WSACMSGHDR header{};
        std::memcpy(&header, message.Control.buf + offset, sizeof(header));
        const std::size_t headerLength = WSA_CMSG_LEN(0);
        if (header.cmsg_len < headerLength || header.cmsg_len > remaining)
            return WSAEINVAL;
        const auto* begin = reinterpret_cast<const std::uint8_t*>(message.Control.buf + offset + headerLength);
        const auto dataLength = header.cmsg_len - headerLength;
        const bool ipv4 = header.cmsg_level == IPPROTO_IP;
        const bool ipv6 = header.cmsg_level == IPPROTO_IPV6;
        std::vector<std::uint8_t>* target = nullptr;
        if ((ipv4 && header.cmsg_type == IP_WFP_REDIRECT_RECORDS) ||
            (ipv6 && header.cmsg_type == IPV6_WFP_REDIRECT_RECORDS))
            target = &temporary.records;
        else if ((ipv4 && header.cmsg_type == IP_WFP_REDIRECT_CONTEXT) ||
                 (ipv6 && header.cmsg_type == IPV6_WFP_REDIRECT_CONTEXT))
            target = &temporary.context;
        if (target)
        {
            auto& seen = target == &temporary.records ? recordsSeen : contextSeen;
            if (seen)
                return WSAEINVAL;
            seen = true;
            // With these options enabled, ordinary UDP carries empty WFP
            // control headers. They indicate no redirect metadata.
            if (dataLength != 0)
                target->assign(begin, begin + dataLength);
        }
        const std::size_t next = Align(header.cmsg_len);
        if (next > remaining)
        {
            if (header.cmsg_len != remaining)
                return WSAEINVAL;
            break;
        }
        offset += next;
    }
    output = std::move(temporary);
    return 0;
}

int ReceiveRedirectedDatagram(SOCKET socket, std::uint8_t* buffer, std::size_t size, ReceivedDatagram& output)
{
    output = {};
    if (!buffer || size == 0 || size > 65535)
        return WSAEINVAL;
    LPFN_WSARECVMSG receive = nullptr;
    int error = Extension(socket, WSAID_WSARECVMSG, receive);
    if (error)
        return error;
    std::vector<char> control(MaxMetadataSize);
    WSABUF payload{static_cast<ULONG>(size), reinterpret_cast<char*>(buffer)};
    WSAMSG message{};
    message.name = reinterpret_cast<sockaddr*>(&output.sender);
    message.namelen = sizeof(output.sender);
    message.lpBuffers = &payload;
    message.dwBufferCount = 1;
    message.Control.buf = control.data();
    message.Control.len = static_cast<ULONG>(control.size());
    if (receive(socket, &message, &output.bytes, nullptr, nullptr) != 0)
        return WSAGetLastError();
    if (message.dwFlags & MSG_TRUNC)
        return WSAEMSGSIZE;
    output.senderLength = message.namelen;
    return RedirectMetadata::FromDatagram(message, output.redirect);
}

int SendRedirectedDatagram(SOCKET socket, const sockaddr* destination, int destinationLength,
                           const std::uint8_t* buffer, std::size_t size,
                           const std::vector<std::uint8_t>& records, DWORD& bytesSent)
{
    bytesSent = 0;
    if ((!buffer && size != 0) || size > 65535 || records.size() > MaxMetadataSize - WSA_CMSG_SPACE(0) ||
        (!destination && !records.empty()))
        return WSAEINVAL;
    if (destination && ((destination->sa_family == AF_INET && destinationLength != sizeof(sockaddr_in)) ||
                        (destination->sa_family == AF_INET6 && destinationLength != sizeof(sockaddr_in6)) ||
                        (destination->sa_family != AF_INET && destination->sa_family != AF_INET6)))
        return WSAEINVAL;
    LPFN_WSASENDMSG send = nullptr;
    int error = Extension(socket, WSAID_WSASENDMSG, send);
    if (error)
        return error;
    WSABUF payload{static_cast<ULONG>(size), reinterpret_cast<char*>(const_cast<std::uint8_t*>(buffer))};
    WSAMSG message{};
    message.name = const_cast<sockaddr*>(destination);
    message.namelen = destination ? destinationLength : 0;
    message.lpBuffers = &payload;
    message.dwBufferCount = 1;
    std::vector<char> control;
    if (!records.empty())
    {
        control.resize(WSA_CMSG_SPACE(records.size()), 0);
        auto* header = reinterpret_cast<WSACMSGHDR*>(control.data());
        header->cmsg_len = WSA_CMSG_LEN(records.size());
        header->cmsg_level = destination->sa_family == AF_INET ? IPPROTO_IP : IPPROTO_IPV6;
        header->cmsg_type = destination->sa_family == AF_INET ? IP_WFP_REDIRECT_RECORDS : IPV6_WFP_REDIRECT_RECORDS;
        std::memcpy(WSA_CMSG_DATA(header), records.data(), records.size());
        message.Control.buf = control.data();
        message.Control.len = static_cast<ULONG>(control.size());
    }
    return send(socket, &message, 0, &bytesSent, nullptr, nullptr) == 0 ? 0 : WSAGetLastError();
}
} // namespace zzj::network::wfp
