# Native Windows sockets with Qt events

`NativeTcpStream` provides a Windows Winsock TCP byte stream driven by `WSAEventSelect` and `QWinEventNotifier`. All operations belong on its QObject thread. Adopt the raw descriptor supplied by a `QTcpServer::incomingConnection` override; adoption consumes ownership even when setup fails. Do not construct a QTcpSocket and detach or duplicate it.

Receive FIN emits `readChannelFinished` while preserving the send half. `flushWriteClose` sends FIN after pending writes drain. Final `disconnected` occurs when both halves and their queues are complete. The implementation bounds its receive queue to 64 KiB and write queue to 1 MiB; write overflow closes with an error. Consumers should use their own bounded framing/session flow control as well.

`setRedirectRecords` retains opaque WFP records for a proxied outbound connection and applies them before native connect. Leave it empty for ordinary connections. `peerAddress` / `peerPort` cache `getpeername` after connection so authorization can check the actual connected address.

`NativeTcpListener` extends QTcpServer with exclusive native port ownership and a dual-stack IPv6 listener. This supports WFP redirection of applications explicitly bound to a local NIC address and V6-layer IPv4-mapped connections.

The reusable `Tests/NativeTcpStreamTests.cpp` tests live IPv4 and dual-stack request/FIN/response behavior beyond the read-queue bound, exclusive port ownership, peer address, refusal and write-queue overflow. This tests the native stream without loading a network driver.

`NativeDatagramSocket` owns an exclusively bound native UDP socket and provides `readyRead` / `writeAvailable` events through `WSAEventSelect` and `QWinEventNotifier`. Consumers use the existing `ReceiveRedirectedDatagram` / `SendRedirectedDatagram` helpers on its full-width descriptor to preserve WFP ancillary metadata. Drain native receive until `WSAEWOULDBLOCK`, or consume a bounded burst and allow the native event to reschedule. A zero-length payload is a valid datagram. Do not adopt the descriptor into QUdpSocket: native receive would bypass Qt's pending-datagram notification state and can disable subsequent notifications.

UDP redirect receivers must explicitly call `RedirectMetadata::EnableDatagramMetadata(socket, AF_INET/AF_INET6)` before receiving any redirected packet. Windows leaves redirect records/context socket options disabled by default. The helper enables both IPv4 and IPv6 protocol levels for a dual-stack socket and returns a Winsock error on failure; close that socket if setup fails. Ordinary `NativeDatagramSocket` users do not opt in automatically.
With opt-in enabled, Windows also returns empty records/context headers for ordinary UDP packets. The shared parser preserves such packets with empty metadata and still rejects duplicate headers. Redirect proxies must validate that the first protected datagram contains the expected nonempty metadata.

`ClientDevLib/tests/NativeDatagramSocketTests.cpp` verifies actual IPv4 and dual-stack native receives across two separate packet waves, including empty datagrams, exclusive ownership and closing from a synchronous notification. It also reads back both protocol levels' WFP options before and after explicit opt-in. It tests the notifier and shared Winsock helpers without loading the driver; WFP-generated context and reverse redirection require a signed-driver VM acceptance test.

`NativeSocketAddress.h` converts numeric Qt addresses to native storage and compares full endpoint identities, including IPv4-mapped normalization and IPv6 interface scopes. The UDP tests also cover strict port, scope and truncated-sockaddr rejection.
