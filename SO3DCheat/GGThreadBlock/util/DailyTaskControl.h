#pragma once

namespace GGTB::DailyTaskControl
{
// Registers bounded daily-task query/reroll/accept/finish commands.  The
// broker owns the long-running policy loop; each mutating command performs at
// most one faithful local UI action and waits for one server outcome.
void RegisterHandlers();
}  // namespace GGTB::DailyTaskControl
