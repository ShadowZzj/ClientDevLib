#include <arpa/inet.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

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

    virtual bool send_ping(const std::string &destination) = 0;
    virtual double get_average_delay() const               = 0;
};

#ifdef _WIN32
// Windows specific implementation
// ...
#else
// macOS specific implementation
class MacPinger : public PingerInterface
{
  public:
    bool send_ping(const std::string &destination) override
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
            std::cerr << "Error creating socket" << std::endl;
            freeaddrinfo(res);
            return false;
        }
        // Set the timeout value
        struct timeval timeout;
        timeout.tv_sec = 2;  // Timeout in seconds
        timeout.tv_usec = 0; // Additional timeout in microseconds

        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
        {
            std::cerr << "Failed to set timeout: " << std::strerror(errno) << std::endl;
            close(sockfd);
            freeaddrinfo(res);
            return 1;
        }

        struct icmp icmp_hdr;
        icmp_hdr.icmp_type = ICMP_ECHO;
        icmp_hdr.icmp_code = 0;
        icmp_hdr.icmp_id = htons(getpid());
        icmp_hdr.icmp_seq = htons(1);
        icmp_hdr.icmp_cksum = 0;
        icmp_hdr.icmp_cksum = checksum(&icmp_hdr, sizeof(icmp_hdr));
        char buf[1024]{1};

        ssize_t bytes_sent = sendto(sockfd, &icmp_hdr, sizeof(icmp_hdr), 0, res->ai_addr, res->ai_addrlen);
        if (bytes_sent < 0)
        {
            std::cerr << "Error sending ICMP packet" << std::endl;
            close(sockfd);
            freeaddrinfo(res);
            return false;
        }

        auto timeSent = utime();
        ssize_t bytes_received = recv(sockfd, buf, sizeof(buf), 0);
        if (bytes_received < 0)
        {
            std::cerr << "Error receiving ICMP packet" << std::endl;
            close(sockfd);
            freeaddrinfo(res);
            return false;
        }
        std::cout << "recv: " << bytes_received << std::endl;
        auto timeRecv = utime();

        struct icmp *icmp_reply = (struct icmp *)(buf + 20); // Skip IP header
        int reply_id = ntohs(icmp_reply->icmp_id);
        int reply_seq = ntohs(icmp_reply->icmp_seq);
        if (reply_id != ntohs(icmp_hdr.icmp_id) || reply_seq != ntohs(icmp_hdr.icmp_seq))
        {
            std::cout<<"id error"<<std::endl;
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

  private:
    uint16_t checksum(void *data, int len)
    {
        uint16_t *addr = (uint16_t *)data;
        uint32_t sum = 0;
        while (len > 1)
        {
            sum += *addr++;
            len -= 2;
        }
        if (len > 0)
        {
            sum += *(uint8_t *)addr;
        }
        while (sum >> 16)
            sum = (sum & 0xffff) + (sum >> 16);

        return ~sum;
    }

  private:
    int num_replies_ = 0;
    double total_time_ = 0;
};
#endif