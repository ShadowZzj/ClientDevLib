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
// Arming: we only start counting after BOTH (a) a character is in-game
// (UserConfig::IsReady() — we've locked onto a name) AND (b) the FIRST
// non-channel recv has arrived. Before login/char-select, recv silence on the
// game ports is normal, so the watchdog stays disarmed and never closes us
// while no character is loaded. That also avoids false positives from the
// channel/login chatter on 3000.

void Install();
void Uninstall();

// Exposes watchdog teardown state so status consumers do not report an
// unreadable local player as dead while the client is closing.
bool    IsDisconnectCloseTriggered();
int64_t GetWorldRecvIdleMs();

// Called from NetLog's recv hook on every completed recv. port is host-order.
// Zero-byte recvs and the channel/login port (3000) are filtered internally;
// anything else counts as a keep-alive.
void OnRecv(uint16_t port, uint32_t bytes);
} // namespace GGTB::DisconnectWatchdog
