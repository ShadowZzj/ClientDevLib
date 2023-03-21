#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <sys/types.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#else
#include <IPExport.h>
#include <Windows.h>
#include <icmpapi.h>
#include <inaddr.h>
#include <iphlpapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#endif
namespace zzj
{
static uint64_t utime(void)
{
#ifdef _WIN32
    LARGE_INTEGER count;
    LARGE_INTEGER frequency;
    if (QueryPerformanceCounter(&count) == 0 || QueryPerformanceFrequency(&frequency) == 0)
    {
        return 0;
    }
    return count.QuadPart * 1000000 / frequency.QuadPart;
#else
    struct timeval now;
    return gettimeofday(&now, NULL) != 0 ? 0 : now.tv_sec * 1000000 + now.tv_usec;
#endif
}

class PingerInterface
{
  public:
    virtual ~PingerInterface()
    {
    }

    virtual bool send_ping(const std::string &destination, int timeoutSecond) = 0;
    virtual double get_average_delay() const                                  = 0;
    virtual void Reset()                                                      = 0;
};

#ifdef _WIN32
// Windows specific implementation
// ...
class WinPinger : public PingerInterface
{
  public:
    std::string get_ip_address(const std::string &hostname)
    {
        addrinfo hints    = {};
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo *res = nullptr;
        if (getaddrinfo(hostname.c_str(), nullptr, &hints, &res) != 0)
        {
            std::cerr << "Error resolving address" << std::endl;
            return "";
        }

        char ipAddr[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &((sockaddr_in *)res->ai_addr)->sin_addr, ipAddr, sizeof(ipAddr)) == nullptr)
        {
            std::cerr << "Error converting IP address" << std::endl;
            freeaddrinfo(res);
            return "";
        }

        freeaddrinfo(res);

        return ipAddr;
    }
    bool send_ping(const std::string &destination, int timeoutSecond = 2) override
    {
        HANDLE hIcmpFile;
        DWORD dwRetVal     = 0;
        DWORD dwError      = 0;
        char SendData[32]  = "Data Buffer";
        LPVOID ReplyBuffer = nullptr;
        DWORD ReplySize    = 0;

        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            std::cerr << "WSAStartup failed: " << WSAGetLastError() << std::endl;
            return false;
        }

        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        if (getaddrinfo(destination.c_str(), nullptr, &hints, &res) != 0)
        {
            std::cerr << "Error resolving address" << std::endl;
            WSACleanup();
            return false;
        }

        struct sockaddr_in *ipv4_addr = (struct sockaddr_in *)res->ai_addr;
        unsigned long ipAddress       = ipv4_addr->sin_addr.S_un.S_addr;

        hIcmpFile = IcmpCreateFile();
        if (hIcmpFile == INVALID_HANDLE_VALUE)
        {
            std::cerr << "Unable to open handle: " << GetLastError() << std::endl;
            freeaddrinfo(res);
            WSACleanup();
            return false;
        }

        ReplySize   = sizeof(ICMP_ECHO_REPLY) + sizeof(SendData);
        ReplyBuffer = (VOID *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ReplySize);
        if (ReplyBuffer == nullptr)
        {
            std::cerr << "HeapAlloc failed: " << GetLastError() << std::endl;
            IcmpCloseHandle(hIcmpFile);
            freeaddrinfo(res);
            WSACleanup();
            return false;
        }

        dwRetVal = IcmpSendEcho(hIcmpFile, ipAddress, SendData, sizeof(SendData), nullptr, ReplyBuffer, ReplySize,
                                timeoutSecond * 1000);

        if (dwRetVal != 0)
        {
            PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)ReplyBuffer;
            struct in_addr ReplyAddr;
            ReplyAddr.S_un.S_addr = pEchoReply->Address;
            num_replies_++;
            total_time_ += pEchoReply->RoundTripTime;
        }
        else
        {
            dwError = GetLastError();
            std::cerr << "IcmpSendEcho failed: " << dwError << std::endl;
            HeapFree(GetProcessHeap(), 0, ReplyBuffer);
            IcmpCloseHandle(hIcmpFile);
            freeaddrinfo(res);
            WSACleanup();
            return false;
        }

        HeapFree(GetProcessHeap(), 0, ReplyBuffer);
        IcmpCloseHandle(hIcmpFile);
        freeaddrinfo(res);
        WSACleanup();
        return true;
    }

    double get_average_delay() const override
    {
        return num_replies_ > 0 ? total_time_ / num_replies_ : 0;
    }
    void Reset() override
    {
        num_replies_ = 0;
        total_time_  = 0;
    }

  private:
    int num_replies_   = 0;
    double total_time_ = 0;
};

#else
// macOS specific implementation
class MacPinger : public PingerInterface
{
  public:
    bool send_ping(const std::string &destination, int timeoutSecond = 2) override
    {
        struct addrinfo hints, *res;
        int sockfd;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_RAW;
        hints.ai_protocol = IPPROTO_ICMP;

        if (getaddrinfo(destination.c_str(), nullptr, &hints, &res) != 0)
        {
            std::cerr << "Error resolving address" << std::endl;
            return false;
        }

        sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sockfd < 0)
        {
            freeaddrinfo(res);
            return false;
        }
        // Set the timeout value
        struct timeval timeout;
        timeout.tv_sec = timeoutSecond; // Timeout in seconds
        timeout.tv_usec = 0;            // Additional timeout in microseconds

        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
        {
            close(sockfd);
            freeaddrinfo(res);
            return false;
        }

        struct icmp icmp_hdr;
        icmp_hdr.icmp_type = ICMP_ECHO;
        icmp_hdr.icmp_code = 0;
        icmp_hdr.icmp_id = htons(getpid());
        icmp_hdr.icmp_seq = htons(1);
        icmp_hdr.icmp_cksum = 0;
        // icmp_hdr.icmp_cksum = checksum(&icmp_hdr, sizeof(icmp_hdr));
        char buf[1024]{1};

        // Add data to the ICMP request and include it in the checksum
        const size_t data_size = 56; // 56 bytes of data (default size for the 'ping' utility)
        char data[data_size];
        for (size_t i = 0; i < data_size; ++i)
        {
            data[i] = i;
        }

        // Allocate memory for the ICMP packet
        char packet[sizeof(icmp_hdr) + data_size];
        memcpy(packet, &icmp_hdr, sizeof(icmp_hdr));
        memcpy(packet + sizeof(icmp_hdr), data, data_size);
        icmp_hdr.icmp_cksum = checksum(packet, sizeof(packet));

        // Update the ICMP header's checksum
        memcpy(packet, &icmp_hdr, sizeof(icmp_hdr));

        ssize_t bytes_sent = sendto(sockfd, packet, sizeof(packet), 0, res->ai_addr, res->ai_addrlen);
        if (bytes_sent < 0)
        {
            close(sockfd);
            freeaddrinfo(res);
            return false;
        }

        auto timeSent = utime();
        ssize_t bytes_received = recv(sockfd, buf, sizeof(buf), 0);
        if (bytes_received < 0)
        {
            close(sockfd);
            freeaddrinfo(res);
            return false;
        }
        auto timeRecv = utime();

        struct icmp *icmp_reply = (struct icmp *)(buf + 20); // Skip IP header
        int reply_id = ntohs(icmp_reply->icmp_id);
        int reply_seq = ntohs(icmp_reply->icmp_seq);
        int request_id = ntohs(icmp_hdr.icmp_id);
        int request_seq = ntohs(icmp_hdr.icmp_seq);
        if (reply_id != request_id || reply_seq != request_seq)
        {
            std::cout << "id error" << std::endl;
            close(sockfd);
            freeaddrinfo(res);
            return false;
        }
        if (icmp_reply->icmp_type == ICMP_ECHOREPLY)
        {
            num_replies_++;
            double round_trip_time_ms = (timeRecv - timeSent) / 1000.0;
            total_time_ += round_trip_time_ms;
        }

        close(sockfd);
        return true;
    }
    double get_average_delay() const override
    {
        return num_replies_ > 0 ? total_time_ / num_replies_ : 0;
    }
    void Reset() override
    {
        num_replies_ = 0;
        total_time_ = 0;
    }

  private:
    //    uint16_t checksum(void *data, int len)
    //    {
    //        uint16_t *addr = (uint16_t *)data;
    //        uint32_t sum = 0;
    //        while (len > 1)
    //        {
    //            sum += *addr++;
    //            len -= 2;
    //        }
    //        if (len > 0)
    //        {
    //            sum += *(uint8_t *)addr;
    //        }
    //        while (sum >> 16)
    //            sum = (sum & 0xffff) + (sum >> 16);
    //
    //        return ~sum;
    //    }
    uint16_t checksum(void *vdata, size_t length)
    {
        // Cast the data pointer to one that can be indexed
        char *data = (char *)vdata;

        // Initialise the accumulator
        uint32_t acc = 0xffff;

        // Handle complete 16-bit blocks
        for (size_t i = 0; i + 1 < length; i += 2)
        {
            uint16_t word;
            memcpy(&word, data + i, 2);
            acc += ntohs(word);
            if (acc > 0xffff)
            {
                acc -= 0xffff;
            }
        }

        // Handle any partial block at the end of the data
        if (length & 1)
        {
            uint16_t word = 0;
            memcpy(&word, data + length - 1, 1);
            acc += ntohs(word);
            if (acc > 0xffff)
            {
                acc -= 0xffff;
            }
        }

        // Return the checksum in network byte order
        return htons(~acc);
    }

  private:
    int num_replies_ = 0;
    double total_time_ = 0;
};
#endif
}; // namespace zzj
