#include "Socket.h"
#ifdef _WIN32
#include <winsock2.h>
#include <Ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#endif
using namespace zzj;
TCPSocket::TCPSocket()
{
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
        return;
#endif
    m_socket = socket(AF_INET, SOCK_STREAM, 0);
}
TCPSocket::~TCPSocket()
{
    Close();
}
int TCPSocket::Connect(const std::string &ip, int port)
{
    sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    return connect(m_socket, (sockaddr *)&addr, sizeof(addr));
}
int TCPSocket::Send(const std::string &data)
{
    return send(m_socket, data.c_str(), data.size(), 0);
}
int TCPSocket::Recv(std::string &data, int len)
{
    char *buf = new char[len];
    int ret   = recv(m_socket, buf, len, 0);
    if (ret > 0)
        data = std::string(buf, ret);
    delete[] buf;
    return ret;
}
bool TCPSocket::Close()
{
#ifdef _WIN32
    WSACleanup();
    return closesocket(m_socket) == 0;
#else
    return close(m_socket);
#endif
}
UDPSocket::UDPSocket()
{
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
        return;
#endif
    m_socket = socket(AF_INET, SOCK_DGRAM, 0);
}
UDPSocket::~UDPSocket()
{
    Close();
}
bool UDPSocket::Close()
{
#ifdef _WIN32
    WSACleanup();
    return closesocket(m_socket) == 0;
#else
    return close(m_socket);
#endif
    
}

int UDPSocket::Sendto(const std::string &data, const std::string &ip, int port)
{
    sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    int result = sendto(m_socket, data.c_str(), data.size(), 0, (sockaddr *)&addr, sizeof(addr));
    return result;
}
int UDPSocket::Recvfrom(std::string &data, std::string &ip, int &port,int len)
{
    sockaddr_in addr;
#ifdef _WIN32
    int addr_len = sizeof(addr);
#else
    socklen_t addr_len = sizeof(addr);
#endif
    char *buf    = new char[len];
    int ret      = recvfrom(m_socket, buf, len, 0, (sockaddr *)&addr, &addr_len);
    if (ret > 0)
    {
        data = std::string(buf, ret);
        ip   = inet_ntoa(addr.sin_addr);
        port = ntohs(addr.sin_port);
    }
    delete[] buf;
    return ret;
}
