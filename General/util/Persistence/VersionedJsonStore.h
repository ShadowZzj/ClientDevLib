#pragma once
#include <General/util/Json/Json.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zzj::persistence {
struct VersionedJsonDocument {
    std::string key;
    std::uint64_t generation{};
    zzj::json::Json document;
    std::string updatedAt;
};
class VersionConflict : public std::runtime_error {
  public:
    explicit VersionConflict(std::uint64_t currentGeneration);
    std::uint64_t currentGeneration() const noexcept { return currentGeneration_; }
  private:
    std::uint64_t currentGeneration_;
};

// A per-file SQLite store. compareExchange reserves the database write lock before
// reading the current revision, so separate processes cannot publish the same revision.
class VersionedJsonStore {
  public:
    static constexpr std::uint64_t MaximumGeneration = 9007199254740991ULL;
    explicit VersionedJsonStore(const std::filesystem::path& databasePath);
    ~VersionedJsonStore();
    VersionedJsonStore(const VersionedJsonStore&) = delete;
    VersionedJsonStore& operator=(const VersionedJsonStore&) = delete;
    std::optional<VersionedJsonDocument> get(const std::string& key);
    std::vector<VersionedJsonDocument> list();
    std::vector<VersionedJsonDocument> history(const std::string& key);
    VersionedJsonDocument compareExchange(const std::string& key, std::uint64_t expectedGeneration,
        const std::function<zzj::json::Json(std::uint64_t)>& createDocument, const std::string& updatedAt);
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace zzj::persistence
