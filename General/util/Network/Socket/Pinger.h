#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#else
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <Windows.h>
#include <IPExport.h>
#include <icmpapi.h>
#include <ws2tcpip.h>
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
        PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)ReplyBuffer;
        if (dwRetVal != 0 && pEchoReply->Address == ipAddress)
        {
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
class PingReceiver
{
  public:
    friend class MacPinger;
    PingReceiver()
    {
        sockfd_ = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (sockfd_ < 0)
        {
            throw std::runtime_error("Error creating socket");
        }
        // Set the timeout value
        struct timeval timeout;
        timeout.tv_sec = 2;  // Timeout in seconds
        timeout.tv_usec = 0; // Additional timeout in microseconds

        if (setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
        {
            throw std::runtime_error("Error set sock rectimeo");
        }
        running_ = true;
        recv_thread_ = std::thread(&PingReceiver::recv_loop, this);
    }

    ~PingReceiver()
    {
        running_ = false;
        if (recv_thread_.joinable())
        {
            recv_thread_.join();
        }
        close(sockfd_);
    }

    int sockfd() const
    {
        return sockfd_;
    }

    std::shared_ptr<std::vector<char>> get_response(int16_t id, int16_t sequence)
    {
        auto key = std::make_pair(id, sequence);
        auto iter = responses_.find(key);
        if (iter != responses_.end())
        {
            return iter->second;
        }
        return nullptr;
    }

    void remove_response(int16_t id, int16_t sequence)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        responses_.erase(std::make_pair(id, sequence));
    }

  private:
    void recv_loop()
    {
        char buf[1024];
        while (running_)
        {
            ssize_t bytes_received = recv(sockfd_, buf, sizeof(buf), 0);
            if (bytes_received < 0)
            {
                if (errno == EINTR) // Continue if interrupted by a signal
                {
                    continue;
                }
                else if (errno == EAGAIN || errno == EWOULDBLOCK) // Timeout
                {
                    continue;
                }
                else
                {
                    std::cerr << "Error receiving packet: " << strerror(errno) << std::endl;
                    break;
                }
            }

            struct ip *ip_hdr = (struct ip *)buf;
            struct icmp *icmp_reply = (struct icmp *)(buf + (ip_hdr->ip_hl << 2));

            if (icmp_reply->icmp_type == ICMP_ECHOREPLY)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                int16_t id = ntohs(icmp_reply->icmp_id);
                int16_t seq = ntohs(icmp_reply->icmp_seq);
                auto key = std::make_pair(id, seq);
                auto data = std::make_shared<std::vector<char>>(buf, buf + bytes_received);
                responses_[key] = data;
                cv_.notify_all();
            }
        }
    }

  private:
    int sockfd_;
    std::atomic<bool> running_;
    std::thread recv_thread_;
    std::map<std::pair<int16_t, int16_t>, std::shared_ptr<std::vector<char>>> responses_;
    std::mutex mutex_;
    std::condition_variable cv_;
};
// macOS specific implementation
class MacPinger : public PingerInterface
{
  public:
    virtual ~MacPinger()
    {
        std::unique_lock<std::mutex> lock(receiver_mutex_);
        instance_count_.fetch_sub(1, std::memory_order_relaxed);
        if (instance_count_.load(std::memory_order_relaxed) == 0)
        {
            receiver_.reset();
        }
    }
    MacPinger(int16_t id, int16_t sequence)
    {
        id_ = id;
        sequence_ = sequence;
        if (!receiver_)
        {
            receiver_ = std::make_shared<PingReceiver>();
        }
        instance_count_.fetch_add(1, std::memory_order_relaxed);
    }
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

        sockfd = receiver_->sockfd();

        struct icmp icmp_hdr;
        icmp_hdr.icmp_type = ICMP_ECHO;
        icmp_hdr.icmp_code = 0;
        icmp_hdr.icmp_id = htons(id_);
        icmp_hdr.icmp_seq = htons(sequence_);
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

        // ... (rest of the send_ping implementation)

        ssize_t bytes_sent = sendto(sockfd, packet, sizeof(packet), 0, res->ai_addr, res->ai_addrlen);
        if (bytes_sent < 0)
        {
            freeaddrinfo(res);
            return false;
        }

        auto timeSent = utime();
        auto timeout = std::chrono::seconds(timeoutSecond); // Set the timeout duration

        std::shared_ptr<std::vector<char>> response;

        {
            std::unique_lock<std::mutex> lock(receiver_->mutex_);
            auto bRes = receiver_->cv_.wait_for(lock, timeout, [this, &response]() {
                response = receiver_->get_response(id_, sequence_);
                return response != nullptr;
            });
            // timeout
            if (!bRes)
            {
                std::cout << "timeout" << std::endl;
                freeaddrinfo(res);
                packet_loss_number++;
                return false;
            }
        }

        if (!response)
        {
            freeaddrinfo(res);
            return false;
        }

        auto timeRecv = utime();

        struct icmp *icmp_reply = (struct icmp *)(response->data() + 20);
        int reply_id = ntohs(icmp_reply->icmp_id);
        int reply_seq = ntohs(icmp_reply->icmp_seq);
        int request_id = ntohs(icmp_hdr.icmp_id);
        int request_seq = ntohs(icmp_hdr.icmp_seq);
        if (reply_id != request_id || reply_seq != request_seq)
        {
            std::cout << "id error" << std::endl;
            receiver_->remove_response(id_, sequence_);
            freeaddrinfo(res);
            return false;
        }
        if (icmp_reply->icmp_type == ICMP_ECHOREPLY)
        {
            double round_trip_time_ms = (timeRecv - timeSent) / 1000.0;
            if(round_trip_time_ms/1000 < timeoutSecond)
            {
                num_replies_++;
                total_time_ += round_trip_time_ms;
            }
            else
            {
                packet_loss_number++;
            }
        }

        receiver_->remove_response(id_, sequence_);
        freeaddrinfo(res);
        return true;
    }
    double get_average_delay() const override
    {
        return num_replies_ > 0 ? total_time_ / num_replies_ : 0;
    }
    double get_packet_loss_rate() const
    {
        return (num_replies_ + packet_loss_number) ==0? 0: packet_loss_number/(num_replies_ + packet_loss_number);
    }
    void Reset() override
    {
        num_replies_ = 0;
        total_time_ = 0;
    }

  private:
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
    int16_t id_;
    int16_t sequence_;
    int num_replies_ = 0;
    double total_time_ = 0;
    int packet_loss_number = 0;
    static std::shared_ptr<PingReceiver> receiver_;
    static std::mutex receiver_mutex_;
    static std::atomic<int> instance_count_;
};
#endif
}; // namespace zzj
