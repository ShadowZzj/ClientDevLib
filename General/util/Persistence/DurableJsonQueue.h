#pragma once
#include <General/util/Json/Json.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace zzj::persistence {
struct QueuedJson { std::string key; zzj::json::Json document; };
// Durable bounded outbox: acknowledgement removes only explicitly delivered IDs.
class DurableJsonQueue final {
  public:
    explicit DurableJsonQueue(const std::filesystem::path&, std::size_t capacity = 8192);
    ~DurableJsonQueue();
    DurableJsonQueue(const DurableJsonQueue&) = delete;
    DurableJsonQueue& operator=(const DurableJsonQueue&) = delete;
    void enqueue(const std::string& key, const zzj::json::Json&);
    std::vector<QueuedJson> batch(std::size_t limit = 100);
    void acknowledge(const std::vector<std::string>& keys);
    std::size_t size();
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
