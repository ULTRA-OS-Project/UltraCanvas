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
// Where the relay is built with its OpenID Connect sign-in
// (ULTRADBADMIN_RELAY_SIGNIN): the ID token's checks one by one (signature,
// RSA and P-256, issuer, audience, expiry, nonce, the algorithm), who a
// token is, where the browser may be sent back to, and the whole sign-in
// against a provider of the test's own on 127.0.0.1 - its discovery, the
// redirects, PKCE, the ticket in the fragment or the loopback query, the
// ticket then saying hello to the relay; a replayed callback, a refused
// sign-in, a stranger and a return elsewhere turned away.
// Version: 0.3.0 - the sign-in; statement_timeout read
// Version: 0.2.0 - an SSH option refused, the audit log's address
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminCompletion.h"
#include "DbAdminConnections.h"
#include "DbAdminTablePager.h"

#include "../../Apps/UltraDBAdmin/relay/RelayConfig.h"
#include "../../Apps/UltraDBAdmin/relay/RelayWebSocket.h"
#if defined(ULTRADBADMIN_RELAY_SIGNIN)
#include "../../Apps/UltraDBAdmin/relay/RelaySignIn.h"

#include <UltraNet/UltraNetOAuth2.h>

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#endif

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
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <cctype>

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
    Check(ParseRelayConfig("[limits]\nstatement_timeout = 45\n", config, error) &&
              config.limits.statementTimeoutSeconds == 45,
          "the statement time-out read");
    Check(!ParseRelayConfig("[connection crm]\nssh_host = bastion\n", config, error) &&
              error.find("SSH") != std::string::npos,
          "an SSH tunnel from the relay is refused, not ignored");
    Check(WebSocketAccept("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
          "the handshake's key, as RFC 6455 computes it");
    // Who the audit log names.
    Check(ClientAddress("203.0.113.7", "") == "203.0.113.7", "a visitor by the address it came from");
    Check(ClientAddress("127.0.0.1", "198.51.100.4, 203.0.113.7") == "203.0.113.7",
          "behind a proxy here, the address the proxy added");
    Check(ClientAddress("203.0.113.7", "10.0.0.1") == "203.0.113.7", "a header from elsewhere ignored");
    Check(ClientAddress("127.0.0.1", "x'; DROP") == "127.0.0.1", "a header that is no address ignored");
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


#if defined(ULTRADBADMIN_RELAY_SIGNIN)
// ===== the sign-in =====

// A key of the test provider's, and the token it signs.
struct SigningKey {
    EVP_PKEY* key = nullptr;
    std::string kid, alg;
    ~SigningKey() { EVP_PKEY_free(key); }

    static std::string Bn(EVP_PKEY* k, const char* name) {
        BIGNUM* bn = nullptr;
        EVP_PKEY_get_bn_param(k, name, &bn);
        std::string out(static_cast<size_t>(BN_num_bytes(bn)), '\0');
        BN_bn2bin(bn, reinterpret_cast<unsigned char*>(out.data()));
        BN_free(bn);
        return out;
    }
    std::string Jwk() const {
        if (alg == "RS256")
            return "{\"kty\":\"RSA\",\"use\":\"sig\",\"kid\":\"" + kid + "\",\"n\":\"" +
                   Base64UrlEncode(Bn(key, OSSL_PKEY_PARAM_RSA_N)) + "\",\"e\":\"" +
                   Base64UrlEncode(Bn(key, OSSL_PKEY_PARAM_RSA_E)) + "\"}";
        // The public key's DER ends with the uncompressed point (04 || x || y).
        unsigned char* der = nullptr;
        const int size = i2d_PUBKEY(key, &der);
        const std::string point = size >= 65 ? std::string(reinterpret_cast<char*>(der) + size - 65, 65) : std::string(65, '\0');
        OPENSSL_free(der);
        return "{\"kty\":\"EC\",\"crv\":\"P-256\",\"kid\":\"" + kid + "\",\"x\":\"" +
               Base64UrlEncode(point.substr(1, 32)) + "\",\"y\":\"" + Base64UrlEncode(point.substr(33, 32)) + "\"}";
    }
    std::string Sign(const std::string& claims, const std::string& headerAlg = "") const {
        const std::string header = "{\"alg\":\"" + (headerAlg.empty() ? alg : headerAlg) + "\",\"kid\":\"" + kid + "\"}";
        const std::string data = Base64UrlEncode(header) + "." + Base64UrlEncode(claims);
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        size_t size = 0;
        EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, key);
        EVP_DigestSign(ctx, nullptr, &size, reinterpret_cast<const unsigned char*>(data.data()), data.size());
        std::string sig(size, '\0');
        EVP_DigestSign(ctx, reinterpret_cast<unsigned char*>(sig.data()), &size,
                       reinterpret_cast<const unsigned char*>(data.data()), data.size());
        sig.resize(size);
        EVP_MD_CTX_free(ctx);
        if (alg == "ES256") {
            // DER to JWS's r || s.
            const unsigned char* p = reinterpret_cast<const unsigned char*>(sig.data());
            ECDSA_SIG* ec = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(sig.size()));
            unsigned char raw[64];
            BN_bn2binpad(ECDSA_SIG_get0_r(ec), raw, 32);
            BN_bn2binpad(ECDSA_SIG_get0_s(ec), raw + 32, 32);
            ECDSA_SIG_free(ec);
            sig.assign(reinterpret_cast<char*>(raw), 64);
        }
        return data + "." + Base64UrlEncode(sig);
    }
};

std::unique_ptr<SigningKey> RsaKey(const std::string& kid, int bits = 2048) {
    auto k = std::make_unique<SigningKey>();
    k->key = EVP_RSA_gen(static_cast<unsigned int>(bits));
    k->kid = kid;
    k->alg = "RS256";
    return k;
}

std::unique_ptr<SigningKey> EcKey(const std::string& kid) {
    auto k = std::make_unique<SigningKey>();
    k->key = EVP_EC_gen("P-256");
    k->kid = kid;
    k->alg = "ES256";
    return k;
}

std::string Claims(const std::string& iss, const std::string& aud, int64_t exp, const std::string& nonce,
                   const std::string& extra = ",\"email\":\"Anna@Example.com\",\"email_verified\":true") {
    return "{\"iss\":\"" + iss + "\",\"sub\":\"sub-anna\",\"aud\":" + aud + ",\"exp\":" + std::to_string(exp) +
           ",\"iat\":" + std::to_string(exp - 300) + ",\"nonce\":\"" + nonce + "\"" + extra + "}";
}

void TestIdTokens() {
    std::printf("\nThe sign-in's ID token\n");
    std::string bytes;
    bool roundTrips = true;
    for (const std::string& raw : { std::string(), std::string("a"), std::string("ab"), std::string("abc"),
                                    std::string("\xff\xfe\x00\x01", 4) })
        roundTrips = roundTrips && Base64UrlDecode(Base64UrlEncode(raw), bytes) && bytes == raw;
    Check(roundTrips, "base64url there and back");

    auto rsa = RsaKey("r1");
    auto ec = EcKey("e1");
    PublicKeys keys;
    std::string error;
    Check(keys.Parse("{\"keys\":[" + rsa->Jwk() + "," + ec->Jwk() + "]}", error) && keys.Size() == 2,
          "the provider's RSA and P-256 keys read (" + error + ")");
    auto small = RsaKey("small", 1024);
    PublicKeys weak;
    Check(!weak.Parse("{\"keys\":[" + small->Jwk() + "]}", error), "a 1024-bit RSA key is not taken");

    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    TokenExpectations expect{ "https://sso.example.com", "ultradbadmin", "n-123", now };
    Identity who;
    const std::string good = Claims(expect.issuer, "\"ultradbadmin\"", now + 600, "n-123");
    Check(VerifyIdToken(rsa->Sign(good), keys, expect, who, error) && who.subject == "sub-anna" &&
              who.email == "anna@example.com" && who.emailVerified,
          "an RS256 token: who it is (" + error + ")");
    Check(VerifyIdToken(ec->Sign(good), keys, expect, who, error), "an ES256 token (" + error + ")");
    // Each check on its own.
    std::string tampered = rsa->Sign(good);
    tampered[tampered.find('.') + 5] ^= 1;
    Check(!VerifyIdToken(tampered, keys, expect, who, error), "a changed token: the signature fails");
    Check(!VerifyIdToken(rsa->Sign(Claims("https://evil.example", "\"ultradbadmin\"", now + 600, "n-123")), keys, expect,
                         who, error) && error.find("evil") != std::string::npos,
          "another issuer");
    Check(!VerifyIdToken(rsa->Sign(Claims(expect.issuer, "\"someone-else\"", now + 600, "n-123")), keys, expect, who, error),
          "another application's token");
    Check(VerifyIdToken(rsa->Sign(Claims(expect.issuer, "[\"x\",\"ultradbadmin\"]", now + 600, "n-123",
                                         ",\"azp\":\"ultradbadmin\"")),
                        keys, expect, who, error),
          "several audiences, this client the one authorized");
    Check(!VerifyIdToken(rsa->Sign(Claims(expect.issuer, "[\"x\",\"ultradbadmin\"]", now + 600, "n-123", ",\"azp\":\"x\"")),
                         keys, expect, who, error),
          "several audiences, authorized for another");
    Check(!VerifyIdToken(rsa->Sign(Claims(expect.issuer, "\"ultradbadmin\"", now - 600, "n-123")), keys, expect, who, error) &&
              error.find("expired") != std::string::npos,
          "an expired token");
    Check(!VerifyIdToken(rsa->Sign(Claims(expect.issuer, "\"ultradbadmin\"", now + 600, "other")), keys, expect, who, error) &&
              error.find("nonce") != std::string::npos,
          "a token from another sign-in (nonce)");
    const std::string unsignedToken = Base64UrlEncode("{\"alg\":\"none\"}") + "." + Base64UrlEncode(good) + ".";
    Check(!VerifyIdToken(unsignedToken, keys, expect, who, error), "alg none refused");
    Check(!VerifyIdToken(rsa->Sign(good, "HS256"), keys, expect, who, error), "HS256 refused");
    auto stranger = RsaKey("r1");      // the same kid, another key
    Check(!VerifyIdToken(stranger->Sign(good), keys, expect, who, error), "a key the provider does not publish");
    Check(IdTokenKeyId(rsa->Sign(good)) == "r1", "the key a token names");

    // Who it is.
    std::map<std::string, UserIdentity> identities;
    identities["anna"].email = "anna@example.com";
    identities["ben"].subject = "sub-ben";
    Identity anna{ "sub-x", "anna@example.com", true };
    Check(UserFor(anna, identities) == "anna", "a verified email");
    anna.emailVerified = false;
    Check(UserFor(anna, identities).empty(), "an email the provider did not verify: nobody");
    Check(UserFor(Identity{ "sub-ben", "", false }, identities) == "ben", "a subject");

    // Where the browser may go back to.
    const std::vector<std::string> origins = { "https://admin.example.com" };
    const std::string pub = "https://db.example.com/relay";
    Check(ReturnAllowed("https://db.example.com/UltraDBAdmin.html?relay=x", pub, origins), "the relay's own origin");
    Check(ReturnAllowed("https://ADMIN.example.com/app", pub, origins), "a configured origin");
    Check(ReturnAllowed("http://127.0.0.1:54321/callback", pub, origins), "this computer (a desktop app)");
    Check(!ReturnAllowed("https://evil.example/", pub, origins), "another site");
    Check(!ReturnAllowed("https://db.example.com.evil.example/", pub, origins), "a look-alike");
    Check(!ReturnAllowed("javascript:alert(1)", pub, origins), "not a page");
    Check(!ReturnAllowed("https://db.example.com/\r\nSet-Cookie: x", pub, origins), "a header smuggled in");

    // The configuration.
    RelayConfig config;
    const std::string signin = "public_url = https://db.example.com/relay/\n"
                               "[signin]\nlabel = Example SSO\nissuer = https://sso.example.com\nclient_id = uda\n"
                               "client_secret_secret = sso\nticket_hours = 2\n"
                               "[connection shop]\ndriver = sqlite\ndatabase = /tmp/x.db\n"
                               "[user anna]\nemail = Anna@Example.com\nshop = read\n";
    Check(ParseRelayConfig(signin, config, error) && config.signIn.enabled && config.signIn.label == "Example SSO" &&
              config.publicUrl == "https://db.example.com/relay" && config.identities["anna"].email == "anna@example.com" &&
              config.users.size() == 1 && config.users[0].tokenSha256.empty(),
          "[signin] read, and a user by email alone (" + error + ")");
    Check(!ParseRelayConfig("[signin]\nissuer = https://sso.example.com\nclient_id = uda\n", config, error) &&
              error.find("public_url") != std::string::npos,
          "a sign-in without public_url");
    Check(!ParseRelayConfig("[signin]\nissuer = http://sso.example.com\n", config, error), "an issuer over plain http");
    Check(!ParseRelayConfig("[signin]\nclient_secret = s3cret\n", config, error), "a client secret in the file");
    Check(!ParseRelayConfig("[user anna]\nemail = anna@example.com\n", config, error) &&
              error.find("[signin]") != std::string::npos,
          "a user by email without a sign-in");
}

// An OpenID provider of the test's own on 127.0.0.1: discovery, keys, and a
// token endpoint that checks the code and its PKCE verifier.
class TestProvider {
public:
    std::string base;
    std::unique_ptr<SigningKey> key = RsaKey("k1");
    std::string clientId = "uda";
    std::string email = "anna@example.com";
    std::atomic<int> tokenRequests{ 0 };

    bool Start() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in at{};
        at.sin_family = AF_INET;
        at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&at), sizeof at) != 0 || ::listen(fd_, 8) != 0) return false;
        socklen_t size = sizeof at;
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&at), &size);
        base = "http://127.0.0.1:" + std::to_string(ntohs(at.sin_port));
        thread_ = std::thread([this] { Run(); });
        return true;
    }
    ~TestProvider() {
        stopping_ = true;
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        if (thread_.joinable()) thread_.join();
    }
    // What the provider's sign-in page does once the user signed in: a code
    // for this authorization request.
    std::string Grant(const std::map<std::string, std::string>& authorize) {
        std::lock_guard<std::mutex> lock(m_);
        const std::string code = "code-" + std::to_string(++codes_);
        grants_[code] = authorize;
        return code;
    }

private:
    void Run() {
        while (!stopping_) {
            const int c = ::accept(fd_, nullptr, nullptr);
            if (c < 0) break;
            std::string request;
            char buffer[4096];
            size_t need = std::string::npos;
            for (;;) {
                const ssize_t n = ::recv(c, buffer, sizeof buffer, 0);
                if (n <= 0) break;
                request.append(buffer, static_cast<size_t>(n));
                const size_t end = request.find("\r\n\r\n");
                if (end != std::string::npos && need == std::string::npos) {
                    std::string lower = request.substr(0, end);
                    for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    const size_t cl = lower.find("content-length:");
                    need = end + 4 + (cl == std::string::npos ? 0 : std::strtoul(lower.c_str() + cl + 15, nullptr, 10));
                }
                if (need != std::string::npos && request.size() >= need) break;
            }
            const std::string answer = Answer(request);
            ::send(c, answer.data(), answer.size(), MSG_NOSIGNAL);
            ::close(c);
        }
    }
    static std::string Http(int status, const std::string& body) {
        return "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Type: application/json\r\nContent-Length: " +
               std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    }
    std::string Answer(const std::string& request) {
        const std::string line = request.substr(0, request.find("\r\n"));
        if (line.rfind("GET /.well-known/openid-configuration", 0) == 0)
            return Http(200, "{\"issuer\":\"" + base + "\",\"authorization_endpoint\":\"" + base +
                                 "/authorize\",\"token_endpoint\":\"" + base + "/token\",\"jwks_uri\":\"" + base + "/jwks\"}");
        if (line.rfind("GET /jwks", 0) == 0) return Http(200, "{\"keys\":[" + key->Jwk() + "]}");
        if (line.rfind("POST /token", 0) == 0) {
            ++tokenRequests;
            const auto form = ParseQuery(request.substr(request.find("\r\n\r\n") + 4));
            std::map<std::string, std::string> grant;
            {
                std::lock_guard<std::mutex> lock(m_);
                const auto it = grants_.find(form.count("code") ? form.at("code") : "");
                if (it == grants_.end()) return Http(400, "{\"error\":\"invalid_grant\"}");
                grant = it->second;
                grants_.erase(it);
            }
            // PKCE: the verifier must be the one behind the challenge.
            if (!form.count("code_verifier") ||
                UltraNet_OAuth2ChallengeFromVerifier(form.at("code_verifier")) != grant["code_challenge"] ||
                form.count("redirect_uri") == 0 || form.at("redirect_uri") != grant["redirect_uri"])
                return Http(400, "{\"error\":\"invalid_grant\",\"error_description\":\"PKCE or redirect_uri\"}");
            const int64_t now = static_cast<int64_t>(std::time(nullptr));
            const std::string idToken = key->Sign(Claims(base, "\"" + clientId + "\"", now + 600, grant["nonce"],
                                                         ",\"email\":\"" + email + "\",\"email_verified\":true"));
            return Http(200, "{\"access_token\":\"at\",\"token_type\":\"Bearer\",\"id_token\":\"" + idToken + "\"}");
        }
        return Http(404, "{}");
    }

    int fd_ = -1;
    std::thread thread_;
    std::atomic<bool> stopping_{ false };
    std::mutex m_;
    int codes_ = 0;
    std::map<std::string, std::map<std::string, std::string>> grants_;
};

std::map<std::string, std::string> QueryOf(const std::string& url) {
    const size_t q = url.find('?');
    if (q == std::string::npos) return {};
    const size_t hash = url.find('#', q);
    return ParseQuery(url.substr(q + 1, hash == std::string::npos ? std::string::npos : hash - q - 1));
}

UltraNetResponse GetNoRedirect(const std::string& url) {
    UltraNetHttpOptions options = UltraNetHttpOptions::Default();
    options.followRedirects = false;
    UltraNetResponse response;
    UltraNet_HttpGet(url, response, options);
    return response;
}

void TestSignIn(const std::string& folder) {
    std::printf("\nSigning in with an OpenID provider\n");
    TestProvider provider;
    if (!provider.Start()) {
        Check(false, "the test provider listens");
        return;
    }
    LiveRelay relay;
    ListenOptions options;
    options.port = 0;
    // public_url is the relay's own address here, known once it listens: the
    // listener starts first and the sign-in is put in front of it after.
    std::shared_ptr<RelaySignIn> signIn;
    std::vector<std::string> log;
    std::mutex logMutex;
    options.http = [&signIn](const HttpRequest& request, HttpReply& reply) { return signIn && signIn->Handle(request, reply); };
    if (!relay.Start(options)) {
        Check(false, "the relay listens");
        return;
    }
    const std::string publicUrl = "http://127.0.0.1:" + std::to_string(relay.listener->Port()) + "/relay";
    const std::string file = folder + "/signin.db";
    RelayConfig config;
    std::string error;
    const std::string text = "public_url = " + publicUrl + "\n[signin]\nlabel = Test SSO\nissuer = " + provider.base +
                             "\nclient_id = uda\n[connection shop]\ndriver = sqlite\ndatabase = " + file +
                             "\n[user anna]\nemail = anna@example.com\nshop = write\n";
    if (!ParseRelayConfig(text, config, error)) {
        Check(false, "the sign-in's configuration (" + error + ")");
        return;
    }
    signIn = std::make_shared<RelaySignIn>(config, std::string(), [&](const std::string& peer, const std::string& line, bool ok) {
        std::lock_guard<std::mutex> lock(logMutex);
        log.push_back(peer + (ok ? " ok " : " failed ") + line);
    });
    Check(signIn->Start(error) && signIn->AuthorizationEndpoint() == provider.base + "/authorize",
          "the provider discovered (" + error + ")");
    for (const OfferedConnection& c : config.connections) relay.server.AddConnection(c.name, c.config);
    for (const UltraDbRelayUser& u : config.users) relay.server.AddUser(u);
    RelaySignIn* tickets = signIn.get();
    relay.server.SetSignIn(config.signIn.label, [&config, tickets](const std::string& token, UltraDbRelayUser& user) {
        std::string name;
        if (!tickets->Ticket(token, name)) return false;
        for (const UltraDbRelayUser& u : config.users)
            if (u.name == name) user = u;
        return !user.name.empty();
    });
    UltraDb_SetRelayConnector(&TestSocket::Connect);
    std::string label;
    Check(UltraDb_RelaySignInOffer(relay.url, label).success && label == "Test SSO", "the relay offers its sign-in");

    // The page: to the relay, to the provider, back to the relay, back to the page.
    UltraNetResponse toProvider = GetNoRedirect(publicUrl + "/signin?return=http%3A%2F%2F127.0.0.1%3A" +
                                                std::to_string(relay.listener->Port()) + "%2FUltraDBAdmin.html");
    const std::string authorize = toProvider.headers.Get("Location");
    auto params = QueryOf(authorize);
    Check(toProvider.statusCode == 302 && authorize.rfind(provider.base + "/authorize?", 0) == 0 &&
              params["client_id"] == "uda" && params["code_challenge_method"] == "S256" && !params["nonce"].empty() &&
              params["redirect_uri"] == publicUrl + "/callback" && params["scope"].find("openid") != std::string::npos,
          "sent to the provider with PKCE, a nonce and the relay's callback");
    const std::string code = provider.Grant(params);
    UltraNetResponse back = GetNoRedirect(publicUrl + "/callback?code=" + code + "&state=" + params["state"]);
    const std::string location = back.headers.Get("Location");
    const size_t at = location.find("#relay_ticket=");
    const std::string ticket = at == std::string::npos ? std::string() : location.substr(at + 14);
    Check(back.statusCode == 302 && location.find("/UltraDBAdmin.html#relay_ticket=") != std::string::npos &&
              ticket.size() >= 40 && provider.tokenRequests == 1,
          "back to the page with a ticket in the fragment (" + location.substr(0, 80) + ")");
    std::vector<UltraDbRelayConnection> offered;
    Check(UltraDb_RelayConnections(relay.url, ticket, offered).success && offered.size() == 1 && offered[0].name == "shop",
          "the ticket opens what anna may open");
    UltraDbConnectionConfig cfg;
    cfg.name = "signed-in-shop";
    cfg.driver = "relay";
    cfg.host = relay.url;
    cfg.database = "shop";
    cfg.options["relay_token"] = ticket;
    UltraDb_RegisterConnection(cfg);
    Check(UltraDb_Exec("signed-in-shop", "CREATE TABLE t(x INTEGER)").success, "a statement as anna, signed in");
    UltraDb_CloseConnection("signed-in-shop");

    // The callback again: one use only.
    UltraNetResponse replay = GetNoRedirect(publicUrl + "/callback?code=" + code + "&state=" + params["state"]);
    Check(replay.statusCode == 400 && provider.tokenRequests == 1, "a callback replayed is turned away");

    // A desktop app: its loopback listener and its own state.
    toProvider = GetNoRedirect(publicUrl + "/signin?return=http%3A%2F%2F127.0.0.1%3A49999%2Fdone&state=desk-42");
    params = QueryOf(toProvider.headers.Get("Location"));
    back = GetNoRedirect(publicUrl + "/callback?code=" + provider.Grant(params) + "&state=" + params["state"]);
    const auto desk = QueryOf(back.headers.Get("Location"));
    Check(back.headers.Get("Location").rfind("http://127.0.0.1:49999/done?code=", 0) == 0 && desk.count("code") &&
              desk.count("state") && desk.at("state") == "desk-42" &&
              UltraDb_RelayConnections(relay.url, desk.at("code"), offered).success,
          "a desktop app gets its ticket on its loopback address, with its state");

    // Someone the relay does not know.
    provider.email = "mallory@example.com";
    toProvider = GetNoRedirect(publicUrl + "/signin?return=http%3A%2F%2F127.0.0.1%3A49999%2Fdone");
    params = QueryOf(toProvider.headers.Get("Location"));
    back = GetNoRedirect(publicUrl + "/callback?code=" + provider.Grant(params) + "&state=" + params["state"]);
    Check(back.statusCode == 403 && back.headers.Get("Location").empty() &&
              back.GetBodyAsString().find("mallory@example.com has no access") != std::string::npos,
          "a stranger gets no ticket, and is told why");
    // The provider said no.
    toProvider = GetNoRedirect(publicUrl + "/signin?return=http%3A%2F%2F127.0.0.1%3A49999%2Fdone");
    params = QueryOf(toProvider.headers.Get("Location"));
    back = GetNoRedirect(publicUrl + "/callback?error=access_denied&state=" + params["state"]);
    Check(back.statusCode == 403, "a sign-in the provider refused");
    // A return to another site.
    toProvider = GetNoRedirect(publicUrl + "/signin?return=https%3A%2F%2Fevil.example%2F");
    Check(toProvider.statusCode == 400 && toProvider.headers.Get("Location").empty(), "a return to another site is refused");
    {
        std::lock_guard<std::mutex> lock(logMutex);
        bool signedIn = false, refused = false;
        for (const std::string& line : log) {
            signedIn = signedIn || line.find(" ok -- signed in with Test SSO as anna@example.com: user anna") != std::string::npos;
            refused = refused || line.find(" failed -- sign-in with Test SSO refused: mallory@example.com") != std::string::npos;
        }
        Check(signedIn && refused, "the audit log has who signed in, and who was refused");
    }
    Check(!signIn->Ticket("not-a-ticket", label), "a ticket not handed out is nobody's");
    UltraDb_SetRelayConnector(nullptr);
}
#endif

int main() {
    std::printf("UltraDBAdmin relay tests\n\n");
    const std::string folder = (std::filesystem::temp_directory_path() / "ultradbadmin-relay-tests").string();
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    TestConfig();
    TestThroughTheRelay(folder);
    TestOrigins();
    TestPostgres();
#if defined(ULTRADBADMIN_RELAY_SIGNIN)
    TestIdTokens();
    TestSignIn(folder);
#endif
    UltraDb_SetRelayConnector(nullptr);
    std::filesystem::remove_all(folder);
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
