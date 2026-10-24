// core/UltraDatabase/UltraDatabaseRelayClient.cpp
// The "relay" driver (UltraDatabaseRelay.h): every call of a connection sent
// to the relay through the application's transport, and its answer read
// back into the module's types. A connection is one "open" on the relay; its
// cursors and statements go through the same transport.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraDatabase/UltraDatabaseRelay.h"

#include "UltraDatabase/UltraDatabasePlugins.h"
#include "UltraDatabaseInternal.h"
#include "UltraDatabaseRelayJson.h"

#include <atomic>
#include <mutex>

using ultradb_relay::Json;

namespace {

std::mutex& ConnectorMutex() { static std::mutex m; return m; }
UltraDbRelayConnector& Connector() { static UltraDbRelayConnector c; return c; }

UltraDbResult Failed(UltraDbResultCode code, const std::string& message) {
    return UltraDbResult::Error(code, message, "relay");
}

// One transport, shared by a connection and its cursors.
class Link {
public:
    explicit Link(std::unique_ptr<IUltraDbRelayTransport> transport) : transport_(std::move(transport)) {}

    // Sends `request` (its "id" set here) and returns the answer; `error` is
    // set when the relay could not be reached or refused the request.
    bool Call(Json request, Json& answer, UltraDbResult& error) {
        const int64_t id = next_++;
        request.Set("id", Json::Int(id));
        std::string text;
        UltraDbResult sent = transport_->Call(id, request.Dump(), text);
        if (!sent) {
            error = sent;
            return false;
        }
        std::string parseError;
        if (!Json::Parse(text, answer, parseError) || answer.type != Json::Type::Object) {
            error = Failed(UltraDbResultCode::Internal, "the relay's answer cannot be read: " + parseError);
            return false;
        }
        if (!answer.Flag("ok")) {
            error = ultradb_relay::ToResult(answer.Get("result"));
            if (error.success) error = Failed(UltraDbResultCode::Internal, "the relay refused the request");
            return false;
        }
        error = UltraDbResult::Ok();
        return true;
    }

private:
    std::unique_ptr<IUltraDbRelayTransport> transport_;
    std::atomic<int64_t> next_{ 1 };
};

class RelayCursor : public IUltraDbCursor {
public:
    RelayCursor(std::shared_ptr<Link> link, int64_t cursor) : link_(std::move(link)), cursor_(cursor) {}
    ~RelayCursor() override {
        Json request = Json::Object();
        request.Set("op", Json::String("uncursor"));
        request.Set("cursor", Json::Int(cursor_));
        Json answer;
        UltraDbResult ignored;
        link_->Call(std::move(request), answer, ignored);
    }
    UltraDbResult Fetch(size_t maxRows, UltraDbResultSet& out, bool& exhausted) override {
        Json request = Json::Object();
        request.Set("op", Json::String("fetch"));
        request.Set("cursor", Json::Int(cursor_));
        request.Set("max", Json::Int(static_cast<int64_t>(maxRows)));
        Json answer;
        UltraDbResult error;
        out.Clear();
        if (!link_->Call(std::move(request), answer, error)) return error;
        ultradb_relay::ToResultSet(answer.Get("rows"), out);
        exhausted = answer.Flag("exhausted");
        return UltraDbResult::Ok();
    }

private:
    std::shared_ptr<Link> link_;
    int64_t cursor_;
};

class RelayConnection;

// A statement the relay compiles each time it runs: there is no handle to
// keep across the wire.
class RelayStatement : public IUltraDbStatement {
public:
    RelayStatement(RelayConnection& connection, std::string sql) : connection_(connection), sql_(std::move(sql)) {}
    UltraDbResult Execute(const UltraDbParams& params, UltraDbResultSet& out) override;

private:
    RelayConnection& connection_;
    std::string sql_;
};

class RelayConnection : public IUltraDbConnection {
public:
    RelayConnection(std::shared_ptr<Link> link, int64_t handle, std::string engine)
        : link_(std::move(link)), handle_(handle), engine_(std::move(engine)) {}
    ~RelayConnection() override {
        Json answer;
        UltraDbResult ignored;
        link_->Call(Request("close"), answer, ignored);
    }

    std::string DriverName() const override { return "relay"; }

    std::unique_ptr<IUltraDbStatement> Prepare(const std::string& sql, UltraDbResult& error) override {
        error = UltraDbResult::Ok();
        return std::make_unique<RelayStatement>(*this, sql);
    }

    UltraDbResult ExecuteDirect(const std::string& sql, const UltraDbParams& params, UltraDbResultSet& out) override {
        Json request = Request("exec");
        request.Set("sql", Json::String(sql));
        request.Set("params", ultradb_relay::FromParams(params));
        Json answer;
        UltraDbResult error;
        out.Clear();
        if (!link_->Call(std::move(request), answer, error)) return error;
        const UltraDbResult r = ultradb_relay::ToResult(answer.Get("result"));
        if (r) ultradb_relay::ToResultSet(answer.Get("rows"), out);
        return r;
    }

    // As the engine behind the relay starts and locks.
    std::string BeginTransactionSql() const override {
        return engine_ == "sqlite" ? "BEGIN IMMEDIATE" : engine_ == "mysql" ? "START TRANSACTION" : "BEGIN";
    }
    std::string RowLockSuffix() const override { return engine_ == "sqlite" ? std::string() : " FOR UPDATE"; }
    std::string QuoteIdentifier(const std::string& name) const override {
        if (engine_ != "mysql") return IUltraDbConnection::QuoteIdentifier(name);
        std::string out = "`";
        for (char c : name) {
            if (c == '`') out += '`';
            out += c;
        }
        return out + "`";
    }

    UltraDbResult ExecuteBatch(const std::string& sql, std::vector<UltraDbStatementResult>& out) override {
        Json request = Request("batch");
        request.Set("sql", Json::String(sql));
        Json answer;
        UltraDbResult error;
        out.clear();
        if (!link_->Call(std::move(request), answer, error)) return error;
        ultradb_relay::ToStatements(answer.Get("statements"), out);
        return ultradb_relay::ToResult(answer.Get("result"));
    }

    UltraDbResult Cancel() override {
        Json answer;
        UltraDbResult error;
        link_->Call(Request("cancel"), answer, error);
        return error;
    }

    std::unique_ptr<IUltraDbCursor> OpenCursor(const std::string& sql, const UltraDbParams& params,
                                               UltraDbResult& error) override {
        Json request = Request("cursor");
        request.Set("sql", Json::String(sql));
        request.Set("params", ultradb_relay::FromParams(params));
        Json answer;
        if (!link_->Call(std::move(request), answer, error)) return nullptr;
        return std::make_unique<RelayCursor>(link_, answer.Num("cursor"));
    }

    UltraDbResult ListSchemas(std::vector<std::string>& out) override {
        Json answer;
        UltraDbResult error;
        out.clear();
        if (!link_->Call(Request("schemas"), answer, error)) return error;
        out = ultradb_relay::ToStrings(answer.Get("schemas"));
        return UltraDbResult::Ok();
    }
    UltraDbResult ListObjects(const std::string& schema, std::vector<UltraDbObjectSummary>& out) override {
        Json request = Request("objects");
        request.Set("schema", Json::String(schema));
        Json answer;
        UltraDbResult error;
        out.clear();
        if (!link_->Call(std::move(request), answer, error)) return error;
        ultradb_relay::ToObjects(answer.Get("objects"), out);
        return UltraDbResult::Ok();
    }
    UltraDbResult DescribeTable(const std::string& schema, const std::string& table, UltraDbTableSchema& out) override {
        Json request = Request("describe");
        request.Set("schema", Json::String(schema));
        request.Set("table", Json::String(table));
        Json answer;
        UltraDbResult error;
        out = UltraDbTableSchema();
        if (!link_->Call(std::move(request), answer, error)) return error;
        ultradb_relay::ToTable(answer.Get("table"), out);
        return UltraDbResult::Ok();
    }

    UltraDbResult ServerSessions(UltraDbResultSet& out) override { return Server("sessions", "", "", out); }
    UltraDbResult ServerSettings(const std::string& filter, UltraDbResultSet& out) override {
        return Server("settings", filter, "", out);
    }
    UltraDbResult ServerSizes(const std::string& schema, UltraDbResultSet& out) override {
        return Server("sizes", "", schema, out);
    }
    UltraDbResult ServerStatus(UltraDbResultSet& out) override { return Server("status", "", "", out); }
    UltraDbResult StopSessionSql(int64_t id, bool endSession, std::string& sql) override {
        Json request = Request("stopsql");
        request.Set("target", Json::Int(id));
        request.Set("end", Json::Truth(endSession));
        Json answer;
        UltraDbResult error;
        sql.clear();
        if (!link_->Call(std::move(request), answer, error)) return error;
        sql = answer.Str("sql");
        return UltraDbResult::Ok();
    }
    UltraDbResult StopSession(int64_t id, bool endSession, bool& stopped) override {
        Json request = Request("stop");
        request.Set("target", Json::Int(id));
        request.Set("end", Json::Truth(endSession));
        Json answer;
        UltraDbResult error;
        stopped = false;
        if (!link_->Call(std::move(request), answer, error)) return error;
        stopped = answer.Flag("stopped");
        return UltraDbResult::Ok();
    }

private:
    Json Request(const char* op) const {
        Json request = Json::Object();
        request.Set("op", Json::String(op));
        request.Set("c", Json::Int(handle_));
        return request;
    }

    UltraDbResult Server(const char* view, const std::string& filter, const std::string& schema, UltraDbResultSet& out) {
        Json request = Request("server");
        request.Set("view", Json::String(view));
        request.Set("filter", Json::String(filter));
        request.Set("schema", Json::String(schema));
        Json answer;
        UltraDbResult error;
        out.Clear();
        if (!link_->Call(std::move(request), answer, error)) return error;
        ultradb_relay::ToResultSet(answer.Get("rows"), out);
        return UltraDbResult::Ok();
    }

    std::shared_ptr<Link> link_;
    int64_t handle_;
    std::string engine_;
};

UltraDbResult RelayStatement::Execute(const UltraDbParams& params, UltraDbResultSet& out) {
    return connection_.ExecuteDirect(sql_, params, out);
}

// The relay at `url`, greeted with `token`: the link and the greeting's answer.
std::shared_ptr<Link> Greet(const std::string& url, const std::string& token, Json& answer, UltraDbResult& error) {
    UltraDbRelayConnector connector;
    {
        std::lock_guard<std::mutex> lock(ConnectorMutex());
        connector = Connector();
    }
    if (!connector) {
        error = Failed(UltraDbResultCode::Unsupported,
                       "no relay transport in this application (UltraDb_SetRelayConnector)");
        return nullptr;
    }
    if (url.empty()) {
        error = Failed(UltraDbResultCode::InvalidArgument, "a relay connection names its relay in `host`");
        return nullptr;
    }
    std::unique_ptr<IUltraDbRelayTransport> transport = connector(url, error);
    if (!transport) {
        if (error.success) error = Failed(UltraDbResultCode::ConnectionFailed, "the relay at " + url + " is not reached");
        return nullptr;
    }
    auto link = std::make_shared<Link>(std::move(transport));
    Json hello = Json::Object();
    hello.Set("op", Json::String("hello"));
    hello.Set("token", Json::String(token));
    if (!link->Call(std::move(hello), answer, error)) return nullptr;
    return link;
}

class RelayDriver : public IUltraDbDriverPlugin {
public:
    std::string GetName() const override { return "UltraDatabase relay"; }
    std::vector<std::string> GetDriverIds() const override { return { "relay" }; }

    std::unique_ptr<IUltraDbConnection> Open(const UltraDbConnectionConfig& config, UltraDbResult& error) override {
        const auto token = config.options.find("relay_token");
        Json answer;
        std::shared_ptr<Link> link =
            Greet(config.host, token == config.options.end() ? std::string() : token->second, answer, error);
        if (!link) return nullptr;
        Json open = Json::Object();
        open.Set("op", Json::String("open"));
        open.Set("connection", Json::String(config.database));
        if (!link->Call(std::move(open), answer, error)) return nullptr;
        return std::make_unique<RelayConnection>(link, answer.Num("c"), answer.Str("engine"));
    }
};

} // namespace

namespace ultradb_internal {

IUltraDbDriverPlugin* BuiltinRelayDriver() {
    static RelayDriver driver;
    static std::once_flag once;
    std::call_once(once, [] { UltraDatabase_RegisterDriver(&driver); });
    return &driver;
}

} // namespace ultradb_internal

int64_t UltraDb_RelayAnswerId(const std::string& answer) {
    static const std::string prefix = "{\"id\":";
    if (answer.compare(0, prefix.size(), prefix) != 0) return -1;
    int64_t id = 0;
    size_t i = prefix.size();
    const bool negative = i < answer.size() && answer[i] == '-';
    if (negative) ++i;
    const size_t digits = i;
    while (i < answer.size() && answer[i] >= '0' && answer[i] <= '9') id = id * 10 + (answer[i++] - '0');
    return i == digits ? -1 : negative ? -id : id;
}

void UltraDb_SetRelayConnector(UltraDbRelayConnector connector) {
    std::lock_guard<std::mutex> lock(ConnectorMutex());
    Connector() = std::move(connector);
}

UltraDbResult UltraDb_RelayConnections(const std::string& url, const std::string& token,
                                       std::vector<UltraDbRelayConnection>& out) {
    out.clear();
    Json answer;
    UltraDbResult error;
    if (!Greet(url, token, answer, error)) return error;
    if (const Json* list = answer.Get("connections"))
        for (const Json& one : list->items)
            out.push_back(UltraDbRelayConnection{ one.Str("name"), one.Str("engine"), one.Flag("readOnly") });
    return UltraDbResult::Ok();
}
