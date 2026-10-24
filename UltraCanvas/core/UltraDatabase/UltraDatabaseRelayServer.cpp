// core/UltraDatabase/UltraDatabaseRelayServer.cpp
// The relay's server side (UltraDatabaseRelay.h): a session's requests run
// on connections registered under names of the relay's own - "relay#<session>
// #<n>" - so every call goes through the public UltraDb_* functions, with the
// vault, tunnels and observers they bring; the client sees only the handles.
//
// Requests and answers (each carries the request's "id"; an answer "ok" and
// on failure "result", the outcome):
//   hello   {token}                      -> {user, connections: [{name, engine, readOnly}]}
//   open    {connection}                 -> {c, engine}
//   close   {c}
//   exec    {c, sql, params}             -> {result, rows}
//   batch   {c, sql}                     -> {result, statements}
//   cursor  {c, sql, params}             -> {cursor}
//   fetch   {cursor, max}                -> {rows, exhausted}
//   uncursor{cursor}
//   cancel  {c}
//   schemas {c}                          -> {schemas}
//   objects {c, schema}                  -> {objects}
//   describe{c, schema, table}           -> {table}
//   server  {c, view, filter, schema}    -> {rows}       sessions / settings / sizes / status
//   stopsql {c, target, end}             -> {sql}
//   stop    {c, target, end}             -> {stopped}
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraDatabase/UltraDatabaseRelay.h"

#include "UltraDatabase/UltraDatabase.h"
#include "UltraDatabaseRelayJson.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>

using ultradb_relay::Json;

namespace {

// ===== SHA-256 (FIPS 180-4), for the tokens =====

constexpr std::array<uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

std::string Sha256Hex(const std::string& input) {
    std::array<uint32_t, 8> h = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    std::string m = input;
    const uint64_t bits = static_cast<uint64_t>(input.size()) * 8;
    m += static_cast<char>(0x80);
    while (m.size() % 64 != 56) m += '\0';
    for (int i = 7; i >= 0; --i) m += static_cast<char>((bits >> (i * 8)) & 0xFF);
    for (size_t chunk = 0; chunk < m.size(); chunk += 64) {
        std::array<uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<uint32_t>(static_cast<unsigned char>(m[chunk + i * 4])) << 24) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(m[chunk + i * 4 + 1])) << 16) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(m[chunk + i * 4 + 2])) << 8) |
                   static_cast<uint32_t>(static_cast<unsigned char>(m[chunk + i * 4 + 3]));
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
            const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::string hex;
    char buffer[9];
    for (uint32_t v : h) {
        std::snprintf(buffer, sizeof buffer, "%08x", v);
        hex += buffer;
    }
    return hex;
}

// Two strings compared in time that does not depend on where they differ.
bool SameSecret(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

// "postgresql", "mysql" or "sqlite", whatever id the config used.
std::string EngineOf(const std::string& driver) {
    if (driver == "postgres" || driver == "postgresql" || driver == "pg") return "postgresql";
    if (driver == "mysql" || driver == "mariadb") return "mysql";
    return driver == "sqlite3" ? "sqlite" : driver;
}

struct Session {
    uint64_t id = 0;
    std::string peer;
    std::string user;                                 // "" until hello
    std::map<std::string, UltraDbRelayAccess> access;
    std::mutex m;
    std::map<int64_t, std::string> connections;       // handle -> the relay's own name
    std::map<int64_t, UltraDbHandle> cursors;         // handle -> the module's cursor
    int64_t next = 1;
};

// Who a connection of the relay's own serves: for the audit log.
struct Served {
    std::string user, peer, connection;
};

// Shared with the statement observer, which may still be reporting when the
// server is gone (UltraDb_RemoveStatementObserver).
struct AuditState {
    std::mutex m;
    std::map<std::string, Served> served;
    std::function<void(const UltraDbRelayAuditEntry&)> audit;
};

Json Answer(int64_t id) {
    Json a = Json::Object();
    a.Set("id", Json::Int(id));
    a.Set("ok", Json::Truth(true));
    return a;
}

std::string Refused(int64_t id, const UltraDbResult& r) {
    Json a = Json::Object();
    a.Set("id", Json::Int(id));
    a.Set("ok", Json::Truth(false));
    a.Set("result", ultradb_relay::FromResult(r));
    return a.Dump();
}

std::string Refused(int64_t id, UltraDbResultCode code, const std::string& message) {
    return Refused(id, UltraDbResult::Error(code, message, "relay"));
}

} // namespace

struct UltraDbRelayServer::Impl {
    std::mutex m;
    std::map<std::string, UltraDbConnectionConfig> offered;
    std::vector<UltraDbRelayUser> users;
    UltraDbRelayLimits limits;
    std::map<uint64_t, std::shared_ptr<Session>> sessions;
    std::map<std::string, std::deque<std::chrono::steady_clock::time_point>> requests;   // by user
    uint64_t nextSession = 1;
    std::shared_ptr<AuditState> audit = std::make_shared<AuditState>();
    int observer = 0;

    std::shared_ptr<Session> Find(uint64_t id) {
        std::lock_guard<std::mutex> lock(m);
        const auto it = sessions.find(id);
        return it == sessions.end() ? nullptr : it->second;
    }

    // Within the user's rate; counts this request.
    bool Allow(const std::string& user) {
        std::lock_guard<std::mutex> lock(m);
        if (limits.requestsPerMinute <= 0) return true;
        auto& times = requests[user];
        const auto now = std::chrono::steady_clock::now();
        while (!times.empty() && now - times.front() > std::chrono::minutes(1)) times.pop_front();
        if (times.size() >= static_cast<size_t>(limits.requestsPerMinute)) return false;
        times.push_back(now);
        return true;
    }

    void Release(Session& s) {
        std::map<int64_t, std::string> connections;
        std::map<int64_t, UltraDbHandle> cursors;
        {
            std::lock_guard<std::mutex> lock(s.m);
            connections.swap(s.connections);
            cursors.swap(s.cursors);
        }
        for (const auto& [unused, cursor] : cursors) UltraDb_CloseCursor(cursor);
        for (const auto& [unused, name] : connections) {
            UltraDb_CancelQuery(name);
            UltraDb_CloseConnection(name);
            std::lock_guard<std::mutex> lock(audit->m);
            audit->served.erase(name);
        }
    }
};

UltraDbRelayServer::UltraDbRelayServer() : impl_(std::make_unique<Impl>()) {
    std::shared_ptr<AuditState> audit = impl_->audit;
    impl_->observer = UltraDb_AddStatementObserver([audit](const UltraDbStatementEvent& event) {
        std::function<void(const UltraDbRelayAuditEntry&)> report;
        UltraDbRelayAuditEntry entry;
        {
            std::lock_guard<std::mutex> lock(audit->m);
            const auto it = audit->served.find(event.connection);
            if (it == audit->served.end() || !audit->audit) return;
            report = audit->audit;
            entry.user = it->second.user;
            entry.peer = it->second.peer;
            entry.connection = it->second.connection;
        }
        entry.sql = event.sql;
        entry.result = event.result;
        report(entry);
    });
}

UltraDbRelayServer::~UltraDbRelayServer() {
    std::map<uint64_t, std::shared_ptr<Session>> sessions;
    {
        std::lock_guard<std::mutex> lock(impl_->m);
        sessions.swap(impl_->sessions);
    }
    for (auto& [unused, session] : sessions) impl_->Release(*session);
    UltraDb_RemoveStatementObserver(impl_->observer);
}

void UltraDbRelayServer::AddConnection(const std::string& name, const UltraDbConnectionConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->m);
    impl_->offered[name] = config;
}

void UltraDbRelayServer::AddUser(const UltraDbRelayUser& user) {
    std::lock_guard<std::mutex> lock(impl_->m);
    impl_->users.push_back(user);
}

void UltraDbRelayServer::SetLimits(const UltraDbRelayLimits& limits) {
    std::lock_guard<std::mutex> lock(impl_->m);
    impl_->limits = limits;
}

void UltraDbRelayServer::SetAudit(std::function<void(const UltraDbRelayAuditEntry&)> audit) {
    std::lock_guard<std::mutex> lock(impl_->audit->m);
    impl_->audit->audit = std::move(audit);
}

uint64_t UltraDbRelayServer::OpenSession(const std::string& peer) {
    auto session = std::make_shared<Session>();
    session->peer = peer;
    std::lock_guard<std::mutex> lock(impl_->m);
    session->id = impl_->nextSession++;
    impl_->sessions[session->id] = session;
    return session->id;
}

void UltraDbRelayServer::CloseSession(uint64_t id) {
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(impl_->m);
        const auto it = impl_->sessions.find(id);
        if (it == impl_->sessions.end()) return;
        session = it->second;
        impl_->sessions.erase(it);
    }
    impl_->Release(*session);
}

std::string UltraDbRelayServer::Handle(uint64_t sessionId, const std::string& request) {
    Json req;
    std::string error;
    if (!Json::Parse(request, req, error) || req.type != Json::Type::Object)
        return Refused(0, UltraDbResultCode::InvalidArgument, "not a request: " + error);
    const int64_t id = req.Num("id");
    const std::string op = req.Str("op");
    std::shared_ptr<Session> session = impl_->Find(sessionId);
    if (!session) return Refused(id, UltraDbResultCode::ConnectionNotFound, "no such session");

    // ----- who is asking -----
    if (op == "hello") {
        const std::string hash = Sha256Hex(req.Str("token"));
        UltraDbRelayUser user;
        bool known = false;
        size_t open = 0;
        {
            std::lock_guard<std::mutex> lock(impl_->m);
            for (const UltraDbRelayUser& u : impl_->users)
                if (!known && SameSecret(u.tokenSha256, hash)) {
                    user = u;
                    known = true;
                }
            for (const auto& [unused, s] : impl_->sessions)
                if (known && s->user == user.name) ++open;
            if (known && open >= impl_->limits.sessionsPerUser)
                return Refused(id, UltraDbResultCode::Busy, "too many sessions open for " + user.name);
        }
        if (!known) return Refused(id, UltraDbResultCode::CredentialsUnavailable, "the token is not known here");
        {
            std::lock_guard<std::mutex> lock(session->m);
            session->user = user.name;
            session->access = user.access;
        }
        Json answer = Answer(id);
        answer.Set("user", Json::String(user.name));
        Json list = Json::Array();
        std::lock_guard<std::mutex> lock(impl_->m);
        for (const auto& [name, config] : impl_->offered) {
            const auto access = user.access.find(name);
            if (access == user.access.end() || access->second == UltraDbRelayAccess::Denied) continue;
            Json one = Json::Object();
            one.Set("name", Json::String(name));
            one.Set("engine", Json::String(EngineOf(config.driver)));
            one.Set("readOnly", Json::Truth(config.readOnly || access->second == UltraDbRelayAccess::Read));
            list.Push(std::move(one));
        }
        answer.Set("connections", std::move(list));
        return answer.Dump();
    }
    std::string user;
    {
        std::lock_guard<std::mutex> lock(session->m);
        user = session->user;
    }
    if (user.empty()) return Refused(id, UltraDbResultCode::CredentialsUnavailable, "say hello with a token first");
    if (!impl_->Allow(user)) return Refused(id, UltraDbResultCode::Busy, "too many requests: wait a moment");

    // ----- a connection of the session -----
    if (op == "open") {
        const std::string name = req.Str("connection");
        UltraDbRelayAccess access = UltraDbRelayAccess::Denied;
        {
            std::lock_guard<std::mutex> lock(session->m);
            const auto it = session->access.find(name);
            if (it != session->access.end()) access = it->second;
        }
        UltraDbConnectionConfig config;
        bool offered = false;
        {
            std::lock_guard<std::mutex> lock(impl_->m);
            const auto it = impl_->offered.find(name);
            if (it != impl_->offered.end()) {
                config = it->second;
                offered = true;
            }
        }
        // The same answer whether it is not there or not this user's.
        if (!offered || access == UltraDbRelayAccess::Denied)
            return Refused(id, UltraDbResultCode::ConnectionNotFound, "no connection " + name + " here");
        int64_t handle = 0;
        {
            std::lock_guard<std::mutex> lock(session->m);
            handle = session->next++;
        }
        config.name = "relay#" + std::to_string(session->id) + "#" + std::to_string(handle);
        if (access == UltraDbRelayAccess::Read) config.readOnly = true;
        UltraDbResult r = UltraDb_RegisterConnection(config);
        if (r) {
            std::lock_guard<std::mutex> lock(impl_->audit->m);
            impl_->audit->served[config.name] = Served{ user, session->peer, name };
        }
        if (r) r = UltraDb_OpenConnection(config.name);
        if (!r) {
            UltraDb_CloseConnection(config.name);
            std::lock_guard<std::mutex> lock(impl_->audit->m);
            impl_->audit->served.erase(config.name);
            return Refused(id, r);
        }
        {
            std::lock_guard<std::mutex> lock(session->m);
            session->connections[handle] = config.name;
        }
        Json answer = Answer(id);
        answer.Set("c", Json::Int(handle));
        answer.Set("engine", Json::String(EngineOf(config.driver)));
        return answer.Dump();
    }

    // The relay's name for the request's connection, or its cursor.
    auto connectionOf = [&session](int64_t handle, std::string& name) {
        std::lock_guard<std::mutex> lock(session->m);
        const auto it = session->connections.find(handle);
        if (it == session->connections.end()) return false;
        name = it->second;
        return true;
    };
    if (op == "fetch" || op == "uncursor") {
        UltraDbHandle cursor = UltraDbInvalidHandle;
        {
            std::lock_guard<std::mutex> lock(session->m);
            const auto it = session->cursors.find(req.Num("cursor"));
            if (it != session->cursors.end()) {
                cursor = it->second;
                if (op == "uncursor") session->cursors.erase(it);
            }
        }
        if (cursor == UltraDbInvalidHandle) return Refused(id, UltraDbResultCode::NotFound, "no such cursor");
        if (op == "uncursor") {
            UltraDb_CloseCursor(cursor);
            return Answer(id).Dump();
        }
        size_t max = static_cast<size_t>(std::max<int64_t>(1, req.Num("max", 500)));
        {
            std::lock_guard<std::mutex> lock(impl_->m);
            max = std::min(max, impl_->limits.rowsPerPage);
        }
        UltraDbResultSet rows;
        bool exhausted = false;
        const UltraDbResult r = UltraDb_FetchRows(cursor, max, rows, exhausted);
        if (!r) return Refused(id, r);
        Json answer = Answer(id);
        answer.Set("rows", ultradb_relay::FromResultSet(rows));
        answer.Set("exhausted", Json::Truth(exhausted));
        return answer.Dump();
    }

    const int64_t handle = req.Num("c");
    std::string name;
    if (!connectionOf(handle, name)) return Refused(id, UltraDbResultCode::ConnectionNotFound, "no such connection");

    if (op == "close") {
        {
            std::lock_guard<std::mutex> lock(session->m);
            session->connections.erase(handle);
        }
        UltraDb_CloseConnection(name);
        std::lock_guard<std::mutex> lock(impl_->audit->m);
        impl_->audit->served.erase(name);
        return Answer(id).Dump();
    }
    if (op == "cancel") {
        UltraDb_CancelQuery(name);
        return Answer(id).Dump();
    }
    if (op == "exec") {
        UltraDbResultSet rows;
        const UltraDbResult r = UltraDb_Query(name, req.Str("sql"), ultradb_relay::ToParams(req.Get("params")), rows);
        size_t cap = 0;
        {
            std::lock_guard<std::mutex> lock(impl_->m);
            cap = impl_->limits.rowsPerResult;
        }
        if (r && rows.Size() > cap)
            return Refused(id, UltraDbResultCode::InvalidArgument,
                           std::to_string(rows.Size()) + " rows are more than the relay sends at once (" +
                               std::to_string(cap) + "): read them with a cursor, or LIMIT them");
        Json answer = Answer(id);
        answer.Set("result", ultradb_relay::FromResult(r));
        if (r) answer.Set("rows", ultradb_relay::FromResultSet(rows));
        return answer.Dump();
    }
    if (op == "batch") {
        std::vector<UltraDbStatementResult> statements;
        const UltraDbResult r = UltraDb_ExecBatch(name, req.Str("sql"), statements);
        Json answer = Answer(id);
        answer.Set("result", ultradb_relay::FromResult(r));
        answer.Set("statements", ultradb_relay::FromStatements(statements));
        return answer.Dump();
    }
    if (op == "cursor") {
        UltraDbResult r;
        const UltraDbHandle cursor =
            UltraDb_OpenCursor(name, req.Str("sql"), ultradb_relay::ToParams(req.Get("params")), &r);
        if (cursor == UltraDbInvalidHandle) return Refused(id, r);
        int64_t mine = 0;
        {
            std::lock_guard<std::mutex> lock(session->m);
            mine = session->next++;
            session->cursors[mine] = cursor;
        }
        Json answer = Answer(id);
        answer.Set("cursor", Json::Int(mine));
        return answer.Dump();
    }
    if (op == "schemas") {
        std::vector<std::string> schemas;
        const UltraDbResult r = UltraDb_ListSchemas(name, schemas);
        if (!r) return Refused(id, r);
        Json answer = Answer(id);
        answer.Set("schemas", ultradb_relay::FromStrings(schemas));
        return answer.Dump();
    }
    if (op == "objects") {
        std::vector<UltraDbObjectSummary> objects;
        const UltraDbResult r = UltraDb_ListObjects(name, req.Str("schema"), objects);
        if (!r) return Refused(id, r);
        Json answer = Answer(id);
        answer.Set("objects", ultradb_relay::FromObjects(objects));
        return answer.Dump();
    }
    if (op == "describe") {
        UltraDbTableSchema table;
        const UltraDbResult r = UltraDb_DescribeTable(name, req.Str("schema"), req.Str("table"), table);
        if (!r) return Refused(id, r);
        Json answer = Answer(id);
        answer.Set("table", ultradb_relay::FromTable(table));
        return answer.Dump();
    }
    if (op == "server") {
        const std::string view = req.Str("view");
        UltraDbResultSet rows;
        UltraDbResult r;
        if (view == "sessions") r = UltraDb_ServerSessions(name, rows);
        else if (view == "settings") r = UltraDb_ServerSettings(name, req.Str("filter"), rows);
        else if (view == "sizes") r = UltraDb_ServerSizes(name, req.Str("schema"), rows);
        else if (view == "status") r = UltraDb_ServerStatus(name, rows);
        else return Refused(id, UltraDbResultCode::InvalidArgument, "no server view " + view);
        if (!r) return Refused(id, r);
        Json answer = Answer(id);
        answer.Set("rows", ultradb_relay::FromResultSet(rows));
        return answer.Dump();
    }
    if (op == "stopsql") {
        std::string sql;
        const UltraDbResult r = UltraDb_StopSessionSql(name, req.Num("target"), req.Flag("end"), sql);
        if (!r) return Refused(id, r);
        Json answer = Answer(id);
        answer.Set("sql", Json::String(sql));
        return answer.Dump();
    }
    if (op == "stop") {
        bool stopped = false;
        const UltraDbResult r = UltraDb_StopSession(name, req.Num("target"), req.Flag("end"), stopped);
        if (!r) return Refused(id, r);
        Json answer = Answer(id);
        answer.Set("stopped", Json::Truth(stopped));
        return answer.Dump();
    }
    return Refused(id, UltraDbResultCode::Unsupported, "no such request: " + op);
}

std::string UltraDb_RelayTokenHash(const std::string& token) {
    return Sha256Hex(token);
}
