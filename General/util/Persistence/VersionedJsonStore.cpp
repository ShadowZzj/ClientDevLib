#include "VersionedJsonStore.h"
#include <General/ThirdParty/sqlite3pp/sqlite3pp.h>
#include <mutex>

namespace zzj::persistence {
namespace {
void check(sqlite3pp::database& db, int result) {
    if (result != SQLITE_OK)
        throw sqlite3pp::database_error(db);
}
VersionedJsonDocument read(const sqlite3pp::query::rows& row) {
    return {row.get<std::string>(0), static_cast<std::uint64_t>(row.get<long long>(1)),
        zzj::json::parseObject(row.get<std::string>(2)), row.get<std::string>(3)};
}
void validateKey(const std::string& key) {
    if (key.empty() || key.size() > 512 || key.find('\0') != std::string::npos)
        throw std::invalid_argument("Invalid document key");
}
std::string filename(const std::filesystem::path& path) {
    if (path.empty())
        throw std::invalid_argument("Database path must not be empty");
    const auto absolute = std::filesystem::absolute(path);
    std::filesystem::create_directories(absolute.parent_path());
    return absolute.u8string();
}
} // namespace
struct VersionedJsonStore::Impl {
    explicit Impl(const std::filesystem::path& path) : db(filename(path).c_str()) {
        check(db, db.set_busy_timeout(5000));
        check(db, db.execute("PRAGMA journal_mode=WAL;"));
        check(db, db.execute("PRAGMA synchronous=FULL;"));
        check(db, db.execute("CREATE TABLE IF NOT EXISTS client_json_documents ("
            "document_key TEXT PRIMARY KEY, generation INTEGER NOT NULL CHECK(generation > 0), "
            "document_json TEXT NOT NULL, updated_at TEXT NOT NULL);"));
        check(db, db.execute("CREATE TABLE IF NOT EXISTS client_json_versions ("
            "document_key TEXT NOT NULL, generation INTEGER NOT NULL, document_json TEXT NOT NULL, "
            "updated_at TEXT NOT NULL, PRIMARY KEY(document_key,generation));"));
    }
    std::optional<VersionedJsonDocument> get(const std::string& key) {
        sqlite3pp::query query(db, "SELECT document_key,generation,document_json,updated_at "
            "FROM client_json_documents WHERE document_key=?;");
        check(db, query.bind(1, key, sqlite3pp::copy));
        const auto iterator = query.begin();
        if (iterator == query.end())
            return std::nullopt;
        return read(*iterator);
    }
    sqlite3pp::database db;
    std::mutex mutex;
};
VersionConflict::VersionConflict(std::uint64_t current)
    : std::runtime_error("Document generation conflict"), currentGeneration_(current) {}
VersionedJsonStore::VersionedJsonStore(const std::filesystem::path& path) : impl_(std::make_unique<Impl>(path)) {}
VersionedJsonStore::~VersionedJsonStore() = default;
std::optional<VersionedJsonDocument> VersionedJsonStore::get(const std::string& key) {
    validateKey(key);
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->get(key);
}
std::vector<VersionedJsonDocument> VersionedJsonStore::list() {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    sqlite3pp::query query(impl_->db, "SELECT document_key,generation,document_json,updated_at "
        "FROM client_json_documents ORDER BY document_key;");
    std::vector<VersionedJsonDocument> result;
    for (const auto row : query)
        result.push_back(read(row));
    return result;
}
std::vector<VersionedJsonDocument> VersionedJsonStore::history(const std::string& key) {
    validateKey(key);
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    sqlite3pp::query query(impl_->db, "SELECT document_key,generation,document_json,updated_at "
        "FROM client_json_versions WHERE document_key=? ORDER BY generation DESC;");
    check(impl_->db, query.bind(1, key, sqlite3pp::copy));
    std::vector<VersionedJsonDocument> result;
    for (const auto row : query)
        result.push_back(read(row));
    return result;
}
VersionedJsonDocument VersionedJsonStore::compareExchange(const std::string& key,
    std::uint64_t expected, const std::function<zzj::json::Json(std::uint64_t)>& create,
    const std::string& updatedAt) {
    validateKey(key);
    if (expected >= MaximumGeneration || !create || updatedAt.empty() || updatedAt.size() > 128)
        throw std::invalid_argument("Invalid document revision or timestamp");
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    auto& db = impl_->db;
    // sqlite3pp transaction does not expose constructor failures. Explicit BEGIN
    // checks the reservation, and every unsuccessful path performs a rollback.
    check(db, db.execute("BEGIN IMMEDIATE;"));
    try {
        const auto current = impl_->get(key);
        const auto generation = current ? current->generation : 0;
        if (generation != expected)
            throw VersionConflict(generation);
        VersionedJsonDocument document{key, generation + 1, create(generation + 1), updatedAt};
        if (!document.document.is_object())
            throw std::invalid_argument("Stored document must be an object");
        const auto bytes = document.document.dump();
        if (bytes.size() > 1024 * 1024)
            throw std::invalid_argument("Stored document exceeds size limit");
        sqlite3pp::command insert(db, "INSERT INTO client_json_versions "
            "(document_key,generation,document_json,updated_at) VALUES (?,?,?,?);");
        insert.binder() << key << static_cast<long long>(document.generation) << bytes << updatedAt;
        check(db, insert.execute());
        sqlite3pp::command update(db, "INSERT OR REPLACE INTO client_json_documents "
            "(document_key,generation,document_json,updated_at) VALUES (?,?,?,?);");
        update.binder() << key << static_cast<long long>(document.generation) << bytes << updatedAt;
        check(db, update.execute());
        check(db, db.execute("COMMIT;"));
        return document;
    } catch (...) {
        db.execute("ROLLBACK;");
        throw;
    }
}
} // namespace zzj::persistence
