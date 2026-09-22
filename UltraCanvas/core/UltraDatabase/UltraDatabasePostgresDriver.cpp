// core/UltraDatabase/UltraDatabasePostgresDriver.cpp
// The PostgreSQL driver, over libpq.
//
// This is what turns "the same schema, local or on a server" from a promise
// into something that runs. Every caller keeps its SQL: the translation that
// makes that true lives here, and there are exactly four pieces of it.
//
//  1. **Placeholders.** Callers write `?`, as SQLite takes; PostgreSQL wants
//     `$1, $2, ...`. Rewriting them is this driver's job, not the callers' -
//     the whole point of a driver layer is that a query written once runs on
//     both. The rewrite understands string literals, quoted identifiers,
//     dollar-quoted bodies and comments, so a `?` inside text stays a `?`.
//  2. **Types.** libpq hands back text and a column OID. The common OIDs are
//     mapped to real types so an INTEGER column reads back as an integer
//     rather than as the string "42" that happens to coerce.
//  3. **TLS is on by default and verified.** `UltraDbTls::VerifyFull` maps to
//     libpq's `verify-full`, which checks the certificate *and* the hostname.
//     A shared accounting database reached over anything but a local socket is
//     exactly the case where a downgrade must be a deliberate act.
//  4. **The password never appears in a config file.** `credentials` is an
//     UltraVault key; it is resolved here, used, and not retained. A
//     connection string with a literal password would end up in a log, a core
//     dump or a screenshot.
//
// Built only when libpq is present (`ULTRADATABASE_HAS_POSTGRES`); without it
// the driver is simply not registered and an attempt to open a "postgresql"
// connection reports that the driver is missing, which is a true answer rather
// than a link error.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDatabase/UltraDatabaseCore.h"
#include "UltraDatabase/UltraDatabasePlugins.h"
#include "UltraDatabase/UltraDatabaseValue.h"

#include "UltraDatabaseInternal.h"

#if defined(ULTRADATABASE_HAS_POSTGRES)

#include <libpq-fe.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Resolving the password through UltraVault when that module is available.
#if defined(ULTRADATABASE_HAS_VAULT)
  #include "UltraVault/UltraVault.h"
#endif

namespace {

const char* const kDriverId = "postgresql";

// PostgreSQL type OIDs, from the catalog. Hardcoded because they are fixed by
// the wire protocol and have not changed since the format was frozen; looking
// them up per query would cost a round trip to learn what is already known.
constexpr unsigned int kOidBool    = 16;
constexpr unsigned int kOidBytea   = 17;
constexpr unsigned int kOidInt8    = 20;
constexpr unsigned int kOidInt2    = 21;
constexpr unsigned int kOidInt4    = 23;
constexpr unsigned int kOidFloat4  = 700;
constexpr unsigned int kOidFloat8  = 701;
constexpr unsigned int kOidNumeric = 1700;

UltraDbResultCode MapPqError(const PGresult* res) {
    const char* state = res ? PQresultErrorField(res, PG_DIAG_SQLSTATE) : nullptr;
    if (state == nullptr) return UltraDbResultCode::QueryFailed;
    const std::string code(state);
    // SQLSTATE classes worth distinguishing, because a caller reacts
    // differently to each.
    if (code == "23505") return UltraDbResultCode::ConstraintViolation;  // unique
    if (code.rfind("23", 0) == 0) return UltraDbResultCode::ConstraintViolation;
    if (code.rfind("08", 0) == 0) return UltraDbResultCode::ConnectionFailed;
    if (code == "40001" || code == "40P01") return UltraDbResultCode::Busy;  // serialise / deadlock
    if (code.rfind("42", 0) == 0) return UltraDbResultCode::QueryFailed;
    return UltraDbResultCode::QueryFailed;
}

UltraDbResult PqError(PGconn* conn, PGresult* res, const std::string& context) {
    std::string msg = context;
    const char* detail = res ? PQresultErrorMessage(res) : PQerrorMessage(conn);
    if (detail != nullptr && *detail != '\0') {
        std::string d(detail);
        while (!d.empty() && (d.back() == '\n' || d.back() == '\r')) d.pop_back();
        msg += ": " + d;
    }
    UltraDbResult r = UltraDbResult::Error(MapPqError(res), msg, kDriverId);
    const char* state = res ? PQresultErrorField(res, PG_DIAG_SQLSTATE) : nullptr;
    if (state != nullptr) r.sqlState = state;
    return r;
}

// ===== PLACEHOLDER TRANSLATION =====
//
// `?` -> `$1, $2, ...`. The rewriter itself is in UltraDatabasePostgresSql.cpp
// so that it is compiled and tested even where libpq is absent.

// ===== PARAMETERS =====

// libpq takes parameters as text (or binary). Text keeps this simple and
// costs nothing measurable at this scale; a null is a null pointer, which is
// how libpq distinguishes it from an empty string.
struct GebundeneParameter {
    std::vector<std::string>  speicher;
    std::vector<const char*>  werte;
    std::vector<int>          laengen;
    std::vector<int>          formate;

    explicit GebundeneParameter(const UltraDbParams& params) {
        speicher.reserve(params.size());
        werte.reserve(params.size());
        laengen.reserve(params.size());
        formate.reserve(params.size());
        for (const UltraDbValue& v : params) {
            switch (v.Type()) {
                case UltraDbType::Null:
                    speicher.emplace_back();
                    werte.push_back(nullptr);
                    laengen.push_back(0);
                    formate.push_back(0);
                    break;
                case UltraDbType::Bool:
                    speicher.emplace_back(v.AsBool() ? "t" : "f");
                    werte.push_back(speicher.back().c_str());
                    laengen.push_back(0);
                    formate.push_back(0);
                    break;
                case UltraDbType::Blob: {
                    // bytea in its hex form, which is what a modern server
                    // expects and what PQescapeByteaConn would produce anyway.
                    static const char* const hex = "0123456789abcdef";
                    const std::vector<uint8_t>& bytes = v.AsBlob();
                    std::string text = "\\x";
                    text.reserve(2 + bytes.size() * 2);
                    for (uint8_t b : bytes) {
                        text.push_back(hex[b >> 4]);
                        text.push_back(hex[b & 0x0F]);
                    }
                    speicher.emplace_back(std::move(text));
                    werte.push_back(speicher.back().c_str());
                    laengen.push_back(0);
                    formate.push_back(0);
                    break;
                }
                default:
                    speicher.emplace_back(v.AsString());
                    werte.push_back(speicher.back().c_str());
                    laengen.push_back(0);
                    formate.push_back(0);
                    break;
            }
        }
        // The vector may have reallocated while it grew, which would leave the
        // pointers dangling. They are rebuilt once everything is in place.
        for (size_t i = 0; i < speicher.size(); ++i)
            if (werte[i] != nullptr) werte[i] = speicher[i].c_str();
    }

    int Anzahl() const { return static_cast<int>(werte.size()); }
    const char* const* Werte() const { return werte.empty() ? nullptr : werte.data(); }
};

// One byte of a bytea hex string.
bool HexZiffer(char c, uint8_t& out) {
    if (c >= '0' && c <= '9') { out = static_cast<uint8_t>(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { out = static_cast<uint8_t>(c - 'a' + 10); return true; }
    if (c >= 'A' && c <= 'F') { out = static_cast<uint8_t>(c - 'A' + 10); return true; }
    return false;
}

UltraDbValue WertAusFeld(PGresult* res, int zeile, int spalte) {
    if (PQgetisnull(res, zeile, spalte)) return UltraDbValue();
    const char* text = PQgetvalue(res, zeile, spalte);
    const unsigned int oid = PQftype(res, spalte);
    switch (oid) {
        case kOidBool:
            return UltraDbValue(text != nullptr && (*text == 't' || *text == 'T'));
        case kOidInt2:
        case kOidInt4:
        case kOidInt8:
            return UltraDbValue(static_cast<int64_t>(std::strtoll(text, nullptr, 10)));
        case kOidFloat4:
        case kOidFloat8:
        case kOidNumeric:
            // PostgreSQL always renders these with a dot, whatever the
            // client's locale; strtod under the C locale reads them back.
            return UltraDbValue(std::strtod(text, nullptr));
        case kOidBytea: {
            std::vector<uint8_t> bytes;
            const size_t laenge = static_cast<size_t>(PQgetlength(res, zeile, spalte));
            if (laenge >= 2 && text[0] == '\\' && text[1] == 'x') {
                bytes.reserve((laenge - 2) / 2);
                for (size_t i = 2; i + 1 < laenge; i += 2) {
                    uint8_t hoch = 0, tief = 0;
                    if (!HexZiffer(text[i], hoch) || !HexZiffer(text[i + 1], tief)) break;
                    bytes.push_back(static_cast<uint8_t>((hoch << 4) | tief));
                }
            }
            return UltraDbValue(std::move(bytes));
        }
        default:
            return UltraDbValue(std::string(text, static_cast<size_t>(
                PQgetlength(res, zeile, spalte))));
    }
}

UltraDbResult ErgebnisEinlesen(PGresult* res, UltraDbResultSet& out) {
    const ExecStatusType status = PQresultStatus(res);
    if (status == PGRES_TUPLES_OK) {
        const int zeilen  = PQntuples(res);
        const int spalten = PQnfields(res);
        std::vector<std::string> namen;
        namen.reserve(static_cast<size_t>(spalten));
        for (int s = 0; s < spalten; ++s) namen.emplace_back(PQfname(res, s));
        out.SetColumns(std::move(namen));

        for (int z = 0; z < zeilen; ++z) {
            std::vector<UltraDbValue> werte;
            werte.reserve(static_cast<size_t>(spalten));
            for (int s = 0; s < spalten; ++s) werte.push_back(WertAusFeld(res, z, s));
            out.AddRow(std::move(werte));
        }
        UltraDbResult r = UltraDbResult::Ok();
        r.affectedRows = zeilen;
        return r;
    }
    if (status == PGRES_COMMAND_OK) {
        UltraDbResult r = UltraDbResult::Ok();
        const char* betroffen = PQcmdTuples(res);
        r.affectedRows = (betroffen != nullptr && *betroffen != '\0')
                             ? std::atoll(betroffen) : 0;
        return r;
    }
    return UltraDbResult::Error(UltraDbResultCode::QueryFailed,
                                "unexpected result status", kDriverId);
}

// ===== CONNECTION =====

class PostgresConnection;

class PostgresStatement : public IUltraDbStatement {
public:
    PostgresStatement(PostgresConnection* owner, std::string name, std::string sql)
        : owner_(owner), name_(std::move(name)), sql_(std::move(sql)) {}
    ~PostgresStatement() override;

    UltraDbResult Execute(const UltraDbParams& params, UltraDbResultSet& out) override;

private:
    PostgresConnection* owner_;
    std::string name_;
    std::string sql_;
};

class PostgresConnection : public IUltraDbConnection {
public:
    explicit PostgresConnection(PGconn* conn) : conn_(conn) {}
    ~PostgresConnection() override {
        if (conn_ != nullptr) PQfinish(conn_);
    }

    std::string DriverName() const override { return kDriverId; }

    // PostgreSQL's plain BEGIN. It has no equivalent of SQLite's
    // BEGIN IMMEDIATE and rejects the word outright, which is why the core
    // asks the driver instead of assuming.
    std::string BeginTransactionSql() const override { return "BEGIN"; }

    // READ COMMITTED lets two transactions read the same counter and both
    // write back the same successor. FOR UPDATE makes the second one wait.
    std::string RowLockSuffix() const override { return " FOR UPDATE"; }

    std::unique_ptr<IUltraDbStatement> Prepare(const std::string& sql,
                                               UltraDbResult& error) override {
        std::lock_guard<std::mutex> lk(mutex_);
        const std::string uebersetzt = ultradb_internal::PostgresPlatzhalter(sql);
        const std::string name = "ucstmt_" + std::to_string(++zaehler_);
        PGresult* res = PQprepare(conn_, name.c_str(), uebersetzt.c_str(), 0, nullptr);
        if (res == nullptr || PQresultStatus(res) != PGRES_COMMAND_OK) {
            error = PqError(conn_, res, "prepare failed");
            if (res != nullptr) PQclear(res);
            return nullptr;
        }
        PQclear(res);
        error = UltraDbResult::Ok();
        return std::unique_ptr<IUltraDbStatement>(
            new PostgresStatement(this, name, uebersetzt));
    }

    UltraDbResult ExecuteDirect(const std::string& sql, const UltraDbParams& params,
                                UltraDbResultSet& out) override {
        out.Clear();
        std::lock_guard<std::mutex> lk(mutex_);

        if (params.empty()) {
            // A batch of ';'-separated statements. PQexec runs them all, which
            // is what the migration steps rely on.
            PGresult* res = PQexec(conn_, sql.c_str());
            if (res == nullptr)
                return PqError(conn_, nullptr, "execute failed");
            const ExecStatusType status = PQresultStatus(res);
            if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
                UltraDbResult r = PqError(conn_, res, "execute failed");
                PQclear(res);
                return r;
            }
            UltraDbResult r = ErgebnisEinlesen(res, out);
            PQclear(res);
            return r;
        }

        const std::string uebersetzt = ultradb_internal::PostgresPlatzhalter(sql);
        GebundeneParameter gebunden(params);
        PGresult* res = PQexecParams(conn_, uebersetzt.c_str(), gebunden.Anzahl(),
                                     nullptr, gebunden.Werte(), nullptr, nullptr, 0);
        if (res == nullptr) return PqError(conn_, nullptr, "execute failed");
        const ExecStatusType status = PQresultStatus(res);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
            UltraDbResult r = PqError(conn_, res, "execute failed");
            PQclear(res);
            return r;
        }
        UltraDbResult r = ErgebnisEinlesen(res, out);
        PQclear(res);
        return r;
    }

    UltraDbResult ExecutePrepared(const std::string& name, const UltraDbParams& params,
                                  UltraDbResultSet& out) {
        out.Clear();
        std::lock_guard<std::mutex> lk(mutex_);
        GebundeneParameter gebunden(params);
        PGresult* res = PQexecPrepared(conn_, name.c_str(), gebunden.Anzahl(),
                                       gebunden.Werte(), nullptr, nullptr, 0);
        if (res == nullptr) return PqError(conn_, nullptr, "execute failed");
        const ExecStatusType status = PQresultStatus(res);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
            UltraDbResult r = PqError(conn_, res, "execute failed");
            PQclear(res);
            return r;
        }
        UltraDbResult r = ErgebnisEinlesen(res, out);
        PQclear(res);
        return r;
    }

    void Deallocate(const std::string& name) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (conn_ == nullptr || PQstatus(conn_) != CONNECTION_OK) return;
        PGresult* res = PQexec(conn_, ("DEALLOCATE " + name).c_str());
        if (res != nullptr) PQclear(res);
    }

private:
    PGconn*    conn_ = nullptr;
    std::mutex mutex_;
    long long  zaehler_ = 0;
};

PostgresStatement::~PostgresStatement() {
    if (owner_ != nullptr) owner_->Deallocate(name_);
}

UltraDbResult PostgresStatement::Execute(const UltraDbParams& params,
                                         UltraDbResultSet& out) {
    return owner_->ExecutePrepared(name_, params, out);
}

// ===== DRIVER =====

std::string TlsModus(UltraDbTls tls) {
    switch (tls) {
        case UltraDbTls::Disable:    return "disable";
        case UltraDbTls::Prefer:     return "prefer";
        case UltraDbTls::Require:    return "require";
        case UltraDbTls::VerifyFull: return "verify-full";
    }
    return "verify-full";
}

// Escape a value for a libpq keyword/value connection string.
std::string Escape(const std::string& wert) {
    std::string aus = "'";
    for (char c : wert) {
        if (c == '\'' || c == '\\') aus.push_back('\\');
        aus.push_back(c);
    }
    aus.push_back('\'');
    return aus;
}

// Resolve the password. `credentials` names an UltraVault entry; a literal
// password in a config file is not supported, deliberately - it would end up
// in a backup, a log or a screenshot.
bool Passwort(const UltraDbConnectionConfig& config, std::string& out,
              std::string& fehler) {
    out.clear();
    if (config.credentials.empty()) return true;   // trust / peer / .pgpass

    std::string schluessel = config.credentials;
    const std::string praefix = "vault:";
    if (schluessel.rfind(praefix, 0) == 0) schluessel = schluessel.substr(praefix.size());

#if defined(ULTRADATABASE_HAS_VAULT)
    if (UltraVault_GetSecret(schluessel, out)) return true;
    fehler = "the password for '" + schluessel + "' is not in UltraVault";
    return false;
#else
    fehler = "credentials were requested ('" + config.credentials +
             "') but UltraVault is not built in, and a literal password in a "
             "configuration file is not supported";
    return false;
#endif
}

class PostgresDriver : public IUltraDbDriverPlugin {
public:
    std::string GetName() const override { return "PostgreSQL (libpq)"; }

    std::vector<std::string> GetDriverIds() const override {
        return { "postgresql", "postgres", "pgsql" };
    }

    std::unique_ptr<IUltraDbConnection> Open(const UltraDbConnectionConfig& config,
                                             UltraDbResult& error) override {
        std::string passwort, fehler;
        if (!Passwort(config, passwort, fehler)) {
            error = UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, fehler,
                                         kDriverId);
            return nullptr;
        }

        std::string verbindung;
        auto anhaengen = [&](const char* schluessel, const std::string& wert) {
            if (wert.empty()) return;
            if (!verbindung.empty()) verbindung.push_back(' ');
            verbindung += schluessel;
            verbindung.push_back('=');
            verbindung += Escape(wert);
        };
        anhaengen("host", config.host);
        if (config.port > 0) anhaengen("port", std::to_string(config.port));
        anhaengen("dbname", config.database);
        anhaengen("user", config.user);
        anhaengen("password", passwort);
        anhaengen("sslmode", TlsModus(config.tls));
        anhaengen("application_name", config.name.empty() ? "UltraCanvas" : config.name);
        for (const auto& option : config.options) anhaengen(option.first.c_str(), option.second);

        PGconn* conn = PQconnectdb(verbindung.c_str());
        // The password has been handed to libpq; it does not stay here.
        std::fill(passwort.begin(), passwort.end(), '\0');
        verbindung.clear();

        if (conn == nullptr) {
            error = UltraDbResult::Error(UltraDbResultCode::ConnectionFailed,
                                         "could not allocate a connection", kDriverId);
            return nullptr;
        }
        if (PQstatus(conn) != CONNECTION_OK) {
            std::string msg = PQerrorMessage(conn);
            while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();
            PQfinish(conn);
            error = UltraDbResult::Error(UltraDbResultCode::ConnectionFailed,
                                         "connection failed: " + msg, kDriverId);
            return nullptr;
        }

        // A read-only connection is made read-only by the server rather than by
        // convention, so a mistake in a caller cannot write through it.
        if (config.readOnly) {
            PGresult* res = PQexec(conn, "SET default_transaction_read_only = on");
            if (res != nullptr) PQclear(res);
        }

        error = UltraDbResult::Ok();
        return std::unique_ptr<IUltraDbConnection>(new PostgresConnection(conn));
    }
};

} // namespace

namespace ultradb_internal {

// Registered on first call, the same way the SQLite driver is. The manager
// calls this from EnsureBuiltins, which is also what keeps this translation
// unit in the link when the core is a static library.
IUltraDbDriverPlugin* BuiltinPostgresDriver() {
    static PostgresDriver driver;
    static std::once_flag once;
    std::call_once(once, [] { UltraDatabase_RegisterDriver(&driver); });
    return &driver;
}

} // namespace ultradb_internal

#else  // !ULTRADATABASE_HAS_POSTGRES

#include "UltraDatabaseInternal.h"

namespace ultradb_internal {

// Built without libpq: there is no driver, and saying so plainly is better
// than a link error or a connection that fails for an unrelated-looking
// reason.
IUltraDbDriverPlugin* BuiltinPostgresDriver() { return nullptr; }

} // namespace ultradb_internal

#endif
