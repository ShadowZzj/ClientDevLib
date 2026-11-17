#include <Windows/util/Network/Wfp/WfpEngine.h>
#include <Windows/util/Network/Wfp/RedirectMetadata.h>
#include <Windows/util/IPC/NamedPipeSecurity.h>
#include <mstcpip.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{
std::size_t checks = 0;
#define CHECK(expression) do { ++checks; if (!(expression)) { std::cerr << "Failed line " << __LINE__ << ": " \
    << #expression << '\n'; std::exit(1); } } while (false)
void AncillaryTests()
{
    using zzj::network::wfp::RedirectMetadata;
    RedirectMetadata metadata;
    WSAMSG message{};
    CHECK(RedirectMetadata::FromDatagram(message, metadata) == 0);
    message.dwFlags = MSG_CTRUNC;
    CHECK(RedirectMetadata::FromDatagram(message, metadata) == WSAEMSGSIZE);
    message.dwFlags = 0;
    message.Control.len = 10;
    CHECK(RedirectMetadata::FromDatagram(message, metadata) == WSAEINVAL);
    std::vector<char> control(WSA_CMSG_SPACE(3), 0);
    auto* header = reinterpret_cast<WSACMSGHDR*>(control.data());
    header->cmsg_len = WSA_CMSG_LEN(3);
    header->cmsg_level = IPPROTO_IP;
    header->cmsg_type = IP_WFP_REDIRECT_RECORDS;
    std::memcpy(WSA_CMSG_DATA(header), "abc", 3);
    message.Control.buf = control.data();
    message.Control.len = static_cast<ULONG>(control.size());
    CHECK(RedirectMetadata::FromDatagram(message, metadata) == 0);
    CHECK(metadata.records == std::vector<std::uint8_t>({'a', 'b', 'c'}) && metadata.context.empty());
    header->cmsg_len = control.size() + 1;
    CHECK(RedirectMetadata::FromDatagram(message, metadata) == WSAEINVAL && metadata.records.empty());
    // Actual Windows ordinary UDP carries both empty headers when opted in.
    const int protocolLevels[]{IPPROTO_IP, IPPROTO_IPV6};
    for (int level : protocolLevels)
    {
        std::vector<char> emptyControl(2 * WSA_CMSG_SPACE(0), 0);
        auto* records = reinterpret_cast<WSACMSGHDR*>(emptyControl.data());
        auto* context = reinterpret_cast<WSACMSGHDR*>(emptyControl.data() + WSA_CMSG_SPACE(0));
        records->cmsg_len = context->cmsg_len = WSA_CMSG_LEN(0);
        records->cmsg_level = context->cmsg_level = level;
        records->cmsg_type = level == IPPROTO_IP ? IP_WFP_REDIRECT_RECORDS : IPV6_WFP_REDIRECT_RECORDS;
        context->cmsg_type = level == IPPROTO_IP ? IP_WFP_REDIRECT_CONTEXT : IPV6_WFP_REDIRECT_CONTEXT;
        message.Control.buf = emptyControl.data();
        message.Control.len = static_cast<ULONG>(emptyControl.size());
        CHECK(RedirectMetadata::FromDatagram(message, metadata) == 0 && metadata.records.empty() && metadata.context.empty());
        context->cmsg_type = records->cmsg_type;
        CHECK(RedirectMetadata::FromDatagram(message, metadata) == WSAEINVAL);
    }
}
void UdpTests()
{
    using namespace zzj::network::wfp;
    WSADATA winsock{};
    CHECK(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
    const SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const SOCKET receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    CHECK(sender != INVALID_SOCKET && receiver != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(receiver, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    int addressLength = sizeof(address);
    CHECK(getsockname(receiver, reinterpret_cast<sockaddr*>(&address), &addressLength) == 0);
    DWORD timeout = 3000;
    CHECK(setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0);
    CHECK(RedirectMetadata::EnableDatagramMetadata(receiver, AF_INET) == 0);
    const std::uint8_t payload[]{1, 2, 3, 4};
    DWORD sent = 0;
    CHECK(SendRedirectedDatagram(sender, reinterpret_cast<sockaddr*>(&address), sizeof(address),
                                payload, sizeof(payload), {}, sent) == 0 && sent == sizeof(payload));
    std::uint8_t incoming[32]{};
    ReceivedDatagram datagram;
    CHECK(ReceiveRedirectedDatagram(receiver, incoming, sizeof(incoming), datagram) == 0);
    CHECK(datagram.bytes == sizeof(payload) && std::memcmp(incoming, payload, sizeof(payload)) == 0);
    CHECK(datagram.redirect.context.empty() && datagram.redirect.records.empty());
    CHECK(RedirectMetadata::ApplyRecords(INVALID_SOCKET, {}) == WSAEINVAL);
    closesocket(sender);
    closesocket(receiver);
    WSACleanup();
}
void SecurityTests()
{
    auto users = zzj::ipc::NamedPipeSecurity::ForLocalUsers();
    CHECK(users && users.Attributes() && !users.Attributes()->bInheritHandle);
    auto admin = zzj::ipc::NamedPipeSecurity::ForAdministrators();
    CHECK(admin);
    auto moved = std::move(admin);
    CHECK(moved && !admin);
    CHECK(!zzj::ipc::NamedPipeSecurity::FromSddl(L"invalid"));
    DWORD error = 0;
    CHECK(!zzj::ipc::IsNamedPipeClientAdministrator(INVALID_HANDLE_VALUE, &error) && error != ERROR_SUCCESS);
    CHECK(!zzj::ipc::EnsureLocalClient(INVALID_HANDLE_VALUE, &error));
    zzj::network::wfp::Engine engine;
    zzj::network::wfp::Transaction transaction(engine);
    CHECK(transaction.Status() == ERROR_INVALID_HANDLE && !transaction.Active());
    CHECK(transaction.Commit() == ERROR_INVALID_HANDLE);
}
void PipeIntegrationTests()
{
    auto security = zzj::ipc::NamedPipeSecurity::ForLocalUsers();
    const std::wstring name = L"ClientDevLib.network.tests." + std::to_wstring(GetCurrentProcessId()) + L"." +
                             std::to_wstring(GetTickCount64());
    const std::wstring path = L"\\\\.\\pipe\\" + name;
    HANDLE server = CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                                     FILE_FLAG_FIRST_PIPE_INSTANCE,
                                     PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                     1, 512, 512, 0, security.Attributes());
    CHECK(server != INVALID_HANDLE_VALUE);
    DWORD error = 0;
    HANDLE client = zzj::ipc::OpenNamedPipeClient(name, 500, &error);
    CHECK(client != INVALID_HANDLE_VALUE && error == ERROR_SUCCESS);
    CHECK(ConnectNamedPipe(server, nullptr) == FALSE && GetLastError() == ERROR_PIPE_CONNECTED);
    CHECK(zzj::ipc::EnsureLocalClient(server, &error));
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof(sid);
    BOOL administrator = FALSE;
    CHECK(CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sid, &sidSize));
    CHECK(CheckTokenMembership(nullptr, sid, &administrator));
    CHECK(zzj::ipc::IsNamedPipeServerPrivileged(client, &error) == (administrator != FALSE));
    CHECK(administrator || error == ERROR_ACCESS_DENIED);

    // Read a real message before impersonation. The server then authorizes the
    // actual client's enabled groups and returns to an unimpersonated thread.
    OVERLAPPED writeOperation{}, readOperation{};
    writeOperation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    readOperation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CHECK(writeOperation.hEvent && readOperation.hEvent);
    DWORD transferred = 0;
    if (!WriteFile(client, "status", 6, &transferred, &writeOperation))
    {
        CHECK(GetLastError() == ERROR_IO_PENDING);
        CHECK(WaitForSingleObject(writeOperation.hEvent, 1000) == WAIT_OBJECT_0);
        CHECK(GetOverlappedResult(client, &writeOperation, &transferred, FALSE));
    }
    CHECK(transferred == 6);
    char request[16]{};
    if (!ReadFile(server, request, sizeof(request), &transferred, &readOperation))
    {
        CHECK(GetLastError() == ERROR_IO_PENDING);
        CHECK(WaitForSingleObject(readOperation.hEvent, 1000) == WAIT_OBJECT_0);
        CHECK(GetOverlappedResult(server, &readOperation, &transferred, FALSE));
    }
    CHECK(transferred == 6 && std::memcmp(request, "status", 6) == 0);
    CHECK(zzj::ipc::IsNamedPipeClientAdministrator(server, &error) == (administrator != FALSE));
    HANDLE threadToken = nullptr;
    CHECK(!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &threadToken) && GetLastError() == ERROR_NO_TOKEN);
    CloseHandle(writeOperation.hEvent);
    CloseHandle(readOperation.hEvent);
    CloseHandle(client);
    CloseHandle(server);

    CHECK(zzj::ipc::OpenNamedPipeClient(L"\\\\remote\\pipe\\evil", 10, &error) == INVALID_HANDLE_VALUE &&
          error == ERROR_INVALID_NAME);
    CHECK(zzj::ipc::OpenNamedPipeClient(name + L".absent", 20, &error) == INVALID_HANDLE_VALUE &&
          error == ERROR_SEM_TIMEOUT);
}
} // namespace
int main()
{
    AncillaryTests();
    UdpTests();
    SecurityTests();
    PipeIntegrationTests();
    std::cout << checks << " Windows network helper checks passed\n";
}
