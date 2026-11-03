# Reverse TCP relay

`ReverseRelayServer` exposes two TCP listeners: public ingress for opaque client
bytes and a mutual-TLS reverse listener. `ReverseRelayClient` actively dials the
reverse listener and connects each accepted stream to its configured, fixed
numeric backend. The connector does not expose a reverse listener. The backend
address is never supplied by an Open frame.

The relay has no product policy, DNS, target authorization or UDP implementation.
An existing TLS gateway can be the connector's private backend; the client's TLS
handshake and application frames pass through POP unchanged. This preserves the
gateway's client authentication and destination authorization. To move POP or
connector to another machine, change bind/dial addresses and preserve the CA,
certificate identity and connector pin. A private gateway backend should bind
loopback.

## API and authentication

Include `General/util/Network/Qt/ReverseRelay.h` and link
`ClientDevLib::QtNetwork`. All API calls and callbacks belong to the object's Qt
event-loop thread. Supply `QSslConfiguration` from the existing
`loadMutualTls(TlsFiles)` helper; both roles force `VerifyPeer` and TLS 1.2 or
newer. TLS errors are never ignored.

The reverse TLS socket explicitly uses `NoProxy`: its configured POP endpoint is
the transport route, independent of the host's application/system HTTP proxy.
This also prevents an inherited caching-only proxy from rejecting the raw TCP
reverse connection. Authentication and certificate-name checks remain required.

The POP requires a trusted connector certificate and an exact, **raw 32-byte**
SHA-256 DER certificate digest in `peerCertificateSha256`. The first frame binds
protocol version 1 and `peerId`; a correct CA without the configured pin or ID
cannot create a ready peer. The connector verifies the POP's chain and
`peerName` before sending authentication. No additional bearer secret is used.

The POP admits at most four pending TLS/authentication candidates. Unauthenticated
TCP sockets cannot reserve the single active slot; the first candidate to pass
TLS, the certificate pin and Auth activates it and closes the other candidates.
When an authenticated peer is active, additional connections are closed without
replacing that peer. An ingress
socket accepted without an authenticated peer is immediately closed; there is no
direct fallback. Reverse disconnection closes every associated ingress/backend.

`start()` validates settings and binds listeners, or schedules the initial dial.
For a server, zero ports request ephemeral test ports exposed through getters.
For a client, the POP and backend ports must be nonzero. `ready()` reports the
authenticated reverse link; it does not claim that the separate backend gateway
or its policy is healthy. `stop()` closes listeners/link/streams and cancels
reconnects. Objects may be restarted with new settings; counters reset on start.

## Flow control and FIN

The reverse link uses the existing `FramedChannel`, with independent message
types for Auth/AuthOk/Open/OpenOk/Data/Fin/Reset/Window/Ping/Pong. Every session
allocates consecutive stream IDs starting at 1; a reused, out-of-order or future
unknown ID is a protocol error. Frames crossing a reset for an already closed
past ID are ignored, and that ID can never be reused.

Limits are fixed and shared by both roles:

- At most 128 active streams; each Data frame is at most 16 KiB.
- Each direction has a 64 KiB receive credit window per stream. Credit is
  replenished only when the local transport reports bytes written. A stalled
  backend therefore stops peer reads rather than accumulating its data forever.
- Windows uses existing `NativeTcpStream`, with its 64 KiB input queue; a relay
  never reads that queue without reverse write capacity and stream credit.
- `FramedChannel` bounds the shared TLS queue at 4 MiB and reserves space for
  TLS overhead. Data additionally leaves 64 KiB for control progress; controls
  have a separate 1,024-message cap. Over-limit or malformed peers are closed.
- A single pump visits all active streams before repeating and rotates the
  initial stream, so siblings get opportunities under shared backpressure.
- Authentication and backend-open deadlines are 10 seconds. Heartbeats run
  every 5 seconds and reject a reverse link with no received frame for 30
  seconds. A started connector retries failed links with a 250 ms exponential
  delay capped at 8 seconds, indefinitely until stopped.

Windows uses native TCP half-close for ingress and backend streams: a local EOF
sends Fin after its remaining bytes, while peer Fin drains already queued local
writes before closing only the send half. The other direction remains usable.
The non-Windows Qt fallback compiles but does not provide the Windows native
transport's guaranteed receive-half preservation after TCP FIN; full cross-platform
half-close requires an equivalent platform transport and its integration tests.

Events contain connection/stream identifiers, termination reasons and byte
accounting; application payload, keys, certificates and bearer credentials are
not logged. `RelayCounters` accumulates opened streams and bytes in each
direction, measured at framing acceptance/submission, not as proof that the
remote application's business operation completed.
