#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>
#include <vector>

// nlohmann::json forward declaration would require <json_fwd.hpp> from the
// vendored copy; modules already pull <json.hpp> wholesale, so just include it.
#include <json.hpp>

namespace GGTB
{
class Setting;

namespace UserConfig
{
// One-time bootstrap. Call from HackThread (NOT DllMain — file IO under loader
// lock + spdlog registry mutex is asking for a deadlock). Sets the default
// spdlog logger to <dll-dir>/GGConfig/_bootstrap/bootstrap.log so the early
// HackThread / PatternResolver phase has somewhere to write.
void Bootstrap(HMODULE hOwnerDll);

// Drive from HackThread main loop (~every 100ms, cheap when ready).
// State machine:
//   - already ready  -> handle pending debounced save and return
//   - setting->IsInitialized() == false -> return (modules' OnInit must have run
//     before LoadState pokes them; Setting::Init runs lazily on first EndScene)
//   - GetLocalPlayerName() empty -> return
//   - sanitize name, mkdir GGConfig/<name>/, swap logger to <name>/ggtb.log,
//     read config.json (manualWhitelist + module states), call LoadState on each
//     module, set ready (locked for the rest of the process lifetime).
void Tick(Setting *setting);

bool                  IsReady();
std::string           CurrentName();   // UTF-8 sanitised character name
std::filesystem::path UserDir();       // <dll-dir>/GGConfig/<name>/  (or _bootstrap before ready)

// Whitelist — UTF-8, matches Big5ToUtf8() output from CLocalPlayer.cpp so
// GetAroundPlayers can compare directly.
bool                     IsWhitelisted(const std::string &utf8name);
std::vector<std::string> GetWhitelist();
void                     AddWhitelist(const std::string &utf8name);
void                     RemoveWhitelist(const std::string &utf8name);

// Mark the in-memory module state as dirty. UI thread calls this when it
// notices a module's serialized form changed; Tick() flushes ~1s later to
// avoid one-write-per-frame while a slider is being dragged.
void MarkDirty();

// Force-flush. Call once before Setting::End() — OnShutdown might reset
// enabled_/slider values, and we want to persist what the user actually had
// checked, not the post-shutdown state.
void SaveModuleStates(Setting *setting);
} // namespace UserConfig
} // namespace GGTB
