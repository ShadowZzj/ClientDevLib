#include "DurableJsonQueue.h"
#include <General/ThirdParty/sqlite3pp/sqlite3pp.h>
#include <mutex>
namespace zzj::persistence {
namespace {
void check(sqlite3pp::database& db, int result) {
    if (result != SQLITE_OK) throw sqlite3pp::database_error(db);
}
std::string filename(const std::filesystem::path& path) {
    if (path.empty()) throw std::invalid_argument("Queue database path is required");
    const auto absolute = std::filesystem::absolute(path);
    std::filesystem::create_directories(absolute.parent_path());
    return absolute.u8string();
}
void validate(const std::string& key) {
    if (key.empty() || key.size() > 128 || key.find('\0') != std::string::npos)
        throw std::invalid_argument("Invalid queue key");
}
}
struct DurableJsonQueue::Impl {
    Impl(const std::filesystem::path& path, std::size_t maximum) : db(filename(path).c_str()), capacity(maximum) {
        if (!capacity || capacity > 100000) throw std::invalid_argument("Invalid queue capacity");
        check(db, db.set_busy_timeout(5000));
        check(db, db.execute("PRAGMA journal_mode=WAL;"));
        check(db, db.execute("PRAGMA synchronous=FULL;"));
        check(db, db.execute("CREATE TABLE IF NOT EXISTS client_json_outbox ("
            "sequence INTEGER PRIMARY KEY AUTOINCREMENT, event_key TEXT NOT NULL UNIQUE, document_json TEXT NOT NULL);"));
    }
    std::size_t count() {
        sqlite3pp::query q(db, "SELECT count(*) FROM client_json_outbox;");
        return static_cast<std::size_t>((*q.begin()).get<long long>(0));
    }
    sqlite3pp::database db;
    std::mutex mutex;
    std::size_t capacity;
};
DurableJsonQueue::DurableJsonQueue(const std::filesystem::path& p, std::size_t capacity)
    : impl_(std::make_unique<Impl>(p, capacity)) {}
DurableJsonQueue::~DurableJsonQueue() = default;
void DurableJsonQueue::enqueue(const std::string& key, const zzj::json::Json& document) {
    validate(key);
    if (!document.is_object()) throw std::invalid_argument("Queue document must be an object");
    const auto bytes = document.dump();
    if (bytes.size() > 16384) throw std::invalid_argument("Queue document exceeds 16 KiB");
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    auto& db = impl_->db;
    check(db, db.execute("BEGIN IMMEDIATE;"));
    try {
        sqlite3pp::query existing(db, "SELECT document_json FROM client_json_outbox WHERE event_key=?;");
        check(db, existing.bind(1, key, sqlite3pp::copy));
        const auto row = existing.begin();
        if (row != existing.end()) {
            if ((*row).get<std::string>(0) != bytes) throw std::invalid_argument("Queue key reused for a different document");
        } else {
            if (impl_->count() >= impl_->capacity) throw std::runtime_error("Durable queue is full");
            sqlite3pp::command insert(db, "INSERT INTO client_json_outbox (event_key,document_json) VALUES (?,?);");
            insert.binder() << key << bytes;
            check(db, insert.execute());
        }
        check(db, db.execute("COMMIT;"));
    } catch (...) { db.execute("ROLLBACK;"); throw; }
}
std::vector<QueuedJson> DurableJsonQueue::batch(std::size_t limit) {
    if (!limit || limit > 500) throw std::invalid_argument("Invalid queue batch limit");
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    sqlite3pp::query q(impl_->db, "SELECT event_key,document_json FROM client_json_outbox ORDER BY sequence LIMIT ?;");
    check(impl_->db, q.bind(1, static_cast<long long>(limit)));
    std::vector<QueuedJson> result;
    for (const auto row : q) result.push_back({row.get<std::string>(0), zzj::json::parseObject(row.get<std::string>(1))});
    return result;
}
void DurableJsonQueue::acknowledge(const std::vector<std::string>& keys) {
    if (keys.size() > 500) throw std::invalid_argument("Invalid acknowledgement size");
    for (const auto& key : keys) validate(key);
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    auto& db = impl_->db;
    check(db, db.execute("BEGIN IMMEDIATE;"));
    try {
        for (const auto& key : keys) {
            sqlite3pp::command remove(db, "DELETE FROM client_json_outbox WHERE event_key=?;");
            check(db, remove.bind(1, key, sqlite3pp::copy));
            check(db, remove.execute());
        }
        check(db, db.execute("COMMIT;"));
    } catch (...) { db.execute("ROLLBACK;"); throw; }
}
std::size_t DurableJsonQueue::size() {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->count();
}
}
