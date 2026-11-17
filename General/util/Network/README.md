# Independent network modules

These modules reuse ClientDevLib's existing repository and naming conventions,
but can be built without the legacy feature-selection graph or its Boost,
curl, GUI, injection, and debugging dependencies. Existing `DNSHelper.h`,
packet header structs, socket wrappers, and legacy IPC classes are unchanged.
The new wire parser is needed because the legacy DNS API exposes structures
and a system A-record resolver, not a length-checked DNS message parser.

## CMake integration

```cmake
include("${CLIENT_DEV_LIB_ROOT}/cmake/ClientNetwork.cmake")
target_link_libraries(my_policy_tool PRIVATE ClientDevLib::Network)
if(WIN32)
    target_link_libraries(my_windows_service PRIVATE ClientDevLib::WindowsNetwork)
endif()
```

Both targets publish the ClientDevLib root include directory and require C++17.
`Network` is portable standard C++; `WindowsNetwork` additionally links the
Windows SDK's `fwpuclnt`, `ws2_32`, and `advapi32` libraries. Caller code must
initialize Winsock before using native socket helpers. No global build flags
are changed. Qt adaptations are provided separately by `ClientQtNetwork.cmake`.

## Portable APIs (`zzj::network`)

| Header | Responsibility |
| --- | --- |
| `General/util/Network/DomainName.h` | ASCII/IDNA A-label normalization and exact/leading-wildcard hostname matching |
| `General/util/Network/IpAddress.h` | Strict IPv4/IPv6 parsing, canonical formatting, CIDR normalization and prefix matching |
| `General/util/Network/DnsWire.h` | Single-question DNS query parsing and synthetic A/NODATA/error responses |

Wildcard `*.example.com` requires a label boundary and matches nested
subdomains, but never the apex. Domain normalization rejects Unicode, invalid
labels, empty labels, and names exceeding DNS hostname limits. Convert Unicode
to IDNA A-labels before invoking the API. DNS service question labels can
contain underscores, while hostname policy normalization intentionally cannot.

IPv4 parsing rejects ambiguous leading zeros, abbreviated forms, and trailing
input. IPv6 supports compressed and embedded IPv4 forms; zone IDs and bracketed
URI syntax are intentionally outside the address parser. A CIDR parser requires
an explicit prefix and clears host bits. Address families are never silently
converted while checking a network.

`DnsWire::ParseQuery` checks packet bounds, question count, standard opcode,
name compression loops, label/name lengths, record lengths, and trailing input.
Compressed input question names are rebuilt uncompressed for responses, so a
pointer into a discarded EDNS section cannot survive into an invalid answer.
The input is DNS message bytes without the two-byte TCP length prefix. A fake
A response is permitted only for IN/A questions. Responses retain ID/RD/CD,
set QR/RA, and clear AD because synthetic results are not DNSSEC-signed. They
omit EDNS and SOA records. An error reply to malformed input contains only a
bounded header and an ID if enough input bytes exist. This is deliberately a
resolver proxy helper rather than a complete authoritative DNS server.

`BuildQuery` creates a bounded IN query without platform DNS APIs.
`ValidateResponse` checks the original ID, decoded question, header and all
record bounds before a response is used. Truncated UDP responses can be
recognized explicitly for TCP fallback but are not accepted as complete
answers. `ExtractARecords` only accepts answer-section A records reachable from
the original question through a validated CNAME chain. It rejects loops and
ambiguous owners and ignores unrelated/additional A records; a CNAME-only
result identifies the final name for a caller's bounded follow-up query.

## Windows WFP APIs (`zzj::network::wfp`)

`Windows/util/Network/Wfp/WfpEngine.h` provides an `Engine` owning a dynamic
BFE session and a `Transaction` which aborts unless committed. The engine must
outlive its transaction. Errors are explicit Win32/WFP status values. Dynamic
session objects are removed when the engine closes or its process terminates;
products requiring persistent enforcement must implement that lifecycle
explicitly instead of assuming this session survives process crashes.

`Windows/util/Network/Wfp/RedirectMetadata.h` keeps application redirect context
separate from opaque WFP records. For TCP, query an accepted redirected socket
and set its records on the outbound socket before connecting. For UDP, use
`ReceiveRedirectedDatagram`/`SendRedirectedDatagram`, which use native
`WSARecvMsg`/`WSASendMsg` and retain full-width `SOCKET` handles. UDP redirect
metadata may appear only on a flow's first datagram; the product must retain it
in the per-flow session. Ancillary parsing validates lengths, truncation,
duplicate metadata, and alignment before copying. A normal UDP listener is not
a TCP-style accepted socket and cannot be treated as one.

These wrappers manage WFP object and transport mechanics. They do not contain
resource identifiers, tunnel decisions, login, or other zero-trust policy.
Driver/service redirect context is a separate versioned ABI owned by the driver
library and product integration.

## Named pipe security (`zzj::ipc`)

`Windows/util/IPC/NamedPipeSecurity.h` builds protected, non-inheritable DACLs.
`ForAdministrators()` permits SYSTEM and enabled Administrators.
`ForLocalUsers()` additionally permits authenticated-user communication with
specific rights `0x0012019b`; it excludes `FILE_CREATE_PIPE_INSTANCE`.
Use `FILE_FLAG_FIRST_PIPE_INSTANCE`, `PIPE_REJECT_REMOTE_CLIENTS`, and
`FILE_FLAG_OVERLAPPED` when creating a service control pipe. A secure descriptor
must be applied at creation, before any connection can be accepted.

`OpenNamedPipeClient` requests those explicit rights and returns an overlapped,
caller-owned handle with bounded connection retries. A `GENERIC_WRITE` client
would also request the server-instance right and is therefore incompatible with
this restricted DACL. Framework adapters should adopt the returned handle.
`IsNamedPipeServerPrivileged` rejects an unprivileged process that pre-created
the expected endpoint. It trusts SYSTEM or an enabled Administrators group; a
UAC-filtered administrator token does not pass.

Service listeners must run under SYSTEM or an enabled administrator token to
create further instances under this DACL. An unprivileged diagnostic foreground
process should omit the control pipe explicitly rather than weakening the ACL.
`IsEffectiveAdministrator` provides the corresponding effective-token check.

After reading each mutating command, call `IsNamedPipeClientAdministrator` on
that connection. It checks the actual impersonation token and returns to the
unimpersonated thread before returning. It rejects a caller with an existing
thread impersonation token rather than discarding that identity. Failure to
revert impersonation terminates, because continuing would execute unrelated
service work with an unexpected authority. `EnsureLocalClient` verifies both
client PID and NPFS local-client status; server-side remote rejection is still
required. Read-only commands may use their own weaker authorization policy.

## Verification

```powershell
cmake -S Tests/network -B Tests/network/build -G "Visual Studio 18 2026" -A x64
cmake --build Tests/network/build --config Debug --parallel
ctest --test-dir Tests/network/build -C Debug --output-on-failure
```

Parser tests cover hostname boundaries, IPv4/IPv6/CIDR edge cases, DNS
compression loops/truncation/EDNS bounds, response flags/IDs, and 10,000
deterministic malformed packets. Windows tests exercise native UDP loopback,
ancillary bounds, restricted named-pipe connection/read/write, client token
authorization, server token verification, impersonation restoration, and finite
timeouts. The independent test targets compile with `/W4 /WX` on MSVC.

These tests do not load a kernel driver or prove real WFP interception. BFE
filter registration, kernel redirection, tunnel forwarding, coexistence with
other proxies, and Windows service lifecycle require product integration tests.

References: [WFP redirect records](https://learn.microsoft.com/en-us/windows/win32/winsock/sio-query-wfp-connection-redirect-records),
[WFP transactions](https://learn.microsoft.com/en-us/windows/win32/api/fwpmu/nf-fwpmu-fwpmtransactionbegin0),
[named-pipe security](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights).
