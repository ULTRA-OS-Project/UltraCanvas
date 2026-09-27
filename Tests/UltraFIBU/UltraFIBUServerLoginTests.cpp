// Tests/UltraFIBU/UltraFIBUServerLoginTests.cpp
// Server mode's password comes out of UltraVault - proved without a server.
//
// Store::OpenServer takes only a "vault:<key>" reference, never a password, and
// the PostgreSQL driver is what turns the reference into the password libpq
// sends. Until 0.9.65 that resolution was compiled out in every build (the
// define that enabled it was set nowhere, and the function it called did not
// exist), so every password login failed with "UltraVault is not built in" -
// and nothing noticed, because the multi-user test logs in without a password.
//
// This suite needs no PostgreSQL server. It opens a memory-backed vault and
// talks to a fake server on the loopback interface that speaks just enough of
// the wire protocol to ask for a cleartext password and write down what
// arrives:
//
//  - through the driver, the password stored in the vault is exactly what
//    reaches the wire - quotes and backslashes included, which the connection
//    string has to escape;
//  - through OpenServer, a stored key gets as far as the server (which then
//    refuses, since OpenServer insists on verified TLS and the fake has none),
//    while a key that is not in the vault, or a vault that is not open, is
//    refused before any connection is made.
//
// Built only where libpq is present, like UltraFIBUServerTests. Without the
// PostgreSQL driver it reports SKIP, and with ULTRAFIBU_TEST_PG_REQUIRED=1 the
// skip is a failure.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraFIBUStore.h"

#include <UltraDatabase/UltraDatabaseCore.h>
#include <UltraDatabase/UltraDatabaseConnection.h>
#include <UltraVault/UltraVault.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace UltraFIBU;

namespace {

int fehler = 0;

void Check(bool ok, const std::string& was) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FEHLER", was.c_str());
    if (!ok) ++fehler;
}

bool Contains(const std::string& text, const std::string& teil) {
    return text.find(teil) != std::string::npos;
}

// ---------------------------------------------------------------------------
// A PostgreSQL server that knows three things: to decline TLS and GSS
// encryption, to ask for a cleartext password, and to refuse whatever it gets.
// It serves one connection and records what it saw.
// ---------------------------------------------------------------------------
class FakePostgres {
public:
    FakePostgres() {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        socklen_t len = sizeof(a);
        if (listener_ < 0 ||
            ::bind(listener_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 ||
            ::listen(listener_, 4) != 0 ||
            ::getsockname(listener_, reinterpret_cast<sockaddr*>(&a), &len) != 0) {
            return;
        }
        port_ = ntohs(a.sin_port);
        thread_ = std::thread([this] { Serve(); });
    }

    ~FakePostgres() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
        if (listener_ >= 0) ::close(listener_);
    }

    int Port() const { return port_; }

    // Waits for the one connection to finish (or for nobody to come).
    void Finish() {
        if (thread_.joinable()) thread_.join();
    }

    bool connected = false;             // anyone opened a connection
    bool askedForEncryption = false;    // SSLRequest or GSSENCRequest seen
    bool sawStartup = false;            // the StartupMessage itself
    std::string password;               // what the PasswordMessage carried

private:
    static bool ReadExact(int fd, void* buf, size_t n) {
        auto* p = static_cast<uint8_t*>(buf);
        while (n > 0) {
            pollfd pfd{fd, POLLIN, 0};
            if (::poll(&pfd, 1, 5000) <= 0) return false;
            const ssize_t r = ::recv(fd, p, n, 0);
            if (r <= 0) return false;
            p += r;
            n -= static_cast<size_t>(r);
        }
        return true;
    }
    static void WriteAll(int fd, const std::vector<uint8_t>& b) {
        size_t off = 0;
        while (off < b.size()) {
            const ssize_t w = ::send(fd, b.data() + off, b.size() - off, MSG_NOSIGNAL);
            if (w <= 0) return;
            off += static_cast<size_t>(w);
        }
    }
    static uint32_t U32(const uint8_t* p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) << 8) | p[3];
    }
    static void PutU32(std::vector<uint8_t>& b, uint32_t v) {
        for (int i = 3; i >= 0; --i) b.push_back(uint8_t(v >> (i * 8)));
    }

    void Serve() {
        // Up to five seconds for a client; none is the expected outcome of
        // the tests that are refused before connecting.
        int fd = -1;
        for (int i = 0; i < 50 && !stop_ && fd < 0; ++i) {
            pollfd pfd{listener_, POLLIN, 0};
            if (::poll(&pfd, 1, 100) > 0) fd = ::accept(listener_, nullptr, nullptr);
        }
        if (fd < 0) return;
        connected = true;
        Converse(fd);
        ::close(fd);
    }

    void Converse(int fd) {
        // Startup phase: an encryption request is answered 'N' and the client
        // either gives up (sslmode=verify-full) or sends the real startup.
        for (;;) {
            uint8_t head[8];
            if (!ReadExact(fd, head, 4)) return;
            const uint32_t len = U32(head);
            if (len < 8 || len > 10000) return;
            std::vector<uint8_t> rest(len - 4);
            if (!ReadExact(fd, rest.data(), rest.size())) return;
            const uint32_t code = U32(rest.data());
            if (code == 80877103u || code == 80877104u) {   // SSLRequest / GSSENCRequest
                askedForEncryption = true;
                WriteAll(fd, {'N'});
                continue;
            }
            sawStartup = true;
            break;
        }

        // AuthenticationCleartextPassword.
        std::vector<uint8_t> auth{'R'};
        PutU32(auth, 8);
        PutU32(auth, 3);
        WriteAll(fd, auth);

        uint8_t type = 0;
        uint8_t lenBuf[4];
        if (!ReadExact(fd, &type, 1) || type != 'p' || !ReadExact(fd, lenBuf, 4)) return;
        const uint32_t len = U32(lenBuf);
        if (len < 5 || len > 10000) return;
        std::vector<uint8_t> body(len - 4);
        if (!ReadExact(fd, body.data(), body.size())) return;
        password.assign(reinterpret_cast<const char*>(body.data()));   // NUL-terminated

        // ErrorResponse: FATAL 28P01 invalid_password.
        std::vector<uint8_t> err;
        auto feld = [&](char code, const char* text) {
            err.push_back(uint8_t(code));
            err.insert(err.end(), text, text + std::strlen(text) + 1);
        };
        feld('S', "FATAL");
        feld('V', "FATAL");
        feld('C', "28P01");
        feld('M', "fake server: password recorded");
        err.push_back(0);
        std::vector<uint8_t> msg{'E'};
        PutU32(msg, uint32_t(err.size() + 4));
        msg.insert(msg.end(), err.begin(), err.end());
        WriteAll(fd, msg);
    }

    int listener_ = -1;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

bool HasPostgresDriver() {
    for (const std::string& t : UltraDatabase_GetSupportedDrivers())
        if (t == "postgresql") return true;
    return false;
}

// Quotes and a backslash: the connection string must escape both, or libpq
// sends a different password (or cannot parse the string at all).
const std::string kPasswort = "s3cr'et \\pw";
const std::string kSchluessel = "ultrafibu.test.pg-password";

void TestDriverSendsTheVaultPassword() {
    std::printf("Treiber: Passwort aus UltraVault\n");
    FakePostgres server;
    Check(server.Port() > 0, "the fake server listens on the loopback interface");

    UltraDbConnectionConfig cfg;
    cfg.name        = "login-driver";
    cfg.driver      = "postgresql";
    cfg.database    = "fibu";
    cfg.host        = "127.0.0.1";
    cfg.port        = server.Port();
    cfg.user        = "fibu";
    cfg.credentials = "vault:" + kSchluessel;
    cfg.tls         = UltraDbTls::Disable;   // the fake speaks no TLS
    cfg.options["gssencmode"] = "disable";
    Check(bool(UltraDb_RegisterConnection(cfg)), "the connection registers");
    const UltraDbResult r = UltraDb_OpenConnection(cfg.name);
    server.Finish();

    Check(!r, "the fake server refuses the login, as it always does");
    Check(!Contains(r.message, "UltraVault"),
          "the refusal is the server's, not the vault's: " + r.message);
    Check(server.sawStartup, "libpq got as far as the startup message");
    Check(server.password == kPasswort,
          "the password on the wire is the one stored in the vault (got \"" +
          server.password + "\")");
}

void TestOpenServerResolvesTheKey() {
    std::printf("OpenServer: vault:-Schluessel\n");
    FakePostgres server;
    Store store;
    const StoreResult r = store.OpenServer("login-open", "127.0.0.1", server.Port(),
                                           "fibu", "fibu", "vault:" + kSchluessel);
    server.Finish();

    // OpenServer insists on verify-full; the fake declines TLS, so libpq stops
    // there. Reaching the server at all is what proves the key was resolved:
    // a failed lookup refuses before any connection is made (next test).
    Check(!r.ok, "the fake server (no TLS) is not accepted as a database");
    Check(server.connected, "OpenServer resolved the key and went on to connect");
    Check(server.askedForEncryption, "and asked for TLS first, as verify-full must");
    Check(server.password.empty(), "no password is sent without TLS");
    Check(Contains(r.fehler, "nicht erreichbar") && !Contains(r.fehler, "UltraVault"),
          "the message is about the server, not the vault: " + r.fehler);
}

void TestMissingKeyIsRefusedBeforeConnecting() {
    std::printf("OpenServer: Schluessel fehlt\n");
    FakePostgres server;
    Store store;
    const StoreResult r = store.OpenServer("login-missing", "127.0.0.1", server.Port(),
                                           "fibu", "fibu", "vault:ultrafibu.test.nobody");
    server.Finish();
    Check(!r.ok, "a key that is not in the vault is refused");
    Check(Contains(r.fehler, "not in UltraVault"), "and says so: " + r.fehler);
    Check(!server.connected, "before any connection is made");
}

void TestClosedVaultIsRefusedBeforeConnecting() {
    std::printf("OpenServer: UltraVault geschlossen\n");
    UltraVault::Shutdown();
    FakePostgres server;
    Store store;
    const StoreResult r = store.OpenServer("login-closed", "127.0.0.1", server.Port(),
                                           "fibu", "fibu", "vault:" + kSchluessel);
    server.Finish();
    Check(!r.ok, "with the vault closed the login is refused");
    Check(Contains(r.fehler, "not open"), "and names the closed vault: " + r.fehler);
    Check(!server.connected, "before any connection is made");
}

} // namespace

int main() {
    std::printf("UltraFIBU: Server-Anmeldung ueber UltraVault\n\n");

    if (!HasPostgresDriver()) {
        const bool pflicht = std::getenv("ULTRAFIBU_TEST_PG_REQUIRED") != nullptr;
        std::printf("SKIP: this build has no PostgreSQL driver\n");
        return pflicht ? 1 : 0;
    }

    UltraVault::Config vc;
    vc.backend = UltraVault::Backend::Memory;
    if (!UltraVault::Initialize(vc)) {
        std::printf("FEHLER: the memory vault does not open\n");
        return 1;
    }
    Check(bool(UltraVault::Put(kSchluessel, UltraVault::SecretValue::FromString(kPasswort))),
          "the password is stored in a memory-backed vault");

    TestDriverSendsTheVaultPassword();
    TestOpenServerResolvesTheKey();
    TestMissingKeyIsRefusedBeforeConnecting();
    TestClosedVaultIsRefusedBeforeConnecting();   // last: it closes the vault

    std::printf("\n%d Pruefung(en) fehlgeschlagen\n", fehler);
    return fehler == 0 ? 0 : 1;
}
