#pragma once
#include <string>
namespace zzj
{
class TCPSocket
{
  public:
    TCPSocket();
    ~TCPSocket();
    int Connect(const std::string &ip, int port);
    int Send(const std::string &data);
    int Recv(std::string &data, int len);
    bool Close();

  private:
    int m_socket;
};
class UDPSocket
{
  public:
    UDPSocket();
    ~UDPSocket();
    int Sendto(const std::string &data, const std::string &ip, int port);
    int Recvfrom(std::string &data, std::string &ip, int &port, int len);
    bool Close();

  private:
    int m_socket;
};
}; // namespace zzj