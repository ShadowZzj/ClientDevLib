#pragma once

namespace GGTB::EnchantStoneControl
{
// Registers bounded, single-step broker commands. No worker is created here;
// the broker owns the long-running state machine and calls one action at a time.
void RegisterHandlers();
} // namespace GGTB::EnchantStoneControl
