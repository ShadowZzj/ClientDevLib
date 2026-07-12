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
//     read config.json (manualWhitelist + activeProfile), load the active profile
//     and call LoadState on each module, set ready (locked for the process life).
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

// Mark the per-character meta (manualWhitelist + activeProfile) as dirty so
// Tick() flushes config.json ~1s later. Module states are NOT auto-saved — they
// only hit disk via SaveProfile (explicit save). Whitelist add/remove and
// profile switches call this.
void MarkDirty();

// Force-flush config.json meta (manualWhitelist + activeProfile). Call once
// before Setting::End(). Does NOT write module states — those live in the
// active profile and are only persisted on explicit SaveProfile.
void FlushConfigMeta(Setting *setting);

// ----- named config profiles (per-character) -----
// Profiles live in <userdir>/profiles/<name>.json and hold only module states
// ({version, modules}). config.json holds the per-character meta: manualWhitelist
// and activeProfile. Module states reach disk ONLY through SaveProfile — never
// auto-flushed — matching the "仅点保存时写入" requirement. All ops are no-ops
// before IsReady(); they lock internally and are safe to call from the UI thread.

std::vector<std::string> ListProfiles();      // sorted UTF-8 names, no extension
std::string              ActiveProfileName();  // "" before ready

// Write current module states to profiles/<name>.json and make it active.
// Used for both "保存"(name == active) and "新建"(name is new). Returns false on
// invalid name or write failure.
bool SaveProfile(Setting *setting, const std::string &name);

// Apply profiles/<name>.json to all modules (LoadState) and make it active.
// Returns false if the profile file does not exist.
bool LoadProfile(Setting *setting, const std::string &name);

// Rename / delete a profile file. Rename refuses to clobber an existing target.
// Deleting the active profile re-points active at another profile (or "默认").
bool RenameProfile(Setting *setting, const std::string &from, const std::string &to);
bool DeleteProfile(Setting *setting, const std::string &name);

// Bumped whenever module states are (re)loaded from disk (login / LoadProfile)
// or the active profile / profile list changes (SaveProfile / Rename / Delete).
// The UI watches this to refresh its profile list and re-baseline its
// "unsaved changes" snapshot. Starts at 0 (never loaded).
uint64_t ConfigGeneration();
} // namespace UserConfig
} // namespace GGTB
