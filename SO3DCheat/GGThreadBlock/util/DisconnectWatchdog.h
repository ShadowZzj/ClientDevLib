#pragma once
#include <Windows.h>
#include <cstdint>

namespace GGTB::DisconnectWatchdog
{
// Watches for recv traffic from any peer that isn't the channel/login port
// (3000). If 1 minute passes with no recv from a non-channel port AFTER the
// first in-game packet has been seen, assume the connection has silently died
// (server/NAT dropped us with no FIN, or the game stopped pumping its recv
// loop) and force-close the process.
//
// Close path tries, in order:
//   1) PostMessageW(WM_CLOSE) to the host's main top-level window. This is
//      the same route the red X button takes — hits the normal WM_CLOSE
//      branch in WindowProc so the game gets to flush caches / save state /
//      unhook cleanly.
//   2) If no main window can be found, or PostMessage fails, ExitProcess(0).
//
// Arming: we only start counting after the FIRST non-channel recv arrives.
// That avoids false positives during login (channel/login chatter on 3000
// keeps happening and would otherwise look like "still alive").

void Install();
void Uninstall();

// Called from NetLog's recv hook on every completed recv. port is host-order.
// Zero-byte recvs and the channel/login port (3000) are filtered internally;
// anything else counts as a keep-alive.
void OnRecv(uint16_t port, uint32_t bytes);
} // namespace GGTB::DisconnectWatchdog
