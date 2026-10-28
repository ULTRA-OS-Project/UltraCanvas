// Apps/UltraStoreAdmin/core/AdminDatabase.cpp
// Version: 0.3.0 - sales.reference
// Version: 0.2.0 - affiliates, codes, sales
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "AdminDatabase.h"
#include <sstream>
#include <cctype>

#include "CatalogClient.h"   // NowIso8601Utc

#include "UltraDatabase/UltraDatabase.h"

#include <cstdlib>

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
        // schema 2
        "CREATE TABLE IF NOT EXISTS affiliates ("
        " id TEXT PRIMARY KEY, name TEXT NOT NULL, contact TEXT NOT NULL DEFAULT '', share_percent INTEGER NOT NULL DEFAULT 0,"
        " recruit_share_percent INTEGER NOT NULL DEFAULT 0, recruited_by TEXT NOT NULL DEFAULT '', enabled INTEGER NOT NULL DEFAULT 1,"
        " created_at TEXT NOT NULL DEFAULT '')",
        "CREATE TABLE IF NOT EXISTS codes ("
        " code TEXT PRIMARY KEY, kind TEXT NOT NULL, affiliate TEXT NOT NULL DEFAULT '', discount_percent INTEGER NOT NULL DEFAULT 0,"
        " discount_amount TEXT NOT NULL DEFAULT '', currency TEXT NOT NULL DEFAULT '', apps TEXT NOT NULL DEFAULT '',"
        " valid_from TEXT NOT NULL DEFAULT '', valid_until TEXT NOT NULL DEFAULT '', enabled INTEGER NOT NULL DEFAULT 1,"
        " created_at TEXT NOT NULL DEFAULT '')",
        "CREATE TABLE IF NOT EXISTS sales ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT, at TEXT NOT NULL, code TEXT NOT NULL DEFAULT '', app TEXT NOT NULL DEFAULT '',"
        " amount TEXT NOT NULL DEFAULT '0', currency TEXT NOT NULL DEFAULT '', affiliate TEXT NOT NULL DEFAULT '',"
        " recruiter TEXT NOT NULL DEFAULT '', affiliate_credit TEXT NOT NULL DEFAULT '0', recruiter_credit TEXT NOT NULL DEFAULT '0',"
        " reference TEXT NOT NULL DEFAULT '')",
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
    if (existing == 2) {   // schema 2's sales table has no reference column
        if (const UltraDbResult r = UltraDb_Exec(connection_, "ALTER TABLE sales ADD COLUMN reference TEXT NOT NULL DEFAULT ''"); !r) {
            Close();
            return DbFailure<void>(r, "add the sales' reference column");
        }
    }
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
// ===== affiliates =====
Result<void> AdminDatabase::PutAffiliate(const Affiliate& a) {
    if (a.id.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "An affiliate needs an id");
    for (char c : a.id) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.') {
            return Result<void>::Failure(ErrorCode::InvalidArgument, "An affiliate id is letters, digits, '-', '_' and '.'", a.id);
        }
    }
    if (a.sharePercent < 0 || a.sharePercent > 100 || a.recruitSharePercent < 0 || a.recruitSharePercent > 100) {
        return Result<void>::Failure(ErrorCode::InvalidArgument, "A share is 0 to 100 percent", a.id);
    }
    if (!a.recruitedBy.empty()) {
        if (a.recruitedBy == a.id) return Result<void>::Failure(ErrorCode::InvalidArgument, "An affiliate cannot recruit itself", a.id);
        const auto recruiter = GetAffiliate(a.recruitedBy);
        if (!recruiter) return Result<void>::Failure(ErrorCode::NotFound, "No affiliate " + a.recruitedBy + " to be recruited by");
        // Two tiers: the recruiter earns from this one; this one's recruits
        // credit it, never the recruiter. A loop of recruits is refused.
        for (std::string up = recruiter.Value().recruitedBy; !up.empty();) {
            if (up == a.id) return Result<void>::Failure(ErrorCode::InvalidArgument, "The recruits would form a loop", a.id);
            const auto next = GetAffiliate(up);
            if (!next) break;
            up = next.Value().recruitedBy;
        }
    }
    const UltraDbResult r = UltraDb_Exec(connection_,
        "INSERT OR REPLACE INTO affiliates (id, name, contact, share_percent, recruit_share_percent, recruited_by, enabled, created_at)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, COALESCE((SELECT created_at FROM affiliates WHERE id = ?), ?))",
        {UltraDbValue(a.id), UltraDbValue(a.name.empty() ? a.id : a.name), UltraDbValue(a.contact),
         UltraDbValue(static_cast<int64_t>(a.sharePercent)), UltraDbValue(static_cast<int64_t>(a.recruitSharePercent)),
         UltraDbValue(a.recruitedBy), UltraDbValue(static_cast<int64_t>(a.enabled ? 1 : 0)), UltraDbValue(a.id),
         UltraDbValue(a.createdAt.empty() ? NowIso8601() : a.createdAt)});
    if (!r) return DbFailure<void>(r, "write the affiliate " + a.id);
    return Result<void>::Succeed();
}

Result<std::vector<Affiliate>> AdminDatabase::Affiliates() const {
    UltraDbResultSet rs;
    if (const UltraDbResult r = UltraDb_Query(connection_,
            "SELECT id, name, contact, share_percent, recruit_share_percent, recruited_by, enabled, created_at FROM affiliates ORDER BY id", rs); !r) {
        return DbFailure<std::vector<Affiliate>>(r, "read its affiliates");
    }
    std::vector<Affiliate> out;
    for (size_t i = 0; i < rs.Size(); ++i) {
        const UltraDbRow& row = rs.Row(i);
        Affiliate a;
        a.id = Str(row, "id");
        a.name = Str(row, "name");
        a.contact = Str(row, "contact");
        a.sharePercent = static_cast<int>(row["share_percent"].AsInt64());
        a.recruitSharePercent = static_cast<int>(row["recruit_share_percent"].AsInt64());
        a.recruitedBy = Str(row, "recruited_by");
        a.enabled = row["enabled"].AsInt64() != 0;
        a.createdAt = Str(row, "created_at");
        out.push_back(std::move(a));
    }
    return Result<std::vector<Affiliate>>::Succeed(std::move(out));
}

Result<Affiliate> AdminDatabase::GetAffiliate(const std::string& id) const {
    const auto all = Affiliates();
    if (!all) return Result<Affiliate>::Failure(all.error.code, all.error.message, all.error.detail);
    for (const Affiliate& a : all.Value()) if (a.id == id) return Result<Affiliate>::Succeed(a);
    return Result<Affiliate>::Failure(ErrorCode::NotFound, "No affiliate " + id);
}

Result<void> AdminDatabase::RemoveAffiliate(const std::string& id) {
    const auto codes = Codes();
    if (!codes) return Result<void>::Failure(codes.error.code, codes.error.message, codes.error.detail);
    for (const CodeRecord& c : codes.Value()) {
        if (c.code.affiliate == id) return Result<void>::Failure(ErrorCode::InvalidArgument, "The code " + c.code.code + " names the affiliate: remove it first", id);
    }
    const auto all = Affiliates();
    if (!all) return Result<void>::Failure(all.error.code, all.error.message, all.error.detail);
    for (const Affiliate& a : all.Value()) {
        if (a.recruitedBy == id) return Result<void>::Failure(ErrorCode::InvalidArgument, "The affiliate " + a.id + " was recruited by it: change that first", id);
    }
    const UltraDbResult r = UltraDb_Exec(connection_, "DELETE FROM affiliates WHERE id = ?", {UltraDbValue(id)});
    if (!r) return DbFailure<void>(r, "remove the affiliate " + id);
    if (r.affectedRows == 0) return Result<void>::Failure(ErrorCode::NotFound, "No affiliate " + id);
    return Result<void>::Succeed();
}

// ===== codes =====
Result<void> AdminDatabase::PutCode(const CodeRecord& record) {
    const UltraStore::PromoCode& c = record.code;
    const std::string normalized = UltraStore::NormalizeCode(c.code);
    if (normalized.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "A code needs text");
    for (char ch : normalized) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') {
            return Result<void>::Failure(ErrorCode::InvalidArgument, "A code is letters, digits, '-' and '_'", c.code);
        }
    }
    if (c.discountPercent < 0 || c.discountPercent > 100) return Result<void>::Failure(ErrorCode::InvalidArgument, "A discount is 0 to 100 percent", c.code);
    if (c.discountPercent > 0 && !c.discountAmount.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "A code takes off a percentage or an amount, not both", c.code);
    if (c.kind == UltraStore::CodeKind::Affiliate) {
        if (c.affiliate.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "An affiliate code names its affiliate", c.code);
        if (const auto a = GetAffiliate(c.affiliate); !a) return a.error.code == ErrorCode::NotFound
            ? Result<void>::Failure(ErrorCode::NotFound, "No affiliate " + c.affiliate + " for the code " + c.code)
            : Result<void>::Failure(a.error.code, a.error.message, a.error.detail);
    }
    std::string apps;
    for (const std::string& a : c.apps) apps += (apps.empty() ? "" : " ") + a;
    const UltraDbResult r = UltraDb_Exec(connection_,
        "INSERT OR REPLACE INTO codes (code, kind, affiliate, discount_percent, discount_amount, currency, apps, valid_from, valid_until, enabled, created_at)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, COALESCE((SELECT created_at FROM codes WHERE code = ?), ?))",
        {UltraDbValue(normalized), UltraDbValue(UltraStore::CodeKindName(c.kind)), UltraDbValue(c.affiliate),
         UltraDbValue(static_cast<int64_t>(c.discountPercent)), UltraDbValue(c.discountAmount), UltraDbValue(c.currency), UltraDbValue(apps),
         UltraDbValue(c.validFrom), UltraDbValue(c.validUntil), UltraDbValue(static_cast<int64_t>(record.enabled ? 1 : 0)),
         UltraDbValue(normalized), UltraDbValue(record.createdAt.empty() ? NowIso8601() : record.createdAt)});
    if (!r) return DbFailure<void>(r, "write the code " + c.code);
    return Result<void>::Succeed();
}

Result<std::vector<CodeRecord>> AdminDatabase::Codes() const {
    UltraDbResultSet rs;
    if (const UltraDbResult r = UltraDb_Query(connection_,
            "SELECT code, kind, affiliate, discount_percent, discount_amount, currency, apps, valid_from, valid_until, enabled, created_at FROM codes ORDER BY code", rs); !r) {
        return DbFailure<std::vector<CodeRecord>>(r, "read its codes");
    }
    std::vector<CodeRecord> out;
    for (size_t i = 0; i < rs.Size(); ++i) {
        const UltraDbRow& row = rs.Row(i);
        CodeRecord rec;
        rec.code.code = Str(row, "code");
        rec.code.kind = UltraStore::ParseCodeKind(Str(row, "kind"));
        rec.code.affiliate = Str(row, "affiliate");
        rec.code.discountPercent = static_cast<int>(row["discount_percent"].AsInt64());
        rec.code.discountAmount = Str(row, "discount_amount");
        rec.code.currency = Str(row, "currency");
        std::istringstream apps(Str(row, "apps"));
        for (std::string a; apps >> a;) rec.code.apps.push_back(a);
        rec.code.validFrom = Str(row, "valid_from");
        rec.code.validUntil = Str(row, "valid_until");
        rec.enabled = row["enabled"].AsInt64() != 0;
        rec.createdAt = Str(row, "created_at");
        out.push_back(std::move(rec));
    }
    return Result<std::vector<CodeRecord>>::Succeed(std::move(out));
}

Result<CodeRecord> AdminDatabase::GetCode(const std::string& code) const {
    const auto all = Codes();
    if (!all) return Result<CodeRecord>::Failure(all.error.code, all.error.message, all.error.detail);
    const std::string wanted = UltraStore::NormalizeCode(code);
    for (const CodeRecord& c : all.Value()) if (c.code.code == wanted) return Result<CodeRecord>::Succeed(c);
    return Result<CodeRecord>::Failure(ErrorCode::NotFound, "No code " + wanted);
}

Result<void> AdminDatabase::RemoveCode(const std::string& code) {
    const UltraDbResult r = UltraDb_Exec(connection_, "DELETE FROM codes WHERE code = ?", {UltraDbValue(UltraStore::NormalizeCode(code))});
    if (!r) return DbFailure<void>(r, "remove the code " + code);
    if (r.affectedRows == 0) return Result<void>::Failure(ErrorCode::NotFound, "No code " + UltraStore::NormalizeCode(code));
    return Result<void>::Succeed();
}

// ===== sales =====
Result<SaleRecord> AdminDatabase::AddSale(const SaleRecord& sale) {
    const UltraDbResult r = UltraDb_Exec(connection_,
        "INSERT INTO sales (at, code, app, amount, currency, affiliate, recruiter, affiliate_credit, recruiter_credit, reference) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        {UltraDbValue(sale.at.empty() ? NowIso8601() : sale.at), UltraDbValue(sale.code), UltraDbValue(sale.app), UltraDbValue(sale.amount),
         UltraDbValue(sale.currency), UltraDbValue(sale.affiliate), UltraDbValue(sale.recruiter), UltraDbValue(sale.affiliateCredit), UltraDbValue(sale.recruiterCredit),
         UltraDbValue(sale.reference)});
    if (!r) return DbFailure<SaleRecord>(r, "record the sale");
    SaleRecord out = sale;
    out.id = r.lastInsertId;
    if (out.at.empty()) out.at = NowIso8601();
    return Result<SaleRecord>::Succeed(std::move(out));
}

bool AdminDatabase::HasSaleReference(const std::string& reference) const {
    if (reference.empty()) return false;
    UltraDbResultSet rs;
    if (const UltraDbResult r = UltraDb_Query(connection_, "SELECT id FROM sales WHERE reference = ? LIMIT 1", {UltraDbValue(reference)}, rs); !r) return false;
    return !rs.Empty();
}

Result<std::vector<SaleRecord>> AdminDatabase::Sales() const {
    UltraDbResultSet rs;
    if (const UltraDbResult r = UltraDb_Query(connection_,
            "SELECT id, at, code, app, amount, currency, affiliate, recruiter, affiliate_credit, recruiter_credit, reference FROM sales ORDER BY id", rs); !r) {
        return DbFailure<std::vector<SaleRecord>>(r, "read its sales");
    }
    std::vector<SaleRecord> out;
    for (size_t i = 0; i < rs.Size(); ++i) {
        const UltraDbRow& row = rs.Row(i);
        SaleRecord sale;
        sale.id = row["id"].AsInt64();
        sale.at = Str(row, "at");
        sale.code = Str(row, "code");
        sale.app = Str(row, "app");
        sale.amount = Str(row, "amount");
        sale.currency = Str(row, "currency");
        sale.affiliate = Str(row, "affiliate");
        sale.recruiter = Str(row, "recruiter");
        sale.affiliateCredit = Str(row, "affiliate_credit");
        sale.recruiterCredit = Str(row, "recruiter_credit");
        sale.reference = Str(row, "reference");
        out.push_back(std::move(sale));
    }
    return Result<std::vector<SaleRecord>>::Succeed(std::move(out));
}

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
