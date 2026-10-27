// Apps/UltraStoreAdmin/core/AdminDatabase.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "AdminDatabase.h"

#include "CatalogClient.h"   // NowIso8601Utc

#include "UltraDatabase/UltraDatabase.h"

namespace UltraStoreAdmin {

namespace {

template <typename T>
Result<T> DbFailure(const UltraDbResult& r, const std::string& what) {
    return Result<T>::Failure(ErrorCode::IoError, "The admin database could not " + what, r.message);
}

std::string Str(const UltraDbRow& row, const char* column) {
    return row[column].IsNull() ? std::string() : row[column].AsString();
}

} // namespace

std::string NowIso8601() { return UltraStore::NowIso8601Utc(); }

AdminDatabase::~AdminDatabase() { Close(); }

Result<void> AdminDatabase::Open(const std::string& databasePathUtf8) {
    Close();
    if (databasePathUtf8.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "The admin database has no path");
    const std::string name = "ultrastoreadmin:" + databasePathUtf8;
    UltraDbConnectionConfig cfg;
    cfg.name = name;
    cfg.driver = "sqlite";
    cfg.database = databasePathUtf8;
    if (const UltraDbResult r = UltraDb_RegisterConnection(cfg); !r) return DbFailure<void>(r, "be registered");
    if (const UltraDbResult r = UltraDb_OpenConnection(name); !r) {
        UltraDb_CloseConnection(name);
        return DbFailure<void>(r, "be opened at " + databasePathUtf8);
    }
    connection_ = name;
    path_ = databasePathUtf8;

    static const char* kTables[] = {
        "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS publishers (id TEXT PRIMARY KEY, name TEXT NOT NULL, url TEXT NOT NULL DEFAULT '')",
        "CREATE TABLE IF NOT EXISTS components ("
        " id TEXT PRIMARY KEY, kind TEXT NOT NULL, name TEXT NOT NULL, publisher TEXT NOT NULL DEFAULT '',"
        " json TEXT NOT NULL, updated_at TEXT NOT NULL DEFAULT '')",
        "CREATE TABLE IF NOT EXISTS package_files ("
        " sha256 TEXT PRIMARY KEY, file TEXT NOT NULL, access TEXT NOT NULL DEFAULT 'public',"
        " size INTEGER NOT NULL DEFAULT 0, format TEXT NOT NULL DEFAULT '')",
        "CREATE TABLE IF NOT EXISTS keys ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, public_key TEXT NOT NULL,"
        " sealed BLOB NOT NULL, created_at TEXT NOT NULL, retired_at TEXT NOT NULL DEFAULT '')",
    };
    for (const char* sql : kTables) {
        if (const UltraDbResult r = UltraDb_Exec(connection_, sql); !r) {
            Close();
            return DbFailure<void>(r, "create its tables");
        }
    }
    UltraDbResultSet meta;
    if (const UltraDbResult r = UltraDb_Query(connection_, "SELECT value FROM meta WHERE key = 'schema_version'", meta); !r) {
        Close();
        return DbFailure<void>(r, "read its schema version");
    }
    const int existing = meta.Empty() ? 0 : meta.Row(0)[size_t(0)].AsInt();
    if (existing > kAdminSchemaVersion) {
        Close();
        return Result<void>::Failure(ErrorCode::SchemaTooNew, "The admin database was written by a newer UltraStoreAdmin",
                                     "schema " + std::to_string(existing) + " > " + std::to_string(kAdminSchemaVersion));
    }
    if (existing < kAdminSchemaVersion) {
        if (const UltraDbResult r = UltraDb_Exec(connection_,
                "INSERT OR REPLACE INTO meta (key, value) VALUES ('schema_version', ?)",
                {UltraDbValue(std::to_string(kAdminSchemaVersion))}); !r) {
            Close();
            return DbFailure<void>(r, "record its schema version");
        }
    }
    return Result<void>::Succeed();
}

void AdminDatabase::Close() {
    if (!connection_.empty()) UltraDb_CloseConnection(connection_);
    connection_.clear();
    path_.clear();
}

// ===== settings =====
std::string AdminDatabase::Setting(const std::string& key, const std::string& fallback) const {
    if (!IsOpen()) return fallback;
    UltraDbResultSet rs;
    if (!UltraDb_Query(connection_, "SELECT value FROM settings WHERE key = ?", {UltraDbValue(key)}, rs) || rs.Empty()) return fallback;
    return rs.Row(0)[size_t(0)].AsString();
}

Result<void> AdminDatabase::SetSetting(const std::string& key, const std::string& value) {
    if (key.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "A setting needs a name");
    const UltraDbResult r = UltraDb_Exec(connection_, "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)",
                                         {UltraDbValue(key), UltraDbValue(value)});
    if (!r) return DbFailure<void>(r, "write the setting " + key);
    return Result<void>::Succeed();
}

Result<std::vector<std::pair<std::string, std::string>>> AdminDatabase::Settings() const {
    using R = Result<std::vector<std::pair<std::string, std::string>>>;
    UltraDbResultSet rs;
    if (const UltraDbResult r = UltraDb_Query(connection_, "SELECT key, value FROM settings ORDER BY key", rs); !r) return DbFailure<std::vector<std::pair<std::string, std::string>>>(r, "read its settings");
    std::vector<std::pair<std::string, std::string>> out;
    for (size_t i = 0; i < rs.Size(); ++i) out.emplace_back(Str(rs.Row(i), "key"), Str(rs.Row(i), "value"));
    return R::Succeed(std::move(out));
}

// ===== publishers =====
Result<void> AdminDatabase::PutPublisher(const Publisher& publisher) {
    if (publisher.id.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "A publisher needs an id");
    const UltraDbResult r = UltraDb_Exec(connection_, "INSERT OR REPLACE INTO publishers (id, name, url) VALUES (?, ?, ?)",
                                         {UltraDbValue(publisher.id), UltraDbValue(publisher.name.empty() ? publisher.id : publisher.name), UltraDbValue(publisher.url)});
    if (!r) return DbFailure<void>(r, "write the publisher " + publisher.id);
    return Result<void>::Succeed();
}

Result<void> AdminDatabase::RemovePublisher(const std::string& id) {
    const UltraDbResult r = UltraDb_Exec(connection_, "DELETE FROM publishers WHERE id = ?", {UltraDbValue(id)});
    if (!r) return DbFailure<void>(r, "remove the publisher " + id);
    if (r.affectedRows == 0) return Result<void>::Failure(ErrorCode::NotFound, "No publisher " + id);
    return Result<void>::Succeed();
}

Result<std::vector<Publisher>> AdminDatabase::Publishers() const {
    UltraDbResultSet rs;
    if (const UltraDbResult r = UltraDb_Query(connection_, "SELECT id, name, url FROM publishers ORDER BY id", rs); !r) return DbFailure<std::vector<Publisher>>(r, "read its publishers");
    std::vector<Publisher> out;
    for (size_t i = 0; i < rs.Size(); ++i) out.push_back({Str(rs.Row(i), "id"), Str(rs.Row(i), "name"), Str(rs.Row(i), "url")});
    return Result<std::vector<Publisher>>::Succeed(std::move(out));
}

// ===== components =====
Result<void> AdminDatabase::PutComponent(const Component& component) {
    if (component.id.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "A component needs an id");
    const std::string json = UltraStore::SerializeComponent(component);
    // What is written is what the store reads: parse it back before storing.
    if (const auto check = UltraStore::ParseComponent(json, component.kind, false); !check) {
        return Result<void>::Failure(check.error.code, "The component would not read back: " + check.error.message, check.error.detail);
    }
    const UltraDbResult r = UltraDb_Exec(connection_,
        "INSERT OR REPLACE INTO components (id, kind, name, publisher, json, updated_at) VALUES (?, ?, ?, ?, ?, ?)",
        {UltraDbValue(component.id), UltraDbValue(UltraStore::ComponentKindName(component.kind)),
         UltraDbValue(component.name.empty() ? component.id : component.name), UltraDbValue(component.publisher),
         UltraDbValue(json), UltraDbValue(NowIso8601())});
    if (!r) return DbFailure<void>(r, "write the component " + component.id);
    return Result<void>::Succeed();
}

Result<Component> AdminDatabase::GetComponent(const std::string& id) const {
    UltraDbResultSet rs;
    if (const UltraDbResult r = UltraDb_Query(connection_, "SELECT kind, json FROM components WHERE id = ?", {UltraDbValue(id)}, rs); !r) return DbFailure<Component>(r, "read the component " + id);
    if (rs.Empty()) return Result<Component>::Failure(ErrorCode::NotFound, "No app or component " + id);
    return UltraStore::ParseComponent(Str(rs.Row(0), "json"), UltraStore::ParseComponentKind(Str(rs.Row(0), "kind")), false);
}

Result<std::vector<Component>> AdminDatabase::Components() const {
    UltraDbResultSet rs;
    if (const UltraDbResult r = UltraDb_Query(connection_, "SELECT id, kind, json FROM components ORDER BY id", rs); !r) return DbFailure<std::vector<Component>>(r, "read its components");
    std::vector<Component> out;
    for (size_t i = 0; i < rs.Size(); ++i) {
        const auto parsed = UltraStore::ParseComponent(Str(rs.Row(i), "json"), UltraStore::ParseComponentKind(Str(rs.Row(i), "kind")), false);
        if (!parsed) return Result<std::vector<Component>>::Failure(parsed.error.code, "The stored component " + Str(rs.Row(i), "id") + " does not read: " + parsed.error.message, parsed.error.detail);
        out.push_back(parsed.Value());
    }
    return Result<std::vector<Component>>::Succeed(std::move(out));
}

Result<void> AdminDatabase::RemoveComponent(const std::string& id) {
    const UltraDbResult r = UltraDb_Exec(connection_, "DELETE FROM components WHERE id = ?", {UltraDbValue(id)});
    if (!r) return DbFailure<void>(r, "remove the component " + id);
    if (r.affectedRows == 0) return Result<void>::Failure(ErrorCode::NotFound, "No app or component " + id);
    return Result<void>::Succeed();
}

// ===== package files =====
Result<void> AdminDatabase::PutPackageFile(const PackageFile& file) {
    if (file.sha256.size() != 64) return Result<void>::Failure(ErrorCode::InvalidArgument, "A package file is keyed by its 64-digit SHA-256");
    if (file.access != "public" && file.access != "restricted") return Result<void>::Failure(ErrorCode::InvalidArgument, "A package's access is public or restricted, not \"" + file.access + "\"");
    const UltraDbResult r = UltraDb_Exec(connection_,
        "INSERT OR REPLACE INTO package_files (sha256, file, access, size, format) VALUES (?, ?, ?, ?, ?)",
        {UltraDbValue(file.sha256), UltraDbValue(file.file), UltraDbValue(file.access), UltraDbValue(static_cast<int64_t>(file.size)), UltraDbValue(file.format)});
    if (!r) return DbFailure<void>(r, "write the package file " + file.sha256);
    return Result<void>::Succeed();
}

Result<PackageFile> AdminDatabase::GetPackageFile(const std::string& sha256) const {
    UltraDbResultSet rs;
    if (const UltraDbResult r = UltraDb_Query(connection_, "SELECT sha256, file, access, size, format FROM package_files WHERE sha256 = ?", {UltraDbValue(sha256)}, rs); !r) return DbFailure<PackageFile>(r, "read the package file " + sha256);
    if (rs.Empty()) return Result<PackageFile>::Failure(ErrorCode::NotFound, "No package file with hash " + sha256);
    const UltraDbRow& row = rs.Row(0);
    PackageFile f;
    f.sha256 = Str(row, "sha256");
    f.file = Str(row, "file");
    f.access = Str(row, "access");
    f.size = static_cast<uint64_t>(row["size"].AsInt64());
    f.format = Str(row, "format");
    return Result<PackageFile>::Succeed(std::move(f));
}

Result<std::vector<PackageFile>> AdminDatabase::PackageFiles() const {
    UltraDbResultSet rs;
    if (const UltraDbResult r = UltraDb_Query(connection_, "SELECT sha256, file, access, size, format FROM package_files ORDER BY sha256", rs); !r) return DbFailure<std::vector<PackageFile>>(r, "read its package files");
    std::vector<PackageFile> out;
    for (size_t i = 0; i < rs.Size(); ++i) {
        const UltraDbRow& row = rs.Row(i);
        PackageFile f;
        f.sha256 = Str(row, "sha256");
        f.file = Str(row, "file");
        f.access = Str(row, "access");
        f.size = static_cast<uint64_t>(row["size"].AsInt64());
        f.format = Str(row, "format");
        out.push_back(std::move(f));
    }
    return Result<std::vector<PackageFile>>::Succeed(std::move(out));
}

Result<void> AdminDatabase::RemovePackageFile(const std::string& sha256) {
    const UltraDbResult r = UltraDb_Exec(connection_, "DELETE FROM package_files WHERE sha256 = ?", {UltraDbValue(sha256)});
    if (!r) return DbFailure<void>(r, "remove the package file " + sha256);
    if (r.affectedRows == 0) return Result<void>::Failure(ErrorCode::NotFound, "No package file with hash " + sha256);
    return Result<void>::Succeed();
}

// ===== keys =====
Result<KeyRecord> AdminDatabase::AddKey(const std::string& name, const std::string& publicKeyHex,
                                        const std::vector<uint8_t>& sealedSeed) {
    if (publicKeyHex.size() != 64) return Result<KeyRecord>::Failure(ErrorCode::InvalidArgument, "A public key is 64 hex digits");
    if (sealedSeed.empty()) return Result<KeyRecord>::Failure(ErrorCode::InvalidArgument, "A key needs its sealed seed");
    KeyRecord key;
    key.name = name.empty() ? "signing key" : name;
    key.publicKeyHex = publicKeyHex;
    key.createdAt = NowIso8601();
    const UltraDbResult r = UltraDb_Exec(connection_,
        "INSERT INTO keys (name, public_key, sealed, created_at, retired_at) VALUES (?, ?, ?, ?, '')",
        {UltraDbValue(key.name), UltraDbValue(key.publicKeyHex), UltraDbValue::Blob(sealedSeed), UltraDbValue(key.createdAt)});
    if (!r) return DbFailure<KeyRecord>(r, "add the key " + key.name);
    key.id = r.lastInsertId;
    return Result<KeyRecord>::Succeed(std::move(key));
}

Result<std::vector<KeyRecord>> AdminDatabase::Keys() const {
    UltraDbResultSet rs;
    if (const UltraDbResult r = UltraDb_Query(connection_, "SELECT id, name, public_key, created_at, retired_at FROM keys ORDER BY id", rs); !r) return DbFailure<std::vector<KeyRecord>>(r, "read its keys");
    std::vector<KeyRecord> out;
    for (size_t i = 0; i < rs.Size(); ++i) {
        const UltraDbRow& row = rs.Row(i);
        out.push_back({row["id"].AsInt64(), Str(row, "name"), Str(row, "public_key"), Str(row, "created_at"), Str(row, "retired_at")});
    }
    return Result<std::vector<KeyRecord>>::Succeed(std::move(out));
}

Result<KeyRecord> AdminDatabase::GetKey(int64_t id) const {
    const auto all = Keys();
    if (!all) return Result<KeyRecord>::Failure(all.error.code, all.error.message, all.error.detail);
    for (const KeyRecord& k : all.Value()) if (k.id == id) return Result<KeyRecord>::Succeed(k);
    return Result<KeyRecord>::Failure(ErrorCode::NotFound, "No key " + std::to_string(id));
}

Result<std::vector<uint8_t>> AdminDatabase::SealedSeed(int64_t id) const {
    UltraDbResultSet rs;
    if (const UltraDbResult r = UltraDb_Query(connection_, "SELECT sealed FROM keys WHERE id = ?", {UltraDbValue(id)}, rs); !r) return DbFailure<std::vector<uint8_t>>(r, "read the key " + std::to_string(id));
    if (rs.Empty()) return Result<std::vector<uint8_t>>::Failure(ErrorCode::NotFound, "No key " + std::to_string(id));
    return Result<std::vector<uint8_t>>::Succeed(rs.Row(0)[size_t(0)].AsBlob());
}

Result<void> AdminDatabase::RetireKey(int64_t id) {
    const UltraDbResult r = UltraDb_Exec(connection_, "UPDATE keys SET retired_at = ? WHERE id = ? AND retired_at = ''",
                                         {UltraDbValue(NowIso8601()), UltraDbValue(id)});
    if (!r) return DbFailure<void>(r, "retire the key " + std::to_string(id));
    if (r.affectedRows == 0) return Result<void>::Failure(ErrorCode::NotFound, "No active key " + std::to_string(id));
    return Result<void>::Succeed();
}

// ===== sequence =====
uint64_t AdminDatabase::Sequence() const {
    const std::string s = Setting("catalog.sequence", "0");
    return static_cast<uint64_t>(std::strtoull(s.c_str(), nullptr, 10));   // locale-ok: digits only
}

Result<void> AdminDatabase::SetSequence(uint64_t sequence) {
    return SetSetting("catalog.sequence", std::to_string(sequence));
}

// ===== transactions =====
Result<void> AdminDatabase::BeginTransaction() {
    const UltraDbResult r = UltraDb_Exec(connection_, "BEGIN IMMEDIATE");
    if (!r) return DbFailure<void>(r, "begin a transaction");
    return Result<void>::Succeed();
}

Result<void> AdminDatabase::Commit() {
    const UltraDbResult r = UltraDb_Exec(connection_, "COMMIT");
    if (!r) return DbFailure<void>(r, "commit");
    return Result<void>::Succeed();
}

Result<void> AdminDatabase::Rollback() {
    const UltraDbResult r = UltraDb_Exec(connection_, "ROLLBACK");
    if (!r) return DbFailure<void>(r, "roll back");
    return Result<void>::Succeed();
}

} // namespace UltraStoreAdmin
