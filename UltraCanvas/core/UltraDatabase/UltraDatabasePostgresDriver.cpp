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
//     rather than as the string "42" that happens to coerce. `numeric` stays
//     text: it is exact, and a double is not (12345678901234.5678 does not
//     survive the trip).
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
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDatabase/UltraDatabaseCore.h"
#include "UltraDatabase/UltraDatabasePlugins.h"
#include "UltraDatabase/UltraDatabaseValue.h"

#include "UltraDatabaseInternal.h"

#if defined(ULTRADATABASE_HAS_POSTGRES)

#include <libpq-fe.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Resolving the password through UltraVault. The build defines
// ULTRADATABASE_HAS_VAULT wherever it links UltraVault into UltraDatabase.
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
// numeric (1700) is deliberately not mapped: it arrives as its exact text.

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
                case UltraDbType::Double:
                    // Dot-decimal and round-tripping. AsString goes through
                    // std::to_string, which writes "3,140000" on a
                    // comma-decimal desktop - refused by the server - and
                    // keeps six decimals, so 1e-9 was sent as zero.
                    speicher.emplace_back(ultradb_internal::FormatDoubleClassic(v.AsDouble()));
                    werte.push_back(speicher.back().c_str());
                    laengen.push_back(0);
                    formate.push_back(0);
                    break;
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
        case kOidFloat8: {
            // PostgreSQL always renders these with a dot, whatever the
            // client's locale - so they are read with a dot too. strtod was
            // used here and follows LC_NUMERIC, which a GUI program on a
            // comma-decimal desktop has set: "3.14" came back as 3.
            double wert = 0.0;
            ultradb_internal::ParseDoubleClassic(
                std::string(text, static_cast<size_t>(PQgetlength(res, zeile, spalte))), wert);
            return UltraDbValue(wert);
        }
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

    // Introspection is plain SQL through ExecuteDirect, so it takes the
    // connection mutex statement by statement; see UltraDatabasePostgresSchema.cpp.
    UltraDbResult ListSchemas(std::vector<std::string>& out) override {
        return ultradb_internal::PostgresListSchemas(*this, out);
    }
    UltraDbResult ListObjects(const std::string& schema,
                              std::vector<UltraDbObjectSummary>& out) override {
        return ultradb_internal::PostgresListObjects(*this, schema, out);
    }
    UltraDbResult DescribeTable(const std::string& schema, const std::string& table,
                                UltraDbTableSchema& out) override {
        return ultradb_internal::PostgresDescribeTable(*this, schema, table, out);
    }

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
            // A batch can hold DDL anywhere, and PQexec reports only the
            // last statement's tag; the text is the only place to look.
            if (AendertSchema(sql)) KatalogVergessen();
            UltraDbResult r = Abschliessen(res, out);
            PQclear(res);
            return r;
        }

        if (AendertSchema(sql)) KatalogVergessen();
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
        UltraDbResult r = Abschliessen(res, out);
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
        UltraDbResult r = Abschliessen(res, out);
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
    // ===== RESULT COLUMN METADATA =====
    //
    // libpq gives each result column its type OID and modifier, and - for a
    // column read straight from a table - the table's OID and the column's
    // number. Names for those take a catalog query, so they are cached per
    // connection: the first query on a table pays one round trip, every later
    // one nothing. DDL on this connection empties the cache (a rename keeps
    // the OID and would otherwise report the old name); DDL from another
    // session can leave a stale name until the connection is reopened.

    struct TabellenName {
        std::string schema;
        std::string tabelle;
        std::map<int, std::string> spalten;   // attnum -> name
    };

    // Read the rows, then describe the columns. Called with mutex_ held.
    UltraDbResult Abschliessen(PGresult* res, UltraDbResultSet& out) {
        UltraDbResult r = ErgebnisEinlesen(res, out);
        const char* tag = PQcmdStatus(res);
        if (tag != nullptr && AendertSchema(tag)) KatalogVergessen();
        if (r && PQresultStatus(res) == PGRES_TUPLES_OK) SpaltenBeschreiben(res, out);
        return r;
    }

    // CREATE, ALTER or DROP as a whole word anywhere in the text. Whole words,
    // because `created_at` is a column in half the schemas this runs against
    // and must not empty the cache on every query; a keyword inside a string
    // literal costs a reload, nothing worse.
    static bool AendertSchema(const std::string& text) {
        std::string wort;
        auto pruefen = [&wort]() {
            const bool treffer = wort == "CREATE" || wort == "ALTER" || wort == "DROP";
            wort.clear();
            return treffer;
        };
        for (char c : text) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (std::isalnum(u) || c == '_') {
                wort.push_back(static_cast<char>(std::toupper(u)));
            } else if (pruefen()) {
                return true;
            }
        }
        return pruefen();
    }

    void KatalogVergessen() {
        typNamen_.clear();
        tabellen_.clear();
    }

    void SpaltenBeschreiben(PGresult* res, UltraDbResultSet& out) {
        const int spalten = PQnfields(res);
        std::vector<std::pair<unsigned int, int>> fehlendeTypen;
        std::vector<unsigned int> fehlendeTabellen;
        for (int s = 0; s < spalten; ++s) {
            const std::pair<unsigned int, int> typ{PQftype(res, s), PQfmod(res, s)};
            if (!typNamen_.count(typ) &&
                std::find(fehlendeTypen.begin(), fehlendeTypen.end(), typ) == fehlendeTypen.end())
                fehlendeTypen.push_back(typ);
            const unsigned int tabelle = PQftable(res, s);
            if (tabelle != InvalidOid && !tabellen_.count(tabelle) &&
                std::find(fehlendeTabellen.begin(), fehlendeTabellen.end(), tabelle) ==
                    fehlendeTabellen.end())
                fehlendeTabellen.push_back(tabelle);
        }
        if (!fehlendeTypen.empty() || !fehlendeTabellen.empty())
            KatalogNachladen(fehlendeTypen, fehlendeTabellen);

        std::vector<UltraDbColumnInfo> info;
        info.reserve(static_cast<size_t>(spalten));
        for (int s = 0; s < spalten; ++s) {
            UltraDbColumnInfo ci;
            ci.name = out.Columns()[static_cast<size_t>(s)];
            auto typ = typNamen_.find({PQftype(res, s), PQfmod(res, s)});
            if (typ != typNamen_.end()) ci.declaredType = typ->second;
            auto tabelle = tabellen_.find(PQftable(res, s));
            if (tabelle != tabellen_.end()) {
                auto spalte = tabelle->second.spalten.find(PQftablecol(res, s));
                if (spalte != tabelle->second.spalten.end()) {
                    ci.sourceSchema = tabelle->second.schema;
                    ci.sourceTable  = tabelle->second.tabelle;
                    ci.sourceColumn = spalte->second;
                }
            }
            info.push_back(std::move(ci));
        }
        out.SetColumnInfo(std::move(info));
    }

    // One query for the type names, one for the tables. Inside the caller's
    // transaction they run under a savepoint, so that a failure here - it
    // should never happen, but a cancel or an out-of-memory can - costs the
    // metadata and not the caller's transaction, which PostgreSQL would
    // otherwise abort. In a failed transaction nothing is asked at all.
    void KatalogNachladen(const std::vector<std::pair<unsigned int, int>>& typen,
                          const std::vector<unsigned int>& tabellen) {
        const PGTransactionStatusType zustand = PQtransactionStatus(conn_);
        if (zustand != PQTRANS_IDLE && zustand != PQTRANS_INTRANS) return;
        const bool sicherungspunkt = zustand == PQTRANS_INTRANS;
        if (sicherungspunkt && !Befehl("SAVEPOINT ultradb_spalteninfo")) return;

        bool fehler = false;
        if (!typen.empty()) {
            std::string oids = "{", mods = "{";
            for (size_t i = 0; i < typen.size(); ++i) {
                if (i) { oids += ','; mods += ','; }
                oids += std::to_string(typen[i].first);
                mods += std::to_string(typen[i].second);
            }
            oids += '}'; mods += '}';
            const char* werte[2] = { oids.c_str(), mods.c_str() };
            PGresult* res = PQexecParams(conn_,
                "SELECT u.o::bigint, u.m, pg_catalog.format_type(u.o, u.m)"
                " FROM unnest($1::oid[], $2::int4[]) AS u(o, m)",
                2, nullptr, werte, nullptr, nullptr, 0);
            if (res != nullptr && PQresultStatus(res) == PGRES_TUPLES_OK) {
                for (int z = 0; z < PQntuples(res); ++z)
                    typNamen_[{static_cast<unsigned int>(std::strtoul(PQgetvalue(res, z, 0), nullptr, 10)),
                               static_cast<int>(std::strtol(PQgetvalue(res, z, 1), nullptr, 10))}] =
                        PQgetvalue(res, z, 2);
                for (const auto& typ : typen) typNamen_.emplace(typ, std::string());
            } else {
                fehler = true;
            }
            if (res != nullptr) PQclear(res);
        }
        if (!tabellen.empty() && !fehler) {
            std::string oids = "{";
            for (size_t i = 0; i < tabellen.size(); ++i) {
                if (i) oids += ',';
                oids += std::to_string(tabellen[i]);
            }
            oids += '}';
            const char* werte[1] = { oids.c_str() };
            PGresult* res = PQexecParams(conn_,
                "SELECT c.oid::bigint, n.nspname, c.relname, a.attnum, a.attname"
                " FROM pg_catalog.pg_class c"
                " JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace"
                " JOIN pg_catalog.pg_attribute a ON a.attrelid = c.oid"
                "  AND a.attnum > 0 AND NOT a.attisdropped"
                " WHERE c.oid = ANY($1::oid[])",
                1, nullptr, werte, nullptr, nullptr, 0);
            if (res != nullptr && PQresultStatus(res) == PGRES_TUPLES_OK) {
                for (int z = 0; z < PQntuples(res); ++z) {
                    TabellenName& t = tabellen_[static_cast<unsigned int>(
                        std::strtoul(PQgetvalue(res, z, 0), nullptr, 10))];
                    t.schema  = PQgetvalue(res, z, 1);
                    t.tabelle = PQgetvalue(res, z, 2);
                    t.spalten[static_cast<int>(std::strtol(PQgetvalue(res, z, 3), nullptr, 10))] =
                        PQgetvalue(res, z, 4);
                }
                // A table that has gone is remembered as unknown, so it is
                // not asked about again on every query.
                for (unsigned int oid : tabellen) tabellen_.emplace(oid, TabellenName{});
            } else {
                fehler = true;
            }
            if (res != nullptr) PQclear(res);
        }

        if (sicherungspunkt) {
            if (fehler) Befehl("ROLLBACK TO SAVEPOINT ultradb_spalteninfo");
            Befehl("RELEASE SAVEPOINT ultradb_spalteninfo");
        }
    }

    bool Befehl(const char* sql) {
        PGresult* res = PQexec(conn_, sql);
        const bool ok = res != nullptr && PQresultStatus(res) == PGRES_COMMAND_OK;
        if (res != nullptr) PQclear(res);
        return ok;
    }

    PGconn*    conn_ = nullptr;
    std::mutex mutex_;
    long long  zaehler_ = 0;
    std::map<std::pair<unsigned int, int>, std::string> typNamen_;
    std::map<unsigned int, TabellenName> tabellen_;
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

// Append a value to a libpq keyword/value connection string, quoted and
// escaped. Written straight into the target rather than returned, so the
// password never passes through a temporary that is freed without a wipe.
void AppendEscaped(std::string& ziel, const std::string& wert) {
    ziel.push_back('\'');
    for (char c : wert) {
        if (c == '\'' || c == '\\') ziel.push_back('\\');
        ziel.push_back(c);
    }
    ziel.push_back('\'');
}

// Overwrite a string's bytes before it is released. volatile, so the stores
// are not dropped as dead writes to memory that is about to be freed.
void Wipe(std::string& s) {
    volatile char* p = s.data();
    for (size_t i = 0; i < s.size(); ++i) p[i] = '\0';
    s.clear();
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
    // The application opens the vault (its own file and passphrase); the
    // driver only reads from it. Opening one here would silently fall back to
    // an empty in-memory vault and turn a setup mistake into "not found".
    if (!UltraVault::IsAvailable()) {
        fehler = "the password for '" + schluessel + "' cannot be read: "
                 "UltraVault is not open";
        return false;
    }
    UltraVault::SecretValue geheimnis;
    const UltraVault::Result gelesen = UltraVault::Get(schluessel, geheimnis);
    if (!gelesen) {
        fehler = gelesen.code == UltraVault::ResultCode::NotFound
            ? "the password for '" + schluessel + "' is not in UltraVault"
            : "the password for '" + schluessel + "' could not be read from "
              "UltraVault: " + gelesen.message;
        return false;
    }
    out.assign(geheimnis.bytes.begin(), geheimnis.bytes.end());
    volatile uint8_t* b = geheimnis.bytes.data();
    for (size_t i = 0; i < geheimnis.bytes.size(); ++i) b[i] = 0;
    return true;
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
            // Its own code, not ConnectionFailed: nothing was attempted, and
            // a caller that says "the server cannot be reached" about a key
            // missing from the vault sends the user to check the network.
            error = UltraDbResult::Error(UltraDbResultCode::CredentialsUnavailable,
                                         fehler, kDriverId);
            return nullptr;
        }

        // Room for the whole string up front - every value escaped at worst
        // doubles, plus quotes, keys and separators - so appending never
        // reallocates and frees a buffer that already holds the password.
        size_t platz = 256 + 2 * (config.host.size() + config.database.size() +
                                  config.user.size() + passwort.size() +
                                  config.name.size());
        for (const auto& option : config.options)
            platz += 4 + option.first.size() + 2 * option.second.size();
        std::string verbindung;
        verbindung.reserve(platz);
        auto anhaengen = [&](const char* schluessel, const std::string& wert) {
            if (wert.empty()) return;
            if (!verbindung.empty()) verbindung.push_back(' ');
            verbindung += schluessel;
            verbindung.push_back('=');
            AppendEscaped(verbindung, wert);
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
        // The password has been handed to libpq; neither copy stays here.
        Wipe(passwort);
        Wipe(verbindung);

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
