// Tests/UltraDBAdmin/UltraDBAdminRelayTests.cpp
// UltraDBAdmin through its relay, end to end on this computer: the relay's
// configuration file read (and what it refuses), its WebSocket listener on a
// free port, a WebSocket client of the test's own as the transport, and the
// app's own engine on a relay connection - the store keeping the
// token where a password goes, the Browse pager, the completion catalog, the
// SQL console's statements - against a SQLite file the relay serves and, where
// the tests find one, PostgreSQL (its server status, a query stopped through
// the relay). The health check, the handshake's key, a path or an origin
// that is not the relay's.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminCompletion.h"
#include "DbAdminConnections.h"
#include "DbAdminTablePager.h"

#include "../../Apps/UltraDBAdmin/relay/RelayConfig.h"
#include "../../Apps/UltraDBAdmin/relay/RelayWebSocket.h"

#include <UltraDatabase/UltraDatabase.h>
#include <UltraNet/UltraNetHttp.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

using namespace UltraDBAdmin;
using namespace UltraDBAdminRelay;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) {
        std::printf("  ok    %s\n", what.c_str());
    } else {
        ++g_failures;
        std::printf("  FAIL  %s\n", what.c_str());
    }
}

std::string Env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

const char* kToken = "a-test-token-long-enough-for-the-relay";

// A WebSocket client of the test's own, plain ws:// on 127.0.0.1: the
// distribution's libcurl may lack WebSockets (Ubuntu 24.04's does), and the
// relay's framing is what is tested. Masked frames out, answers read on a
// thread and handed to whoever waits for their id.
class TestSocket : public IUltraDbRelayTransport {
public:
    static std::unique_ptr<IUltraDbRelayTransport> Connect(const std::string& url, UltraDbResult& error) {
        // ws://127.0.0.1:<port><path>
        const std::string prefix = "ws://127.0.0.1:";
        if (url.rfind(prefix, 0) != 0) {
            error = UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, "not a test relay: " + url);
            return nullptr;
        }
        const size_t slash = url.find('/', prefix.size());
        const int port = std::atoi(url.substr(prefix.size(), slash - prefix.size()).c_str());
        const std::string path = url.substr(slash);
        auto s = std::unique_ptr<TestSocket>(new TestSocket());
        s->fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(static_cast<uint16_t>(port));
        to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(s->fd_, reinterpret_cast<sockaddr*>(&to), sizeof to) != 0) {
            error = UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, "nothing listens at " + url);
            return nullptr;
        }
        const std::string hello = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
                                  "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                                  "Sec-WebSocket-Version: 13\r\n\r\n";
        ::send(s->fd_, hello.data(), hello.size(), MSG_NOSIGNAL);
        std::string head;
        char c = 0;
        while (head.find("\r\n\r\n") == std::string::npos && ::recv(s->fd_, &c, 1, 0) == 1) head += c;
        if (head.rfind("HTTP/1.1 101", 0) != 0) {
            error = UltraDbResult::Error(UltraDbResultCode::ConnectionFailed,
                                         "the relay said " + head.substr(0, head.find('\r')));
            return nullptr;
        }
        TestSocket* raw = s.get();
        s->reader_ = std::thread([raw] { raw->Read(); });
        return s;
    }
    ~TestSocket() override {
        ::shutdown(fd_, SHUT_RDWR);
        if (reader_.joinable()) reader_.join();
        ::close(fd_);
    }
    UltraDbResult Call(int64_t id, const std::string& request, std::string& response) override {
        {
            std::lock_guard<std::mutex> lock(write_);
            std::string frame(1, static_cast<char>(0x81));
            const size_t n = request.size();
            if (n < 126) frame += static_cast<char>(0x80 | n);
            else if (n <= 0xFFFF) { frame += static_cast<char>(0x80 | 126); frame += static_cast<char>(n >> 8); frame += static_cast<char>(n & 0xFF); }
            else { frame += static_cast<char>(0x80 | 127); for (int i = 7; i >= 0; --i) frame += static_cast<char>((static_cast<uint64_t>(n) >> (i * 8)) & 0xFF); }
            const char mask[4] = { 0x12, 0x34, 0x56, 0x78 };
            frame.append(mask, 4);
            for (size_t i = 0; i < n; ++i) frame += static_cast<char>(request[i] ^ mask[i % 4]);
            ::send(fd_, frame.data(), frame.size(), MSG_NOSIGNAL);
        }
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [&] { return answers_.count(id) || closed_; });
        if (!answers_.count(id)) return UltraDbResult::Error(UltraDbResultCode::ConnectionFailed, "the relay closed");
        response = std::move(answers_[id]);
        answers_.erase(id);
        return UltraDbResult::Ok();
    }

private:
    bool ReadAll(void* p, size_t n) {
        char* b = static_cast<char*>(p);
        while (n > 0) {
            const ssize_t got = ::recv(fd_, b, n, 0);
            if (got <= 0) return false;
            b += got;
            n -= static_cast<size_t>(got);
        }
        return true;
    }
    void Read() {
        for (;;) {
            uint8_t h[2];
            if (!ReadAll(h, 2)) break;
            uint64_t n = h[1] & 0x7F;
            if (n == 126) { uint8_t e[2]; if (!ReadAll(e, 2)) break; n = (e[0] << 8) | e[1]; }
            else if (n == 127) { uint8_t e[8]; if (!ReadAll(e, 8)) break; n = 0; for (uint8_t b : e) n = (n << 8) | b; }
            std::string payload(static_cast<size_t>(n), '\0');
            if (n && !ReadAll(payload.data(), payload.size())) break;
            if ((h[0] & 0x0F) != 0x1) continue;
            std::lock_guard<std::mutex> lock(m_);
            answers_[UltraDb_RelayAnswerId(payload)] = std::move(payload);
            cv_.notify_all();
        }
        std::lock_guard<std::mutex> lock(m_);
        closed_ = true;
        cv_.notify_all();
    }

    int fd_ = -1;
    std::thread reader_;
    std::mutex write_, m_;
    std::condition_variable cv_;
    std::map<int64_t, std::string> answers_;
    bool closed_ = false;
};

void TestConfig() {
    std::printf("The relay's configuration\n");
    RelayConfig config;
    std::string error;
    const std::string good =
        "listen = 127.0.0.1:0\n"
        "path = /relay   # the WebSocket\n"
        "[connection shop]\n"
        "driver = sqlite\n"
        "database = /tmp/shop.db\n"
        "read_only = true\n"
        "[user anna]\n"
        "token_sha256 = " + UltraDb_RelayTokenHash(kToken) + "\n"
        "shop = write\n"
        "[limits]\n"
        "rows_per_page = 50\n";
    Check(ParseRelayConfig(good, config, error), "a configuration read (" + error + ")");
    Check(config.connections.size() == 1 && config.connections[0].config.readOnly && config.users.size() == 1 &&
              config.users[0].access["shop"] == UltraDbRelayAccess::Write && config.limits.rowsPerPage == 50,
          "its connection, user and limits");
    Check(!ParseRelayConfig("[connection crm]\npassword = secret\n", config, error) &&
              error.find("password_secret") != std::string::npos,
          "a password in the file is refused, the vault named instead");
    Check(!ParseRelayConfig("listen = 0.0.0.0:8765\n", config, error) && error.find("TLS") != std::string::npos,
          "listening beyond loopback without TLS is refused");
    Check(ParseRelayConfig("listen = 0.0.0.0:8765\nplain_ok = true\n", config, error), "unless plain_ok says so");
    Check(!ParseRelayConfig("[user ben]\ntoken_sha256 = 00\n", config, error), "a token hash that is not one");
    Check(!ParseRelayConfig("[user ben]\ntoken_sha256 = " + std::string(64, 'a') + "\ncrm = read\n", config, error) &&
              error.find("no [connection]") != std::string::npos,
          "a grant of a connection not configured");
    Check(WebSocketAccept("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
          "the handshake's key, as RFC 6455 computes it");
}

// The relay on a free port of 127.0.0.1, its listener on a thread.
struct LiveRelay {
    UltraDbRelayServer server;
    std::unique_ptr<RelayListener> listener;
    std::thread thread;
    std::string url;

    bool Start(const ListenOptions& options) {
        listener = std::make_unique<RelayListener>(server, options);
        std::string error;
        if (!listener->Start(error)) {
            std::printf("  cannot listen: %s\n", error.c_str());
            return false;
        }
        url = "ws://127.0.0.1:" + std::to_string(listener->Port()) + options.path;
        thread = std::thread([this] { listener->Run(); });
        return true;
    }
    ~LiveRelay() {
        if (listener) listener->Stop();
        if (thread.joinable()) thread.join();
    }
};

void TestThroughTheRelay(const std::string& folder) {
    std::printf("\nThe app's engine through the relay\n");
    const std::string file = folder + "/relay-shop.db";
    {
        UltraDbConnectionConfig seed;
        seed.name = "relay-seed";
        seed.driver = "sqlite";
        seed.database = file;
        UltraDb_RegisterConnection(seed);
        UltraDb_Exec("relay-seed", "CREATE TABLE orders(id INTEGER PRIMARY KEY, customer TEXT, total REAL)");
        for (int i = 1; i <= 120; ++i)
            UltraDb_Exec("relay-seed", "INSERT INTO orders(customer, total) VALUES (?, ?)",
                         { UltraDbValue("customer " + std::to_string(i % 7)), UltraDbValue(i * 2.5) });
        UltraDb_CloseConnection("relay-seed");
    }
    LiveRelay relay;
    UltraDbConnectionConfig shop;
    shop.driver = "sqlite";
    shop.database = file;
    relay.server.AddConnection("shop", shop);
    UltraDbRelayUser anna;
    anna.name = "anna";
    anna.tokenSha256 = UltraDb_RelayTokenHash(kToken);
    anna.access["shop"] = UltraDbRelayAccess::Write;
    relay.server.AddUser(anna);
    ListenOptions options;
    options.port = 0;
    if (!relay.Start(options)) {
        Check(false, "the relay listens");
        return;
    }
    UltraDb_SetRelayConnector(&TestSocket::Connect);

    std::vector<UltraDbRelayConnection> offered;
    const UltraDbResult listed = UltraDb_RelayConnections(relay.url, kToken, offered);
    Check(listed.success && offered.size() == 1 && offered[0].engine == "sqlite",
          "the relay offers anna its one connection (" + listed.message + ")");
    Check(UltraDb_RelayConnections(relay.url, "not-a-token", offered).code == UltraDbResultCode::CredentialsUnavailable,
          "and nothing to a token it does not know");

    // The app's store: a relay connection, the token kept as its password.
    ConnectionStore store;
    store.OpenInMemory();
    ConnectionSpec spec;
    spec.name = "shop via relay";
    spec.driver = "relay";
    spec.host = relay.url;
    spec.database = "shop";
    spec.relayEngine = "sqlite";
    std::string error;
    const std::string id = store.Put(spec, kToken, false, error);
    Check(!id.empty() && store.Find(id)->hasPassword, "the token kept where a password goes");
    spec = *store.Find(id);
    Check(spec.IsRelay() && spec.IsSqlite() && !spec.IsLocalFile() && EngineOf(spec) == SqlEngine::Sqlite,
          "SQLite's dialect, though no file here");
    Check(Describe(spec) == "shop on " + relay.url, "described as the relay's");
    Check(store.Register(spec).success, "registered");
    const std::string c = RegisteredName(spec);

    // Browse's pager, a page at a time.
    UltraDbTableSchema orders;
    Check(UltraDb_DescribeTable(c, "main", "orders", orders).success && orders.primaryKey.size() == 1,
          "the table described through the relay");
    TablePager pager(c, orders, 50, SqlEngine::Sqlite);
    UltraDbResultSet page;
    const UltraDbResult loaded = pager.LoadPage(0, page);
    int64_t count = 0;
    Check(loaded.success && page.Size() == 50 && pager.CountRows(count).success && count == 120,
          "a Browse page through the relay, and the count (" + loaded.message + ")");
    // The completion catalog.
    CompletionCatalog catalog;
    Check(LoadCompletionCatalog(c, SqlEngine::Sqlite, "main", catalog).success &&
              catalog.schemas["main"]["orders"].size() == 3,
          "the completion catalog");
    // The SQL console: a script, statement by statement.
    std::vector<UltraDbStatementResult> statements;
    Check(UltraDb_ExecBatch(ConsoleName(spec), "UPDATE orders SET total = 0 WHERE id = 1; SELECT sum(total) AS s FROM orders",
                            statements).success &&
              statements.size() == 2 && statements[1].rows.Size() == 1,
          "the console's script");
    UltraDb_CloseConnection(c);
    UltraDb_CloseConnection(ConsoleName(spec));

    // Plain HTTP beside the WebSocket.
    UltraNetResponse health;
    const std::string base = "http://127.0.0.1:" + std::to_string(relay.listener->Port());
    Check(UltraNet_HttpGet(base + "/health", health).success && health.statusCode == 200, "GET /health answers");
    UltraNetResponse other;
    UltraNet_HttpGet(base + "/elsewhere", other);
    Check(other.statusCode == 404, "another path is not the relay's");
    UltraNetResponse plain;
    UltraNet_HttpGet(base + "/relay", plain);
    Check(plain.statusCode == 426, "the relay's path wants a WebSocket");
}

void TestOrigins() {
    std::printf("\nPages from elsewhere\n");
    LiveRelay relay;
    ListenOptions options;
    options.port = 0;
    options.origins = { "https://db.example.com" };
    if (!relay.Start(options)) return;
    UltraDb_SetRelayConnector(&TestSocket::Connect);
    std::vector<UltraDbRelayConnection> offered;
    // This client sends no Origin: as a page from elsewhere would not match either.
    Check(!UltraDb_RelayConnections(relay.url, kToken, offered).success,
          "a client without an allowed origin is turned away");
}

void TestPostgres() {
    std::printf("\nPostgreSQL through the relay\n");
    const std::string database = Env("ULTRAFIBU_TEST_PG_DB");
    if (database.empty()) {
        std::printf("  [SKIP] no PostgreSQL configured (ULTRAFIBU_TEST_PG_DB)\n");
        Check(Env("ULTRAFIBU_TEST_PG_REQUIRED").empty(), "a PostgreSQL server is configured");
        return;
    }
    LiveRelay relay;
    UltraDbConnectionConfig pg;
    pg.driver = "postgresql";
    pg.database = database;
    pg.host = Env("ULTRAFIBU_TEST_PG_HOST").empty() ? std::string("/var/run/postgresql") : Env("ULTRAFIBU_TEST_PG_HOST");
    pg.port = Env("ULTRAFIBU_TEST_PG_PORT").empty() ? 5432 : std::atoi(Env("ULTRAFIBU_TEST_PG_PORT").c_str());
    pg.user = Env("ULTRAFIBU_TEST_PG_USER");
    pg.tls = UltraDbTls::Disable;
    relay.server.AddConnection("pg", pg);
    UltraDbRelayUser anna;
    anna.name = "anna";
    anna.tokenSha256 = UltraDb_RelayTokenHash(kToken);
    anna.access["pg"] = UltraDbRelayAccess::Read;
    relay.server.AddUser(anna);
    ListenOptions options;
    options.port = 0;
    if (!relay.Start(options)) return;
    UltraDb_SetRelayConnector(&TestSocket::Connect);
    UltraDbConnectionConfig cfg;
    cfg.name = "relay-pg";
    cfg.driver = "relay";
    cfg.host = relay.url;
    cfg.database = "pg";
    cfg.options["relay_token"] = kToken;
    UltraDb_RegisterConnection(cfg);
    UltraDbResultSet rows;
    Check(UltraDb_ServerStatus("relay-pg", rows).success && rows.Size() >= 7, "the server's status through the relay");
    Check(!UltraDb_Exec("relay-pg", "CREATE TABLE relay_must_not(x int)").success, "a read-only grant writes nothing");
    std::atomic<bool> done{ false };
    UltraDbResult slept;
    const auto start = std::chrono::steady_clock::now();
    std::thread worker([&] {
        UltraDbResultSet out;
        slept = UltraDb_Query("relay-pg", "SELECT pg_sleep(20)", {}, out);
        done = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    UltraDb_CancelQuery("relay-pg");
    worker.join();
    Check(done && !slept.success && std::chrono::steady_clock::now() - start < std::chrono::seconds(10),
          "a query stopped through the relay, over the WebSocket, while it runs");
    UltraDb_CloseConnection("relay-pg");
}

} // namespace

int main() {
    std::printf("UltraDBAdmin relay tests\n\n");
    const std::string folder = (std::filesystem::temp_directory_path() / "ultradbadmin-relay-tests").string();
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    TestConfig();
    TestThroughTheRelay(folder);
    TestOrigins();
    TestPostgres();
    UltraDb_SetRelayConnector(nullptr);
    std::filesystem::remove_all(folder);
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
