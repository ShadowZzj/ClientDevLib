# JsonPollingClient

`ClientDevLib::JsonPolling` supplies an asynchronous, single-flight JSON HTTP poller.
Include `cmake/ClientJsonPolling.cmake` and link the target. JSON uses the existing
`General/ThirdParty/json.hpp` through `zzj::json::Json`.

`start()` immediately makes a GET and then polls at the configured interval.
The consumer validates `documentReceived`, applies its data, and calls
`acknowledge(etag)` only after success. An acknowledged ETag enables 304 responses;
304 without an acknowledged document is rejected. `post()` sends a bounded JSON
object to the same scheme, host and port, and reports its success independently.
While either request is active, polling skips ticks. `stop()` aborts and detaches
the active reply and cancels both timers.

Bounds: interval 1–3600 seconds, timeout 100 ms–60 seconds, and maximum response
1 MiB. Redirects and proxies are disabled. TLS uses TLS 1.2 or later with normal
certificate and hostname verification; custom CA certificates may be supplied in
the settings. TLS errors are never ignored. HTTP permission, endpoint validation,
token-file loading and domain-specific persistence belong to the calling product.
Bearer tokens remain in memory and are omitted from errors; JSON parser failures
report a fixed message without response content.
