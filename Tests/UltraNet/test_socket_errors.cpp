// Tests/UltraNet/test_socket_errors.cpp
// What a TCP receive and send answer, plain and through TLS:
//   - a receive that waits out its timeout answers Timeout, and the
//     connection is still there afterwards (the next receive gets data);
//   - a connection the peer closed answers Ok with no bytes;
//   - writing to a connection the peer closed fails - it does not raise
//     SIGPIPE, which (left at its default action, as here) would end the
//     test program.
// The TLS half talks to a python3 ssl server with a certificate made by the
// openssl command; it skips without either.
// Version: 0.1.3 - the certificate says serverAuth (macOS refuses one without)
// Version: 0.1.2 - "localhost" reached on 127.0.0.1 whatever it resolves to first
// Version: 0.1.1 - <unistd.h> for getpid (macOS)
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetSocket.h>
#include <UltraNet/UltraNetTls.h>

#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <unistd.h>      // getpid
#endif

namespace {

using Clock = std::chrono::steady_clock;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

long MsSince(Clock::time_point start) {
    return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
}

std::vector<uint8_t> Bytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::string Text(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

// SIGPIPE at its default action (end the program) while it lives, so a
// SIGPIPE from UltraNet cannot go unnoticed.
struct SigPipeDefault {
#if !defined(_WIN32)
    void (*previous)(int) = SIG_DFL;
    SigPipeDefault() { previous = std::signal(SIGPIPE, SIG_DFL); }
    ~SigPipeDefault() { std::signal(SIGPIPE, previous); }
#endif
};

// Writes until one fails (the peer is gone: the first writes may still be
// taken by the system); false when every one of `tries` succeeded.
bool WriteUntilRefused(UltraNetHandle handle, UltraNetResult& last, int tries = 60) {
    const std::vector<uint8_t> data(4096, 'x');
    for (int i = 0; i < tries; ++i) {
        int sent = 0;
        last = UltraNet_TcpSend(handle, data, sent);
        if (!last) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

bool HaveCommand(const char* name) {
#if defined(_WIN32)
    (void)name;
    return false;
#else
    return std::system((std::string("command -v ") + name + " > /dev/null 2>&1").c_str()) == 0;
#endif
}

} // namespace

TEST(tcp_receive_tells_timeout_closed_and_data_apart) {
    SigPipeDefault sigpipe;
    UltraNetSocketOptions options;
    options.bindAddress = "127.0.0.1";
    options.reuseAddress = true;
    UltraNetHandle listener = UltraNet_TcpListen(0, options);
    REQUIRE(listener != UltraNetInvalidHandle);
    UltraNetEndpoint local;
    REQUIRE(UltraNet_SocketLocalEndpoint(listener, local).success);

    UltraNetHandle client = UltraNet_TcpConnect("127.0.0.1", local.port);
    REQUIRE(client != UltraNetInvalidHandle);
    UltraNetEndpoint remote;
    UltraNetHandle server = UltraNet_TcpAccept(listener, remote, 3000);
    REQUIRE(server != UltraNetInvalidHandle);

    // Nothing sent: the receive waits its time and says Timeout.
    std::vector<uint8_t> got;
    auto start = Clock::now();
    UltraNetResult r = UltraNet_TcpReceive(client, got, 1024, 250);
    CHECK(!r.success);
    CHECK(r.code == UltraNetResultCode::Timeout);
    CHECK(got.empty());
    CHECK(MsSince(start) >= 150);

    // ... and the connection is still there.
    int sent = 0;
    REQUIRE(UltraNet_TcpSend(server, Bytes("ping"), sent).success);
    r = UltraNet_TcpReceive(client, got, 1024, 2000);
    CHECK(r.success);
    CHECK(Text(got) == "ping");

    // The peer closes: Ok with no bytes, at once.
    UltraNet_SocketClose(server);
    start = Clock::now();
    r = UltraNet_TcpReceive(client, got, 1024, 2000);
    CHECK(r.success);
    CHECK(got.empty());
    CHECK(MsSince(start) < 1500);

    // Writing to it fails - and the program is still here.
    UltraNetResult last;
    CHECK(WriteUntilRefused(client, last));
    CHECK(last.code == UltraNetResultCode::ConnectionReset || last.code == UltraNetResultCode::SendFailed);

    UltraNet_SocketClose(client);
    UltraNet_SocketClose(listener);
}

// A name with several addresses is reached on whichever takes the connection:
// "localhost" can resolve to ::1 before 127.0.0.1, and a listener on
// 127.0.0.1 alone must still be reached by name.
TEST(tcp_connect_tries_every_address_of_a_name) {
    UltraNetSocketOptions options;
    options.bindAddress = "127.0.0.1";
    options.reuseAddress = true;
    UltraNetHandle listener = UltraNet_TcpListen(0, options);
    REQUIRE(listener != UltraNetInvalidHandle);
    UltraNetEndpoint local;
    REQUIRE(UltraNet_SocketLocalEndpoint(listener, local).success);
    UltraNetHandle client = UltraNetInvalidHandle;
    UltraNetSocketOptions connect;
    connect.connectTimeoutMs = 3000;
    const UltraNetResult r = UltraNet_TcpOpen("localhost", local.port, connect, client);
    CHECK(r.success);
    CHECK(client != UltraNetInvalidHandle);
    if (client != UltraNetInvalidHandle) UltraNet_SocketClose(client);
    UltraNet_SocketClose(listener);
}

TEST(udp_receive_timeout_says_timeout) {
    UltraNetSocketOptions options;
    options.bindAddress = "127.0.0.1";
    UltraNetHandle udp = UltraNet_UdpOpen(0, options);
    REQUIRE(udp != UltraNetInvalidHandle);
    std::vector<uint8_t> got;
    UltraNetEndpoint from;
    const UltraNetResult r = UltraNet_UdpReceive(udp, got, from, 200);
    CHECK(!r.success);
    CHECK(r.code == UltraNetResultCode::Timeout);
    UltraNet_SocketClose(udp);
}

TEST(tls_receive_tells_timeout_closed_and_data_apart) {
#if defined(_WIN32)
    SKIP("needs python3 and openssl on a POSIX system");
#else
    if (!HaveCommand("python3") || !HaveCommand("openssl")) SKIP("needs python3 and openssl");
    SigPipeDefault sigpipe;

    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("ultranet_socket_errors_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const std::string cert = PathToUtf8(dir / "cert.pem");
    const std::string key = PathToUtf8(dir / "key.pem");
    const std::string script = PathToUtf8(dir / "server.py");
    const std::string make = "openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 2"
                             " -subj /CN=localhost -addext subjectAltName=DNS:localhost"
                             // macOS trusts a TLS server's certificate only with these.
                             " -addext extendedKeyUsage=serverAuth -addext keyUsage=digitalSignature"
                             " -keyout '" + key + "' -out '" + cert + "' > /dev/null 2>&1";
    if (std::system(make.c_str()) != 0) {
        fs::remove_all(dir);
        SKIP("openssl could not make a certificate");
    }
    {
        // Sends "hello", then waits for "bye" and closes properly (close_notify).
        std::ofstream out(PathFromUtf8(script));
        out << "import socket, ssl, sys\n"
               "ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)\n"
               "ctx.load_cert_chain(sys.argv[1], sys.argv[2])\n"
               "s = socket.socket(); s.bind(('127.0.0.1', 0)); s.listen(1)\n"
               "print(s.getsockname()[1], flush=True)\n"
               "s.settimeout(20)\n"
               "c, _ = s.accept(); c.settimeout(20)\n"
               "t = ctx.wrap_socket(c, server_side=True)\n"
               "t.sendall(b'hello')\n"
               "data = b''\n"
               "while b'bye' not in data:\n"
               "    more = t.recv(100)\n"
               "    if not more: break\n"
               "    data += more\n"
               "try:\n"
               "    t.unwrap().close()\n"
               "except Exception:\n"
               "    t.close()\n";
    }
    const std::string command = "python3 '" + script + "' '" + cert + "' '" + key + "'";
    FILE* server = ::popen(command.c_str(), "r");
    REQUIRE(server != nullptr);
    char line[32] = {};
    const bool started = std::fgets(line, sizeof line, server) != nullptr;
    const int port = started ? std::atoi(line) : 0;
    REQUIRE(port > 0);

    UltraNetHandle h = UltraNet_TcpConnect("127.0.0.1", port);
    REQUIRE(h != UltraNetInvalidHandle);
    UltraNetTlsOptions tls;
    tls.caBundlePath = cert;
    tls.sniHostname = "localhost";
    REQUIRE(UltraNet_TlsWrap(h, "localhost", tls) == h);
    const UltraNetResult shaken = UltraNet_TlsHandshake(h);
    if (!shaken.success) std::fprintf(stderr, "\n    handshake: %s", shaken.message.c_str());
    REQUIRE(shaken.success);

    std::vector<uint8_t> got;
    std::string text;
    for (int i = 0; i < 10 && text.size() < 5; ++i) {
        if (UltraNet_TcpReceive(h, got, 1024, 2000).success) text += Text(got);
    }
    CHECK(text == "hello");

    // Nothing more yet: Timeout, not an end.
    auto start = Clock::now();
    UltraNetResult r = UltraNet_TcpReceive(h, got, 1024, 250);
    CHECK(!r.success);
    CHECK(r.code == UltraNetResultCode::Timeout);
    CHECK(MsSince(start) >= 150);

    // "bye": the server closes properly - Ok with no bytes.
    int sent = 0;
    REQUIRE(UltraNet_TcpSend(h, Bytes("bye"), sent).success);
    bool closed = false;
    for (int i = 0; i < 10 && !closed; ++i) {
        r = UltraNet_TcpReceive(h, got, 1024, 2000);
        if (r.success && got.empty()) closed = true;
        else if (!r.success && r.code != UltraNetResultCode::Timeout) break;
    }
    CHECK(closed);

    // Writing on: fails, without SIGPIPE.
    UltraNetResult last;
    CHECK(WriteUntilRefused(h, last));

    UltraNet_SocketClose(h);
    ::pclose(server);
    fs::remove_all(dir);
#endif
}
