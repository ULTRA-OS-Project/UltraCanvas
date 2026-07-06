// core/UltraDatabase/UltraDatabaseSqliteDriver.cpp
// The built-in SQLite driver: opens file / in-memory databases and executes
// parameterized statements on top of libsqlite3. Registered automatically the
// first time the manager resolves a driver.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraDatabaseInternal.h"

#include "UltraDatabase/UltraDatabasePlugins.h"

#include <sqlite3.h>

#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>

namespace {

using namespace ultradb_internal;

const char* kDriverId = "sqlite";

UltraDbResultCode MapSqliteError(int rc) {
    switch (rc & 0xFF) {
        case SQLITE_OK:
        case SQLITE_ROW:
        case SQLITE_DONE:       return UltraDbResultCode::Success;
        case SQLITE_CONSTRAINT: return UltraDbResultCode::ConstraintViolation;
        case SQLITE_BUSY:
        case SQLITE_LOCKED:     return UltraDbResultCode::Busy;
        case SQLITE_MISUSE:     return UltraDbResultCode::Misuse;
        case SQLITE_RANGE:      return UltraDbResultCode::InvalidArgument;
        case SQLITE_CANTOPEN:
        case SQLITE_NOTADB:     return UltraDbResultCode::ConnectionFailed;
        case SQLITE_MISMATCH:   return UltraDbResultCode::TypeMismatch;
        case SQLITE_NOTFOUND:   return UltraDbResultCode::NotFound;
        default:                return UltraDbResultCode::QueryFailed;
    }
}

UltraDbResult SqliteError(sqlite3* db, int rc, const std::string& context) {
    std::string msg = context;
    if (db) {
        const char* em = sqlite3_errmsg(db);
        if (em && *em) msg += std::string(": ") + em;
    }
    UltraDbResult r = UltraDbResult::Error(MapSqliteError(rc), msg, kDriverId);
    return r;
}

// Bind params to a prepared statement (indices are 1-based in SQLite).
UltraDbResult BindParams(sqlite3* db, sqlite3_stmt* stmt,
                         const UltraDbParams& params) {
    for (size_t i = 0; i < params.size(); ++i) {
        const UltraDbValue& v = params[i];
        const int idx = static_cast<int>(i) + 1;
        int rc = SQLITE_OK;
        switch (v.Type()) {
            case UltraDbType::Null:
                rc = sqlite3_bind_null(stmt, idx);
                break;
            case UltraDbType::Bool:
            case UltraDbType::Int:
                rc = sqlite3_bind_int64(stmt, idx, v.AsInt64());
                break;
            case UltraDbType::Double:
                rc = sqlite3_bind_double(stmt, idx, v.AsDouble());
                break;
            case UltraDbType::Text: {
                std::string s = v.AsString();
                rc = sqlite3_bind_text(stmt, idx, s.c_str(),
                                       static_cast<int>(s.size()), SQLITE_TRANSIENT);
                break;
            }
            case UltraDbType::Blob: {
                std::vector<uint8_t> b = v.AsBlob();
                rc = sqlite3_bind_blob(stmt, idx, b.empty() ? "" : (const void*)b.data(),
                                       static_cast<int>(b.size()), SQLITE_TRANSIENT);
                break;
            }
        }
        if (rc != SQLITE_OK)
            return SqliteError(db, rc, "bind parameter " + std::to_string(idx));
    }
    return UltraDbResult::Ok();
}

// Read column metadata + step the statement to completion, appending rows to
// `out`. Assumes the statement has already been reset and bound.
UltraDbResult RunStatement(sqlite3* db, sqlite3_stmt* stmt, UltraDbResultSet& out) {
    out.Clear();

    const int ncol = sqlite3_column_count(stmt);
    std::vector<std::string> cols;
    cols.reserve(ncol);
    for (int c = 0; c < ncol; ++c) {
        const char* name = sqlite3_column_name(stmt, c);
        cols.emplace_back(name ? name : "");
    }
    out.SetColumns(std::move(cols));

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        std::vector<UltraDbValue> vals;
        vals.reserve(ncol);
        for (int c = 0; c < ncol; ++c) {
            switch (sqlite3_column_type(stmt, c)) {
                case SQLITE_INTEGER:
                    vals.emplace_back(static_cast<int64_t>(sqlite3_column_int64(stmt, c)));
                    break;
                case SQLITE_FLOAT:
                    vals.emplace_back(sqlite3_column_double(stmt, c));
                    break;
                case SQLITE_TEXT: {
                    const unsigned char* t = sqlite3_column_text(stmt, c);
                    int n = sqlite3_column_bytes(stmt, c);
                    vals.emplace_back(std::string(reinterpret_cast<const char*>(t),
                                                  static_cast<size_t>(n)));
                    break;
                }
                case SQLITE_BLOB: {
                    const uint8_t* b = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, c));
                    int n = sqlite3_column_bytes(stmt, c);
                    vals.emplace_back(std::vector<uint8_t>(b, b + n));
                    break;
                }
                case SQLITE_NULL:
                default:
                    vals.emplace_back(nullptr);
                    break;
            }
        }
        out.AddRow(std::move(vals));
    }

    UltraDbResult result;
    if (rc != SQLITE_DONE)
        return SqliteError(db, rc, "step");

    result.affectedRows = sqlite3_changes(db);
    result.lastInsertId = sqlite3_last_insert_rowid(db);
    return result;
}

// ---- Statement -------------------------------------------------------------

class SqliteConnection;

class SqliteStatement : public IUltraDbStatement {
public:
    SqliteStatement(SqliteConnection* owner, sqlite3_stmt* stmt)
        : owner_(owner), stmt_(stmt) {}
    ~SqliteStatement() override { if (stmt_) sqlite3_finalize(stmt_); }

    UltraDbResult Execute(const UltraDbParams& params, UltraDbResultSet& out) override;

private:
    SqliteConnection* owner_;
    sqlite3_stmt*     stmt_;
};

// ---- Connection ------------------------------------------------------------

class SqliteConnection : public IUltraDbConnection {
public:
    explicit SqliteConnection(sqlite3* db) : db_(db) {}
    ~SqliteConnection() override { if (db_) sqlite3_close_v2(db_); }

    std::string DriverName() const override { return kDriverId; }

    std::unique_ptr<IUltraDbStatement> Prepare(const std::string& sql,
                                               UltraDbResult& error) override {
        std::lock_guard<std::mutex> lk(mtx_);
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK || !stmt) {
            error = SqliteError(db_, rc, "prepare");
            if (stmt) sqlite3_finalize(stmt);
            return nullptr;
        }
        error = UltraDbResult::Ok();
        return std::make_unique<SqliteStatement>(this, stmt);
    }

    UltraDbResult ExecuteDirect(const std::string& sql, const UltraDbParams& params,
                                UltraDbResultSet& out) override {
        std::lock_guard<std::mutex> lk(mtx_);
        out.Clear();

        // With parameters, exactly one statement is expected.
        if (!params.empty()) {
            sqlite3_stmt* stmt = nullptr;
            int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
            if (rc != SQLITE_OK || !stmt) {
                if (stmt) sqlite3_finalize(stmt);
                return SqliteError(db_, rc, "prepare");
            }
            UltraDbResult bindRes = BindParams(db_, stmt, params);
            if (!bindRes) { sqlite3_finalize(stmt); return bindRes; }
            UltraDbResult res = RunStatement(db_, stmt, out);
            sqlite3_finalize(stmt);
            return res;
        }

        // No parameters: allow a batch of ';'-separated statements. Rows from
        // the last row-returning statement are kept as the visible output.
        const char* tail = sql.c_str();
        UltraDbResult meta = UltraDbResult::Ok();
        while (tail && *tail) {
            sqlite3_stmt* stmt = nullptr;
            const char* next = nullptr;
            int rc = sqlite3_prepare_v2(db_, tail, -1, &stmt, &next);
            if (rc != SQLITE_OK)
                return SqliteError(db_, rc, "prepare");
            if (!stmt) { tail = next; continue; }  // trailing whitespace/comment

            UltraDbResultSet localRows;
            UltraDbResult res = RunStatement(db_, stmt, localRows);
            sqlite3_finalize(stmt);
            if (!res) return res;

            out = std::move(localRows);
            meta.affectedRows = res.affectedRows;
            meta.lastInsertId = res.lastInsertId;
            tail = next;
        }
        return meta;
    }

    // Called by SqliteStatement::Execute under the connection mutex.
    UltraDbResult ExecuteStatement(sqlite3_stmt* stmt, const UltraDbParams& params,
                                   UltraDbResultSet& out) {
        std::lock_guard<std::mutex> lk(mtx_);
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        UltraDbResult bindRes = BindParams(db_, stmt, params);
        if (!bindRes) return bindRes;
        return RunStatement(db_, stmt, out);
    }

private:
    sqlite3*   db_;
    std::mutex mtx_;
};

UltraDbResult SqliteStatement::Execute(const UltraDbParams& params, UltraDbResultSet& out) {
    if (!stmt_ || !owner_)
        return UltraDbResult::Error(UltraDbResultCode::Misuse, "statement finalized", kDriverId);
    return owner_->ExecuteStatement(stmt_, params, out);
}

// ---- Driver ----------------------------------------------------------------

class SqliteDriver : public IUltraDbDriverPlugin {
public:
    std::string GetName() const override { return "UltraDatabase SQLite driver"; }

    std::vector<std::string> GetDriverIds() const override {
        return {"sqlite", "sqlite3"};
    }

    std::unique_ptr<IUltraDbConnection> Open(const UltraDbConnectionConfig& config,
                                             UltraDbResult& error) override {
        std::string path = config.database;
        if (path.empty())
            path = ":memory:";
        else if (path != ":memory:")
            path = ExpandUserPath(path);

        int flags = config.readOnly
                        ? SQLITE_OPEN_READONLY
                        : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
        flags |= SQLITE_OPEN_FULLMUTEX;  // serialized; we also guard with our mutex

        sqlite3* db = nullptr;
        int rc = sqlite3_open_v2(path.c_str(), &db, flags, nullptr);
        if (rc != SQLITE_OK) {
            error = SqliteError(db, rc, "open '" + path + "'");
            if (db) sqlite3_close_v2(db);
            return nullptr;
        }
        sqlite3_busy_timeout(db, 5000);
        sqlite3_exec(db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

        error = UltraDbResult::Ok();
        return std::make_unique<SqliteConnection>(db);
    }
};

} // namespace

namespace ultradb_internal {

std::string ExpandUserPath(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home || !*home) return path;
    // "~" or "~/..." -> "$HOME" + rest
    if (path.size() == 1) return home;
    if (path[1] == '/') return std::string(home) + path.substr(1);
    return path;  // "~user" form unsupported; leave as-is
}

IUltraDbDriverPlugin* BuiltinSqliteDriver() {
    static SqliteDriver driver;
    static std::once_flag once;
    std::call_once(once, [] { UltraDatabase_RegisterDriver(&driver); });
    return &driver;
}

} // namespace ultradb_internal
