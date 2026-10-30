// Apps/UltraStore/core/Registry.cpp
// The installed-software registry on UltraDatabase (Registry.h).
// Version: 0.2.0 - licences
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Registry.h"

#include "UltraDatabase/UltraDatabase.h"

#include <sstream>

namespace UltraStore {

namespace {

Result<void> DbFailure(const UltraDbResult& r, const std::string& what) {
    return Result<void>::Failure(ErrorCode::IoError, "The registry could not " + what, r.message);
}

InstalledRecord RecordOf(const UltraDbRow& row) {
    InstalledRecord rec;
    rec.kind = ParseComponentKind(row["kind"].AsString());
    rec.id = row["id"].AsString();
    rec.version = row["version"].AsString();
    rec.folder = row["folder"].AsString();
    rec.entry = row["entry"].AsString();
    rec.sha256 = row["sha256"].AsString();
    rec.installedAt = row["installed_at"].AsString();
    rec.channel = row["channel"].AsString();
    rec.depends = SqliteRegistry::SplitDepends(row["depends"].AsString());
    rec.current = row["current"].AsInt64() != 0;
    return rec;
}

const char* kColumns = "kind, id, version, folder, entry, sha256, installed_at, channel, depends, current";

} // namespace

SqliteRegistry::~SqliteRegistry() { Close(); }

Result<void> SqliteRegistry::Open(const std::string& databasePathUtf8) {
    Close();
    if (databasePathUtf8.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "The registry has no path");
    const std::string name = "ultrastore-registry:" + databasePathUtf8;
    UltraDbConnectionConfig cfg;
    cfg.name = name;
    cfg.driver = "sqlite";
    cfg.database = databasePathUtf8;
    if (const UltraDbResult r = UltraDb_RegisterConnection(cfg); !r) return DbFailure(r, "be registered");
    if (const UltraDbResult r = UltraDb_OpenConnection(name); !r) {
        UltraDb_CloseConnection(name);
        return DbFailure(r, "be opened at " + databasePathUtf8);
    }
    connection_ = name;

    if (const UltraDbResult r = UltraDb_Exec(connection_,
            "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)"); !r) {
        Close();
        return DbFailure(r, "create its tables");
    }
    UltraDbResultSet meta;
    if (const UltraDbResult r = UltraDb_Query(connection_, "SELECT value FROM meta WHERE key = 'schema_version'", meta); !r) {
        Close();
        return DbFailure(r, "read its schema version");
    }
    const int existing = meta.Empty() ? 0 : meta.Row(0)[size_t(0)].AsInt();
    if (existing > kSchemaVersion) {
        Close();
        return Result<void>::Failure(ErrorCode::SchemaTooNew,
                                     "The registry was written by a newer UltraStore",
                                     "schema " + std::to_string(existing) + " > " + std::to_string(kSchemaVersion));
    }
    if (const UltraDbResult r = UltraDb_Exec(connection_,
            "CREATE TABLE IF NOT EXISTS installed ("
            " kind TEXT NOT NULL, id TEXT NOT NULL, version TEXT NOT NULL,"
            " folder TEXT NOT NULL, entry TEXT NOT NULL DEFAULT '', sha256 TEXT NOT NULL DEFAULT '',"
            " installed_at TEXT NOT NULL DEFAULT '', channel TEXT NOT NULL DEFAULT 'stable',"
            " depends TEXT NOT NULL DEFAULT '', current INTEGER NOT NULL DEFAULT 0,"
            " PRIMARY KEY (id, version))"); !r) {
        Close();
        return DbFailure(r, "create its tables");
    }
    if (const UltraDbResult r = UltraDb_Exec(connection_,
            "CREATE TABLE IF NOT EXISTS licences ("
            " app TEXT PRIMARY KEY, licence_id TEXT NOT NULL, order_id TEXT NOT NULL, order_secret TEXT NOT NULL DEFAULT '',"
            " token TEXT NOT NULL, issued_at TEXT NOT NULL DEFAULT '', amount TEXT NOT NULL DEFAULT '',"
            " currency TEXT NOT NULL DEFAULT '', receipt_no INTEGER NOT NULL DEFAULT 0)"); !r) {
        Close();
        return DbFailure(r, "create its licences table");
    }
    if (existing < kSchemaVersion) {
        if (const UltraDbResult r = UltraDb_Exec(connection_,
                "INSERT OR REPLACE INTO meta (key, value) VALUES ('schema_version', ?)",
                {UltraDbValue(std::to_string(kSchemaVersion))}); !r) {
            Close();
            return DbFailure(r, "record its schema version");
        }
    }
    return Result<void>::Succeed();
}

void SqliteRegistry::Close() {
    if (connection_.empty()) return;
    UltraDb_CloseConnection(connection_);
    connection_.clear();
}

Result<std::vector<InstalledRecord>> SqliteRegistry::All() const {
    using R = Result<std::vector<InstalledRecord>>;
    if (connection_.empty()) return R::Failure(ErrorCode::InvalidArgument, "The registry is not open");
    UltraDbResultSet rs;
    const UltraDbResult r = UltraDb_Query(connection_,
        std::string("SELECT ") + kColumns + " FROM installed ORDER BY kind, id, version", rs);
    if (!r) return R::Failure(ErrorCode::IoError, "The registry could not be read", r.message);
    std::vector<InstalledRecord> out;
    for (const UltraDbRow& row : rs) out.push_back(RecordOf(row));
    return R::Succeed(std::move(out));
}

Result<std::vector<InstalledRecord>> SqliteRegistry::Of(std::string_view id) const {
    using R = Result<std::vector<InstalledRecord>>;
    if (connection_.empty()) return R::Failure(ErrorCode::InvalidArgument, "The registry is not open");
    UltraDbResultSet rs;
    const UltraDbResult r = UltraDb_Query(connection_,
        std::string("SELECT ") + kColumns + " FROM installed WHERE id = ? ORDER BY version",
        {UltraDbValue(std::string(id))}, rs);
    if (!r) return R::Failure(ErrorCode::IoError, "The registry could not be read", r.message);
    std::vector<InstalledRecord> out;
    for (const UltraDbRow& row : rs) out.push_back(RecordOf(row));
    return R::Succeed(std::move(out));
}

Result<InstalledRecord> SqliteRegistry::Get(std::string_view id, std::string_view version) const {
    const auto rows = Of(id);
    if (!rows) return Result<InstalledRecord>::Failure(rows.error.code, rows.error.message, rows.error.detail);
    for (const InstalledRecord& r : rows.Value()) {
        if (VersionEqual(r.version, version)) return Result<InstalledRecord>::Succeed(r);
    }
    return Result<InstalledRecord>::Failure(ErrorCode::NotFound, std::string(id) + " " + std::string(version) + " is not installed");
}

Result<InstalledRecord> SqliteRegistry::Current(std::string_view id) const {
    const auto rows = Of(id);
    if (!rows) return Result<InstalledRecord>::Failure(rows.error.code, rows.error.message, rows.error.detail);
    for (const InstalledRecord& r : rows.Value()) if (r.current) return Result<InstalledRecord>::Succeed(r);
    return Result<InstalledRecord>::Failure(ErrorCode::NotFound, std::string(id) + " is not installed");
}

Result<void> SqliteRegistry::Add(const InstalledRecord& rec) {
    if (connection_.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "The registry is not open");
    if (rec.id.empty() || rec.version.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "A record needs an id and a version");
    const UltraDbResult r = UltraDb_Exec(connection_,
        std::string("INSERT OR REPLACE INTO installed (") + kColumns + ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        {UltraDbValue(ComponentKindName(rec.kind)), UltraDbValue(rec.id), UltraDbValue(rec.version),
         UltraDbValue(rec.folder), UltraDbValue(rec.entry), UltraDbValue(rec.sha256), UltraDbValue(rec.installedAt),
         UltraDbValue(rec.channel.empty() ? std::string("stable") : rec.channel), UltraDbValue(JoinDepends(rec.depends)),
         UltraDbValue(static_cast<int64_t>(rec.current ? 1 : 0))});
    if (!r) return DbFailure(r, "record " + rec.id + " " + rec.version);
    if (rec.current) return SetCurrent(rec.id, rec.version);
    return Result<void>::Succeed();
}

Result<void> SqliteRegistry::Remove(std::string_view id, std::string_view version) {
    if (connection_.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "The registry is not open");
    const UltraDbResult r = UltraDb_Exec(connection_, "DELETE FROM installed WHERE id = ? AND version = ?",
                                         {UltraDbValue(std::string(id)), UltraDbValue(std::string(version))});
    if (!r) return DbFailure(r, "forget " + std::string(id) + " " + std::string(version));
    return Result<void>::Succeed();
}

Result<void> SqliteRegistry::SetCurrent(std::string_view id, std::string_view version) {
    if (connection_.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "The registry is not open");
    // The new one first, so a version that is not installed changes nothing
    // (the clear afterwards leaves the row just set).
    const UltraDbResult set = UltraDb_Exec(connection_, "UPDATE installed SET current = 1 WHERE id = ? AND version = ?",
                                           {UltraDbValue(std::string(id)), UltraDbValue(std::string(version))});
    if (!set) return DbFailure(set, "change the current version of " + std::string(id));
    if (set.affectedRows == 0) {
        return Result<void>::Failure(ErrorCode::NotFound, std::string(id) + " " + std::string(version) + " is not installed");
    }
    const UltraDbResult clear = UltraDb_Exec(connection_, "UPDATE installed SET current = 0 WHERE id = ? AND version <> ?",
                                             {UltraDbValue(std::string(id)), UltraDbValue(std::string(version))});
    if (!clear) return DbFailure(clear, "change the current version of " + std::string(id));
    return Result<void>::Succeed();
}

InstalledVersions SqliteRegistry::Versions() const {
    InstalledVersions out;
    const auto all = All();
    if (!all) return out;
    for (const InstalledRecord& r : all.Value()) out.entries.emplace_back(r.id, r.version);
    return out;
}

std::string SqliteRegistry::JoinDepends(const std::vector<Dependency>& depends) {
    std::string out;
    for (const Dependency& d : depends) {
        if (!out.empty()) out += ' ';
        out += d.id + "@" + d.version;
    }
    return out;
}

std::vector<Dependency> SqliteRegistry::SplitDepends(const std::string& text) {
    std::vector<Dependency> out;
    std::istringstream in(text);
    std::string item;
    while (in >> item) {
        const size_t at = item.find('@');
        if (at == std::string::npos || at == 0 || at + 1 >= item.size()) continue;
        out.push_back({item.substr(0, at), item.substr(at + 1)});
    }
    return out;
}

// ===== licences =====
namespace {
LicenceRecord LicenceOf(const UltraDbRow& row) {
    LicenceRecord l;
    l.app = row["app"].AsString();
    l.licenceId = row["licence_id"].AsString();
    l.orderId = row["order_id"].AsString();
    l.orderSecret = row["order_secret"].AsString();
    l.token = row["token"].AsString();
    l.issuedAt = row["issued_at"].AsString();
    l.amount = row["amount"].AsString();
    l.currency = row["currency"].AsString();
    l.receiptNo = static_cast<int>(row["receipt_no"].AsInt64());
    return l;
}
} // namespace

Result<void> SqliteRegistry::PutLicence(const LicenceRecord& l) {
    if (connection_.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "The registry is not open");
    if (l.app.empty() || l.token.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "A licence names its app and carries a token");
    const UltraDbResult r = UltraDb_Exec(connection_,
        "INSERT OR REPLACE INTO licences (app, licence_id, order_id, order_secret, token, issued_at, amount, currency, receipt_no)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
        {UltraDbValue(l.app), UltraDbValue(l.licenceId), UltraDbValue(l.orderId), UltraDbValue(l.orderSecret), UltraDbValue(l.token),
         UltraDbValue(l.issuedAt), UltraDbValue(l.amount), UltraDbValue(l.currency), UltraDbValue(static_cast<int64_t>(l.receiptNo))});
    if (!r) return DbFailure(r, "keep the licence of " + l.app);
    return Result<void>::Succeed();
}

Result<LicenceRecord> SqliteRegistry::GetLicence(std::string_view app) const {
    using R = Result<LicenceRecord>;
    if (connection_.empty()) return R::Failure(ErrorCode::InvalidArgument, "The registry is not open");
    UltraDbResultSet rs;
    const UltraDbResult r = UltraDb_Query(connection_, "SELECT * FROM licences WHERE app = ?", {UltraDbValue(std::string(app))}, rs);
    if (!r) return R::Failure(ErrorCode::IoError, "The registry could not be read", r.message);
    if (rs.Empty()) return R::Failure(ErrorCode::NotFound, std::string(app) + " was not bought here");
    return R::Succeed(LicenceOf(rs.Row(0)));
}

Result<std::vector<LicenceRecord>> SqliteRegistry::Licences() const {
    using R = Result<std::vector<LicenceRecord>>;
    if (connection_.empty()) return R::Failure(ErrorCode::InvalidArgument, "The registry is not open");
    UltraDbResultSet rs;
    const UltraDbResult r = UltraDb_Query(connection_, "SELECT * FROM licences ORDER BY app", rs);
    if (!r) return R::Failure(ErrorCode::IoError, "The registry could not be read", r.message);
    std::vector<LicenceRecord> out;
    for (const UltraDbRow& row : rs) out.push_back(LicenceOf(row));
    return R::Succeed(std::move(out));
}

Result<void> SqliteRegistry::RemoveLicence(std::string_view app) {
    if (connection_.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "The registry is not open");
    const UltraDbResult r = UltraDb_Exec(connection_, "DELETE FROM licences WHERE app = ?", {UltraDbValue(std::string(app))});
    if (!r) return DbFailure(r, "forget the licence of " + std::string(app));
    return Result<void>::Succeed();
}

} // namespace UltraStore
