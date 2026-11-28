// Tests/UltraNet/test_websocket_own.cpp
// UltraNet's own WebSocket client (core/UltraNet/UltraNetWebSocketOwn.h) -
// what UltraNet_WebSocket* runs on where libcurl has no WebSockets (Apple's
// own libcurl through macOS 15, some distributions' builds).
//
// The own_ws_ tests take the client by its internal class, so they test it
// on every system whichever client UltraNet would choose there: the address
// and the accept key (the RFC's own example), the upgrade as it is asked and
// every answer that is no upgrade, fragments with a control frame between
// them, lengths of every encoding both ways, frames that are not the
// protocol, a frame over the allowance (refused before any of it is kept),
// idle / partial / closed, wss - a certificate nobody vouches for refused,
// one that was added as trusted taken - and the proxy setting: an HTTP proxy
// gone through as a CONNECT tunnel, this machine reached without it.
//
// The ws_ tests at the end go through UltraNet_WebSocket* and hold for either
// client: a message put together from fragments, wss with and without
// verification, and the proxy setting. With test_websocket.cpp's they run
// once more on the own client in CTest's UltraNetOwnWebSocket.
//
// The other side is a scripted server (standard-library python: asyncio, ssl)
// whose path chooses what it does - a real server sends none of the frames
// that are wrong. The wss tests need the openssl program for two
// certificates; all of them skip without python3 (and on Windows, where the
// server is not started).
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetSocket.h>
#include <UltraNet/UltraNetTls.h>
#include <UltraNet/UltraNetWebSocket.h>

#include "../../UltraCanvas/core/UltraNet/UltraNetSha1.h"
#include "../../UltraCanvas/core/UltraNet/UltraNetWebSocketOwn.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using ultranet_internal::OwnWebSocket;
using ultranet_internal::ParseWebSocketAddress;
using ultranet_internal::WebSocketAcceptFor;
using ultranet_internal::WebSocketAddress;

namespace {

std::string Hex(const std::array<uint8_t, 20>& digest) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (uint8_t b : digest) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 15]);
    }
    return out;
}

#if !defined(_WIN32)
constexpr int kPlainPort = 18651;       // the scripted server
constexpr int kTlsPort = 18652;         // the same over TLS, a certificate nobody vouches for
constexpr int kTrustedTlsPort = 18653;  // over TLS, the certificate a test adds as trusted
constexpr int kProxyPort = 18654;       // an HTTP proxy: every CONNECT ends at the scripted server

long g_serverPid = -1;
std::filesystem::path g_dir;
bool g_started = false;
bool g_haveTls = false;
std::mutex g_serverMutex;

const char* const kServer = R"PY(
import asyncio, base64, hashlib, json, ssl, struct, sys

GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
CONNECTS = []       # what the proxy below was asked to connect to
PLAIN_PORT = 0

def frame(opcode, payload=b"", fin=True, mask=False, rsv=0, announce=None):
    first = (0x80 if fin else 0) | (rsv << 4) | opcode
    n = len(payload) if announce is None else announce
    bit = 0x80 if mask else 0
    if n < 126:
        head = bytes([first, bit | n])
    elif n <= 0xFFFF:
        head = bytes([first, bit | 126]) + struct.pack(">H", n)
    else:
        head = bytes([first, bit | 127]) + struct.pack(">Q", n)
    if mask:
        key = b"\x11\x22\x33\x44"
        head += key
        payload = bytes(b ^ key[i % 4] for i, b in enumerate(payload))
    return head + payload

async def read_frame(reader):
    b = await reader.readexactly(2)
    opcode, fin, masked, n = b[0] & 0x0F, bool(b[0] & 0x80), bool(b[1] & 0x80), b[1] & 0x7F
    if n == 126:
        n = struct.unpack(">H", await reader.readexactly(2))[0]
    elif n == 127:
        n = struct.unpack(">Q", await reader.readexactly(8))[0]
    key = await reader.readexactly(4) if masked else None
    data = await reader.readexactly(n)
    if key:
        data = bytes(b ^ key[i % 4] for i, b in enumerate(data))
    return opcode, fin, masked, data

def pattern(n):
    return bytes(i % 251 for i in range(n))

async def handle(reader, writer):
    try:
        request = (await reader.readuntil(b"\r\n\r\n")).decode("latin-1")
        lines = request.split("\r\n")
        target = lines[0].split(" ")[1]
        path = target.split("?")[0]
        headers = {}
        for line in lines[1:]:
            if ":" in line:
                k, v = line.split(":", 1)
                headers[k.strip().lower()] = v.strip()
        accept = base64.b64encode(hashlib.sha1(headers.get("sec-websocket-key", "").encode() + GUID).digest()).decode()

        def answer(extra="", accept_value=None):
            writer.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: WebSocket\r\n"
                          "Connection: keep-alive, Upgrade\r\nSec-WebSocket-Accept: %s\r\n%s\r\n"
                          % (accept_value or accept, extra)).encode())

        # ---- answers that are no upgrade ------------------------------------------
        if path == "/status-403":
            writer.write(b"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n")
            await writer.drain(); writer.close(); return
        if path == "/not-http":
            writer.write(b"SSH-2.0-OpenSSH_9.6\r\n\r\n")
            await writer.drain(); writer.close(); return
        if path == "/hang-up":
            writer.close(); return
        if path == "/bad-accept":
            answer(accept_value="AAAAAAAAAAAAAAAAAAAAAAAAAAA=")
        elif path == "/no-upgrade":
            writer.write(("HTTP/1.1 101 Switching Protocols\r\nSec-WebSocket-Accept: %s\r\n\r\n" % accept).encode())
        elif path == "/extension":
            answer("Sec-WebSocket-Extensions: permessage-deflate\r\n")
        elif path == "/subprotocol-other":
            answer("Sec-WebSocket-Protocol: other\r\n")
        elif path == "/subprotocol-b":
            answer("Sec-WebSocket-Protocol: b\r\n")
        else:
            answer()

        # ---- what the server says first ----------------------------------------------
        if path == "/headers":
            writer.write(frame(1, json.dumps({"target": target, "headers": headers}, sort_keys=True).encode()))
        elif path == "/proxy-log":
            writer.write(frame(1, json.dumps(CONNECTS).encode()))
        elif path == "/fragments":
            # A message in three frames, a ping between two of them - in one write.
            writer.write(frame(1, b"Hel", fin=False) + frame(9, b"p") + frame(0, b"l", fin=False) + frame(0, b"o"))
        elif path == "/lengths":
            writer.write(frame(2, pattern(300)) + frame(2, pattern(70000)) + frame(2, b""))
        elif path == "/masked":
            writer.write(frame(1, b"hello", mask=True))
        elif path == "/rsv":
            writer.write(frame(1, b"hello", rsv=4))
        elif path == "/long-ping":
            writer.write(frame(9, b"x" * 126))
        elif path == "/ping-in-fragments":
            writer.write(frame(9, b"x", fin=False))
        elif path == "/unknown-opcode":
            writer.write(frame(3, b"x"))
        elif path == "/huge":
            writer.write(frame(2, b"", announce=1 << 30))      # the header alone: a gigabyte to come
        elif path == "/split":
            # One frame, a byte at a time for its header, then its payload in two parts.
            whole = frame(1, b"split across writes")
            for i in range(4):
                writer.write(whole[i:i + 1]); await writer.drain(); await asyncio.sleep(0.03)
            writer.write(whole[4:10]); await writer.drain(); await asyncio.sleep(0.03)
            writer.write(whole[10:])
        elif path == "/close-1001":
            writer.write(frame(8, struct.pack(">H", 1001) + b"going away"))
        elif path == "/close-now":
            await writer.drain(); writer.close(); return
        await writer.drain()

        # ---- then: what the client sends -------------------------------------------
        while True:
            opcode, fin, masked, data = await read_frame(reader)
            if opcode == 8:
                writer.write(frame(8, data[:2])); await writer.drain(); break
            if opcode == 9:
                writer.write(frame(10, data))
            elif path == "/lengths":
                # What arrived, said back: a client's frames are masked.
                writer.write(frame(1, ("opcode=%d masked=%d size=%d sum=%d"
                                       % (opcode, masked, len(data), sum(data) % 65521)).encode()))
            elif path in ("/echo", "/subprotocol-b", "/quiet") and opcode in (1, 2):
                writer.write(frame(opcode, data))
            await writer.drain()
    except (asyncio.IncompleteReadError, ConnectionError, OSError):
        pass
    finally:
        try:
            writer.close()
        except Exception:
            pass

async def pipe(reader, writer):
    try:
        while True:
            data = await reader.read(65536)
            if not data:
                break
            writer.write(data)
            await writer.drain()
    except (ConnectionError, OSError):
        pass
    finally:
        try:
            writer.close()
        except Exception:
            pass

# An HTTP proxy for one thing: CONNECT. Whatever host is named, the tunnel
# ends at the scripted server - so a test can name a host that is not this
# machine (which is never reached through a proxy) and still arrive here.
async def proxy(reader, writer):
    try:
        request = (await reader.readuntil(b"\r\n\r\n")).decode("latin-1")
        method, target = request.split("\r\n")[0].split(" ")[:2]
        if method != "CONNECT":
            writer.write(b"HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n")
            await writer.drain(); writer.close(); return
        CONNECTS.append(target)
        up_reader, up_writer = await asyncio.open_connection("127.0.0.1", PLAIN_PORT)
        writer.write(b"HTTP/1.1 200 Connection established\r\n\r\n")
        await writer.drain()
        await asyncio.gather(pipe(reader, up_writer), pipe(up_reader, writer))
    except (asyncio.IncompleteReadError, ConnectionError, OSError, ValueError):
        try:
            writer.close()
        except Exception:
            pass

async def main():
    global PLAIN_PORT
    plain, tls, trusted, cert_a, key_a, cert_b, key_b, proxy_port = sys.argv[1:9]
    PLAIN_PORT = int(plain)
    servers = [await asyncio.start_server(handle, "127.0.0.1", int(plain)),
               await asyncio.start_server(proxy, "127.0.0.1", int(proxy_port))]
    if cert_a:
        for port, cert, key in ((tls, cert_a, key_a), (trusted, cert_b, key_b)):
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(cert, key)
            servers.append(await asyncio.start_server(handle, "127.0.0.1", int(port), ssl=context))
    await asyncio.Future()

asyncio.run(main())
)PY";

bool StartServer() {
    std::lock_guard<std::mutex> lk(g_serverMutex);
    if (g_started) return true;
    if (std::system("command -v python3 > /dev/null 2>&1") != 0) return false;

    g_dir = std::filesystem::temp_directory_path() / ("ultranet_own_ws_test_" + std::to_string(::getpid()));
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    const std::string dir = g_dir.string();     // path-string-ok: a temp folder of ASCII names, POSIX only
    {
        std::ofstream out(g_dir / "server.py");
        out << kServer;
    }
    // Two certificates for 127.0.0.1, each its own issuer: one nobody vouches
    // for, one a test adds as trusted.
    g_haveTls = true;
    for (const char* name : {"a", "b"}) {
        const std::string make =
            "openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj \"/CN=localhost\" "
            "-addext \"subjectAltName=DNS:localhost,IP:127.0.0.1\" -addext \"extendedKeyUsage=serverAuth\" "
            "-addext \"keyUsage=digitalSignature,keyEncipherment\" -keyout \"" + dir + "/key-" + name +
            ".pem\" -out \"" + dir + "/cert-" + name + ".pem\" > /dev/null 2>&1";
        if (std::system(make.c_str()) != 0) g_haveTls = false;
    }

    const long pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        std::freopen("/dev/null", "w", stdout);
        const std::string script = dir + "/server.py";
        const std::string plain = std::to_string(kPlainPort), tls = std::to_string(kTlsPort),
                          trusted = std::to_string(kTrustedTlsPort);
        const std::string certA = g_haveTls ? dir + "/cert-a.pem" : "", keyA = g_haveTls ? dir + "/key-a.pem" : "";
        const std::string certB = g_haveTls ? dir + "/cert-b.pem" : "", keyB = g_haveTls ? dir + "/key-b.pem" : "";
        const std::string proxy = std::to_string(kProxyPort);
        execlp("python3", "python3", script.c_str(), plain.c_str(), tls.c_str(), trusted.c_str(), certA.c_str(),
               keyA.c_str(), certB.c_str(), keyB.c_str(), proxy.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    g_serverPid = pid;
    for (int i = 0; i < 80; ++i) {
        UltraNetSocketOptions o;
        o.connectTimeoutMs = 100;
        const UltraNetHandle h = UltraNet_TcpConnect("127.0.0.1", g_haveTls ? kTrustedTlsPort : kPlainPort, o);
        if (h != UltraNetInvalidHandle) {
            UltraNet_SocketClose(h);
            g_started = true;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

struct ServerCleanup {
    ~ServerCleanup() {
        std::lock_guard<std::mutex> lk(g_serverMutex);
        if (g_serverPid > 0) {
            kill(static_cast<pid_t>(g_serverPid), SIGTERM);
            int status = 0;
            waitpid(static_cast<pid_t>(g_serverPid), &status, 0);
            g_serverPid = -1;
        }
        if (!g_dir.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(g_dir, ec);
        }
    }
};
ServerCleanup g_cleanup;

std::string Url(const std::string& path) {
    return "ws://127.0.0.1:" + std::to_string(kPlainPort) + path;
}
std::string TlsUrl(const std::string& path, bool trusted = false) {
    return "wss://127.0.0.1:" + std::to_string(trusted ? kTrustedTlsPort : kTlsPort) + path;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream all;
    all << in.rdbuf();
    return all.str();
}

std::vector<uint8_t> Pattern(size_t n) {
    std::vector<uint8_t> bytes(n);
    for (size_t i = 0; i < n; ++i) bytes[i] = static_cast<uint8_t>(i % 251);
    return bytes;
}

// The next thing the connection has to say that is not "nothing yet".
OwnWebSocket::Read Next(OwnWebSocket& ws, OwnWebSocket::Frame& frame, std::string& error,
                        size_t allowance = 64 * 1024 * 1024, int seconds = 5) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < until) {
        const OwnWebSocket::Read r = ws.Receive(50, allowance, frame, error);
        if (r != OwnWebSocket::Read::Idle && r != OwnWebSocket::Read::Partial) return r;
    }
    return OwnWebSocket::Read::Idle;
}

std::string Text(const OwnWebSocket::Frame& frame) {
    return std::string(frame.payload.begin(), frame.payload.end());
}

#define NEED_SERVER()                                                          \
    UltraNet_Initialize();                                                     \
    if (!StartServer()) SKIP("python3 is not available for the scripted server")
#endif // !_WIN32

} // namespace

// ============================================================================
// THE PARTS THAT NEED NO CONNECTION
// ============================================================================

TEST(own_ws_sha1_is_sha1) {
    using ultranet_internal::Sha1;
    CHECK(Hex(Sha1("abc", 3)) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(Hex(Sha1("", 0)) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";     // 56 bytes: two blocks
    CHECK(Hex(Sha1(two.data(), two.size())) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    const std::string million(1000000, 'a');
    CHECK(Hex(Sha1(million.data(), million.size())) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    // The lengths around a block's end, where the padding changes shape
    // (hashlib's digests of the same bytes).
    const std::string sixtyThree(63, 'x'), sixtyFour(64, 'x'), fiftyFive(55, 'x');
    CHECK(Hex(Sha1(fiftyFive.data(), 55)) == "cef734ba81a024479e09eb5a75b6ddae62e6abf1");
    CHECK(Hex(Sha1(sixtyThree.data(), 63)) == "0ddc4e0cccd9a12850deb5abb0853a4425559fec");
    CHECK(Hex(Sha1(sixtyFour.data(), 64)) == "bb2fa3ee7afb9f54c6dfb5d021f14b1ffe40c163");
}

TEST(own_ws_accept_key_is_the_rfcs) {
    // RFC 6455 1.3: the key of its example and the answer to it.
    CHECK(WebSocketAcceptFor("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(own_ws_address_parts) {
    WebSocketAddress a;
    std::string error;
    REQUIRE(ParseWebSocketAddress("ws://example.org/chat", a, error));
    CHECK(!a.tls && a.host == "example.org" && a.port == 80 && a.hostHeader == "example.org" && a.target == "/chat");
    REQUIRE(ParseWebSocketAddress("WSS://Example.org", a, error));
    CHECK(a.tls && a.host == "Example.org" && a.port == 443 && a.hostHeader == "Example.org" && a.target == "/");
    // A port of its own is in the Host header; the scheme's is not.
    REQUIRE(ParseWebSocketAddress("wss://example.org:8443/a/b?x=1&y=%20#top", a, error));
    CHECK(a.port == 8443 && a.hostHeader == "example.org:8443" && a.target == "/a/b?x=1&y=%20");
    REQUIRE(ParseWebSocketAddress("wss://example.org:443/", a, error));
    CHECK(a.hostHeader == "example.org");
    // A query and no path.
    REQUIRE(ParseWebSocketAddress("ws://example.org?token=abc", a, error));
    CHECK(a.target == "/?token=abc" && a.host == "example.org");
    // IPv6: the brackets are the address's in the header, not the host's.
    REQUIRE(ParseWebSocketAddress("ws://[::1]:9000/x", a, error));
    CHECK(a.host == "::1" && a.port == 9000 && a.hostHeader == "[::1]:9000" && a.target == "/x");
    REQUIRE(ParseWebSocketAddress("ws://[2001:db8::7]/", a, error));
    CHECK(a.host == "2001:db8::7" && a.port == 80 && a.hostHeader == "[2001:db8::7]");
    // Who signs in, taken out of the address.
    REQUIRE(ParseWebSocketAddress("wss://jane:p%40ss@example.org/x", a, error));
    CHECK(a.userInfo == "jane:p@ss" && a.host == "example.org" && a.hostHeader == "example.org");

    CHECK(!ParseWebSocketAddress("https://example.org/", a, error) && error.find("ws://") != std::string::npos);
    CHECK(!ParseWebSocketAddress("example.org/chat", a, error));
    CHECK(!ParseWebSocketAddress("ws:///chat", a, error) && error.find("host") != std::string::npos);
    CHECK(!ParseWebSocketAddress("ws://example.org:port/", a, error));
    CHECK(!ParseWebSocketAddress("ws://example.org:0/", a, error));
    CHECK(!ParseWebSocketAddress("ws://example.org:70000/", a, error));
    CHECK(!ParseWebSocketAddress("ws://[::1/", a, error));
    // Nothing that would end the request line.
    CHECK(!ParseWebSocketAddress("ws://example.org/a b", a, error));
    CHECK(!ParseWebSocketAddress("ws://example.org/a\r\nX-Injected: 1", a, error));
}

// ============================================================================
// THE UPGRADE
// ============================================================================

TEST(own_ws_asks_for_the_upgrade_as_it_should) {
#if defined(_WIN32)
    SKIP("the scripted server is POSIX-only");
#else
    NEED_SERVER();
    UltraNetWebSocketOptions options;
    options.headers.Set("X-Test", "yes it is");
    options.headers.Set("Origin", "https://app.example");
    options.headers.Set("Upgrade", "not the caller's to say");
    options.subprotocols = {"a", "b"};
    std::string error;
    const std::string url = "ws://jane:p%40ss@127.0.0.1:" + std::to_string(kPlainPort) + "/headers?room=7&x=%20#frag";
    auto ws = OwnWebSocket::Open(url, options, error);
    REQUIRE(ws != nullptr);
    OwnWebSocket::Frame frame;
    REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
    const std::string said = Text(frame);
    // The path and the query, without the fragment; the Host with its port.
    CHECK(said.find("\"target\": \"/headers?room=7&x=%20\"") != std::string::npos);
    CHECK(said.find("\"host\": \"127.0.0.1:" + std::to_string(kPlainPort) + "\"") != std::string::npos);
    CHECK(said.find("\"upgrade\": \"websocket\"") != std::string::npos);
    CHECK(said.find("\"connection\": \"Upgrade\"") != std::string::npos);
    CHECK(said.find("\"sec-websocket-version\": \"13\"") != std::string::npos);
    CHECK(said.find("\"sec-websocket-protocol\": \"a, b\"") != std::string::npos);
    CHECK(said.find("\"x-test\": \"yes it is\"") != std::string::npos);
    CHECK(said.find("\"origin\": \"https://app.example\"") != std::string::npos);
    // jane:p@ss
    CHECK(said.find("\"authorization\": \"Basic amFuZTpwQHNz\"") != std::string::npos);
    // A key of 16 bytes, base64: 24 characters ending "==".
    const size_t key = said.find("\"sec-websocket-key\": \"");
    REQUIRE(key != std::string::npos);
    const std::string value = said.substr(key + 22, said.find('"', key + 22) - key - 22);
    CHECK(value.size() == 24 && value.compare(22, 2, "==") == 0);

    // Another connection, another key.
    auto second = OwnWebSocket::Open(url, options, error);
    REQUIRE(second != nullptr);
    REQUIRE(Next(*second, frame, error) == OwnWebSocket::Read::Frame);
    const std::string again = Text(frame);
    const size_t key2 = again.find("\"sec-websocket-key\": \"");
    REQUIRE(key2 != std::string::npos);
    CHECK(again.substr(key2 + 22, 24) != value);

    // A header that is two lines is not sent.
    UltraNetWebSocketOptions injected;
    injected.headers.Set("X-Test", "one\r\nX-Injected: two");
    CHECK(OwnWebSocket::Open(Url("/headers"), injected, error) == nullptr);
    CHECK(error.find("one line") != std::string::npos);
#endif
}

TEST(own_ws_refuses_an_answer_that_is_no_upgrade) {
#if defined(_WIN32)
    SKIP("the scripted server is POSIX-only");
#else
    NEED_SERVER();
    const UltraNetWebSocketOptions options;
    std::string error;
    CHECK(OwnWebSocket::Open(Url("/status-403"), options, error) == nullptr);
    CHECK(error.find("HTTP/1.1 403 Forbidden") != std::string::npos);
    CHECK(OwnWebSocket::Open(Url("/not-http"), options, error) == nullptr);
    CHECK(error.find("not HTTP") != std::string::npos);
    CHECK(OwnWebSocket::Open(Url("/hang-up"), options, error) == nullptr);
    CHECK(error.find("closed the connection") != std::string::npos || error.find("broke") != std::string::npos);
    CHECK(OwnWebSocket::Open(Url("/bad-accept"), options, error) == nullptr);
    CHECK(error.find("Sec-WebSocket-Accept") != std::string::npos);
    CHECK(OwnWebSocket::Open(Url("/no-upgrade"), options, error) == nullptr);
    CHECK(error.find("without upgrading") != std::string::npos);
    CHECK(OwnWebSocket::Open(Url("/extension"), options, error) == nullptr);
    CHECK(error.find("permessage-deflate") != std::string::npos);
    // A subprotocol nobody offered - with none offered, and with others.
    CHECK(OwnWebSocket::Open(Url("/subprotocol-other"), options, error) == nullptr);
    CHECK(error.find("subprotocol") != std::string::npos);
    UltraNetWebSocketOptions offering;
    offering.subprotocols = {"a", "b"};
    CHECK(OwnWebSocket::Open(Url("/subprotocol-other"), offering, error) == nullptr);

    // Nobody there, and something that is no address.
    UltraNetWebSocketOptions quick;
    quick.connectTimeoutMs = 2000;
    CHECK(OwnWebSocket::Open("ws://127.0.0.1:1/", quick, error) == nullptr);
    CHECK(!error.empty());
    CHECK(OwnWebSocket::Open("http://127.0.0.1/", quick, error) == nullptr);
    CHECK(error.find("ws://") != std::string::npos);
#endif
}

TEST(own_ws_takes_the_subprotocol_the_server_chose) {
#if defined(_WIN32)
    SKIP("the scripted server is POSIX-only");
#else
    NEED_SERVER();
    UltraNetWebSocketOptions options;
    options.subprotocols = {"a", "b"};
    std::string error;
    auto ws = OwnWebSocket::Open(Url("/subprotocol-b"), options, error);
    REQUIRE(ws != nullptr);
    CHECK(ws->Subprotocol() == "b");
    CHECK(ws->Send(OwnWebSocket::Text, "over b", 6));
    OwnWebSocket::Frame frame;
    REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(frame.opcode == OwnWebSocket::Text && Text(frame) == "over b");
    auto none = OwnWebSocket::Open(Url("/echo"), options, error);
    REQUIRE(none != nullptr);
    CHECK(none->Subprotocol().empty());
#endif
}

// ============================================================================
// FRAMES
// ============================================================================

TEST(own_ws_reads_fragments_and_a_ping_between_them) {
#if defined(_WIN32)
    SKIP("the scripted server is POSIX-only");
#else
    NEED_SERVER();
    std::string error;
    auto ws = OwnWebSocket::Open(Url("/fragments"), UltraNetWebSocketOptions(), error);
    REQUIRE(ws != nullptr);
    // Four frames that came in one read, each handed out as itself.
    OwnWebSocket::Frame frame;
    REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(frame.opcode == OwnWebSocket::Text && !frame.fin && Text(frame) == "Hel");
    REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(frame.opcode == OwnWebSocket::Ping && frame.fin && Text(frame) == "p");
    REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(frame.opcode == OwnWebSocket::Continuation && !frame.fin && Text(frame) == "l");
    REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(frame.opcode == OwnWebSocket::Continuation && frame.fin && Text(frame) == "o");
    // A ping of ours is answered with its payload.
    CHECK(ws->Send(OwnWebSocket::Ping, "are you there", 13));
    REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(frame.opcode == OwnWebSocket::Pong && Text(frame) == "are you there");
    // And the close, with the code it was sent.
    const uint8_t bye[] = {0x03, 0xE8, 'b', 'y', 'e'};      // 1000
    CHECK(ws->Send(OwnWebSocket::Close, bye, sizeof bye));
    REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(frame.opcode == OwnWebSocket::Close && frame.payload.size() == 2 && frame.payload[0] == 0x03 &&
          frame.payload[1] == 0xE8);
#endif
}

TEST(own_ws_lengths_of_every_encoding_both_ways) {
#if defined(_WIN32)
    SKIP("the scripted server is POSIX-only");
#else
    NEED_SERVER();
    std::string error;
    auto ws = OwnWebSocket::Open(Url("/lengths"), UltraNetWebSocketOptions(), error);
    REQUIRE(ws != nullptr);
    // From the server: 16 bits of length, 64 bits of length, none at all.
    OwnWebSocket::Frame frame;
    REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(frame.opcode == OwnWebSocket::Binary && frame.payload == Pattern(300));
    REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(frame.payload == Pattern(70000));
    REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(frame.opcode == OwnWebSocket::Binary && frame.payload.empty());
    // To the server: every size at which the encoding changes. It says back
    // what it read - masked, as a client's frames are, and whole.
    for (const size_t size : {size_t(0), size_t(1), size_t(125), size_t(126), size_t(65535), size_t(65536),
                              size_t(300000)}) {
        const std::vector<uint8_t> bytes = Pattern(size);
        unsigned sum = 0;
        for (uint8_t b : bytes) sum = (sum + b) % 65521;
        REQUIRE(ws->Send(OwnWebSocket::Binary, bytes.data(), bytes.size()));
        REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
        CHECK(Text(frame) == "opcode=2 masked=1 size=" + std::to_string(size) + " sum=" + std::to_string(sum));
    }
    REQUIRE(ws->Send(OwnWebSocket::Text, "text", 4));
    REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(Text(frame).rfind("opcode=1 masked=1 size=4", 0) == 0);
#endif
}

TEST(own_ws_names_what_is_not_the_protocol) {
#if defined(_WIN32)
    SKIP("the scripted server is POSIX-only");
#else
    NEED_SERVER();
    const struct {
        const char* path;
        const char* says;
    } cases[] = {
        {"/masked", "masked frame from the server"},
        {"/rsv", "reserved bit"},
        {"/long-ping", "longer than 125 bytes"},
        {"/ping-in-fragments", "control frame in fragments"},
        {"/unknown-opcode", "unknown frame (opcode 3)"},
    };
    for (const auto& c : cases) {
        std::string error;
        auto ws = OwnWebSocket::Open(Url(c.path), UltraNetWebSocketOptions(), error);
        REQUIRE(ws != nullptr);
        OwnWebSocket::Frame frame;
        const OwnWebSocket::Read r = Next(*ws, frame, error);
        if (r != OwnWebSocket::Read::Malformed || error.find(c.says) == std::string::npos) {
            ultranet_test::RecordFail(__FILE__, __LINE__, std::string(c.path) + ": " + error);
        }
        // And again: what is wrong stays wrong, it is not read past.
        CHECK(ws->Receive(50, 1 << 20, frame, error) == OwnWebSocket::Read::Malformed);
    }
#endif
}

TEST(own_ws_keeps_nothing_of_a_frame_over_the_allowance) {
#if defined(_WIN32)
    SKIP("the scripted server is POSIX-only");
#else
    NEED_SERVER();
    std::string error;
    auto ws = OwnWebSocket::Open(Url("/huge"), UltraNetWebSocketOptions(), error);
    REQUIRE(ws != nullptr);
    // A gigabyte is announced and none of it sent: refused by its header.
    OwnWebSocket::Frame frame;
    const auto before = std::chrono::steady_clock::now();
    CHECK(Next(*ws, frame, error, 1024 * 1024) == OwnWebSocket::Read::TooBig);
    CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(3));
    // Within the allowance the same frame is waited for instead.
    auto roomy = OwnWebSocket::Open(Url("/huge"), UltraNetWebSocketOptions(), error);
    REQUIRE(roomy != nullptr);
    CHECK(Next(*roomy, frame, error, size_t(2) << 30, 1) == OwnWebSocket::Read::Idle);
    // The allowance is the data's: a control frame is 125 bytes at most anyway.
    auto ping = OwnWebSocket::Open(Url("/fragments"), UltraNetWebSocketOptions(), error);
    REQUIRE(ping != nullptr);
    REQUIRE(Next(*ping, frame, error, 3) == OwnWebSocket::Read::Frame);          // "Hel": just fits
    REQUIRE(Next(*ping, frame, error, 0) == OwnWebSocket::Read::Frame);          // the ping, with no room at all
    CHECK(frame.opcode == OwnWebSocket::Ping);
    CHECK(Next(*ping, frame, error, 0) == OwnWebSocket::Read::TooBig);           // "l"
#endif
}

TEST(own_ws_idle_partial_and_closed) {
#if defined(_WIN32)
    SKIP("the scripted server is POSIX-only");
#else
    NEED_SERVER();
    std::string error;
    OwnWebSocket::Frame frame;
    // Nothing is said: Idle, within about the time asked.
    auto quiet = OwnWebSocket::Open(Url("/quiet"), UltraNetWebSocketOptions(), error);
    REQUIRE(quiet != nullptr);
    const auto before = std::chrono::steady_clock::now();
    CHECK(quiet->Receive(150, 1 << 20, frame, error) == OwnWebSocket::Read::Idle);
    const auto waited = std::chrono::steady_clock::now() - before;
    CHECK(waited >= std::chrono::milliseconds(100) && waited < std::chrono::seconds(2));
    // ... and it is still there afterwards.
    CHECK(quiet->Send(OwnWebSocket::Text, "still here", 10));
    REQUIRE(Next(*quiet, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(Text(frame) == "still here");

    // A frame that arrives a byte at a time: Partial until it is whole.
    auto split = OwnWebSocket::Open(Url("/split"), UltraNetWebSocketOptions(), error);
    REQUIRE(split != nullptr);
    int partial = 0;
    OwnWebSocket::Read r = OwnWebSocket::Read::Idle;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < until) {
        r = split->Receive(50, 1 << 20, frame, error);
        if (r == OwnWebSocket::Read::Partial) ++partial;
        else if (r != OwnWebSocket::Read::Idle) break;
    }
    CHECK(r == OwnWebSocket::Read::Frame && Text(frame) == "split across writes");
    CHECK(partial >= 2);

    // A close frame is a frame; a connection closed without one is Closed.
    auto closing = OwnWebSocket::Open(Url("/close-1001"), UltraNetWebSocketOptions(), error);
    REQUIRE(closing != nullptr);
    REQUIRE(Next(*closing, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(frame.opcode == OwnWebSocket::Close && frame.payload.size() == 12 && frame.payload[0] == 0x03 &&
          frame.payload[1] == 0xE9);
    auto gone = OwnWebSocket::Open(Url("/close-now"), UltraNetWebSocketOptions(), error);
    REQUIRE(gone != nullptr);
    CHECK(Next(*gone, frame, error) == OwnWebSocket::Read::Closed);
#endif
}

// ============================================================================
// WSS
// ============================================================================

TEST(own_ws_wss_verifies_the_server) {
#if defined(_WIN32)
    SKIP("the scripted server is POSIX-only");
#else
    NEED_SERVER();
    if (!g_haveTls) SKIP("no openssl program for the test's certificates");
    std::string error;
    // A certificate nobody vouches for.
    CHECK(OwnWebSocket::Open(TlsUrl("/echo"), UltraNetWebSocketOptions(), error) == nullptr);
    CHECK(error.find("not trusted") != std::string::npos || error.find("handshake") != std::string::npos);
    // Asked not to verify: the connection is made, and carries frames.
    UltraNetWebSocketOptions unverified;
    unverified.verifyTls = false;
    auto ws = OwnWebSocket::Open(TlsUrl("/echo"), unverified, error);
    REQUIRE(ws != nullptr);
    const std::vector<uint8_t> large = Pattern(200000);
    REQUIRE(ws->Send(OwnWebSocket::Binary, large.data(), large.size()));
    OwnWebSocket::Frame frame;
    REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(frame.opcode == OwnWebSocket::Binary && frame.payload == large);

    // The other certificate, once it is one UltraNet's TLS trusts: verified.
    CHECK(OwnWebSocket::Open(TlsUrl("/echo", true), UltraNetWebSocketOptions(), error) == nullptr);
    REQUIRE(bool(UltraNet_TlsAddTrustedCert(ReadFile(g_dir / "cert-b.pem"))));
    auto trusted = OwnWebSocket::Open(TlsUrl("/echo", true), UltraNetWebSocketOptions(), error);
    if (!trusted) ultranet_test::RecordFail(__FILE__, __LINE__, "the trusted certificate: " + error);
    REQUIRE(trusted != nullptr);
    REQUIRE(trusted->Send(OwnWebSocket::Text, "over verified TLS", 17));
    REQUIRE(Next(*trusted, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(Text(frame) == "over verified TLS");
    // The first one is no more trusted for that.
    CHECK(OwnWebSocket::Open(TlsUrl("/echo"), UltraNetWebSocketOptions(), error) == nullptr);
#endif
}

// ============================================================================
// THE PROXY SETTING
// ============================================================================

#if !defined(_WIN32)
namespace {
// The proxy setting for the time of a test: an HTTP proxy at the test's own.
struct ThroughTheTestProxy {
    UltraNetProxyConfig before = UltraNet_GetGlobalProxy();
    ThroughTheTestProxy() {
        UltraNetProxyConfig proxy;
        proxy.type = UltraNetProxyType::Http;
        proxy.host = "127.0.0.1";
        proxy.port = kProxyPort;
        UltraNet_SetGlobalProxy(proxy);
    }
    ~ThroughTheTestProxy() { UltraNet_SetGlobalProxy(before); }
};

// What the test's proxy was asked to connect to so far (asked of the
// scripted server directly: this machine is never reached through a proxy).
std::string ProxyLog() {
    std::string error;
    auto ws = OwnWebSocket::Open(Url("/proxy-log"), UltraNetWebSocketOptions(), error);
    if (!ws) return "no log: " + error;
    OwnWebSocket::Frame frame;
    if (Next(*ws, frame, error) != OwnWebSocket::Read::Frame) return "no log";
    return Text(frame);
}
} // namespace
#endif

TEST(own_ws_goes_through_the_proxy_setting) {
#if defined(_WIN32)
    SKIP("the scripted server is POSIX-only");
#else
    NEED_SERVER();
    const ThroughTheTestProxy setting;
    // A host that is not this machine - and is nowhere: only the proxy can
    // say where it is, so arriving at all is having gone through it.
    const std::string target = "relay.own.under-test.invalid:" + std::to_string(kPlainPort);
    std::string error;
    auto ws = OwnWebSocket::Open("ws://" + target + "/echo", UltraNetWebSocketOptions(), error);
    if (!ws) ultranet_test::RecordFail(__FILE__, __LINE__, "through the proxy: " + error);
    REQUIRE(ws != nullptr);
    REQUIRE(ws->Send(OwnWebSocket::Text, "through the tunnel", 18));
    OwnWebSocket::Frame frame;
    REQUIRE(Next(*ws, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(Text(frame) == "through the tunnel");
    // Asked for as a tunnel to exactly that host and port, once; the log
    // itself came without the proxy (it names no 127.0.0.1).
    const std::string log = ProxyLog();
    CHECK(log.find("\"" + target + "\"") != std::string::npos);
    CHECK(log.find("127.0.0.1") == std::string::npos);
    // The host in the upgrade is the server's, not the proxy's.
    auto headers = OwnWebSocket::Open("ws://" + target + "/headers", UltraNetWebSocketOptions(), error);
    REQUIRE(headers != nullptr);
    REQUIRE(Next(*headers, frame, error) == OwnWebSocket::Read::Frame);
    CHECK(Text(frame).find("\"host\": \"" + target + "\"") != std::string::npos);
#endif
}

// ============================================================================
// THROUGH UltraNet_WebSocket*, ON EITHER CLIENT
// ============================================================================

TEST(ws_goes_through_the_proxy_setting) {
#if defined(_WIN32)
    SKIP("the scripted server is POSIX-only");
#else
    NEED_SERVER();
    const ThroughTheTestProxy setting;
    std::mutex m;
    std::vector<std::string> texts;
    UltraNetWebSocketCallbacks cb;
    cb.onText = [&](UltraNetHandle, const std::string& t) {
        std::lock_guard<std::mutex> lk(m);
        texts.push_back(t);
    };
    const auto previous = UltraNet_WebSocketSetCallbacks(cb);
    const std::string target = "relay.either.under-test.invalid:" + std::to_string(kPlainPort);
    const UltraNetHandle h = UltraNet_WebSocketConnect("ws://" + target + "/echo");
    if (h == UltraNetInvalidHandle) {
        UltraNet_WebSocketSetCallbacks(previous);
        REQUIRE(!"a WebSocket connection through the test's proxy");
    }
    CHECK(bool(UltraNet_WebSocketSendText(h, "through the tunnel")));
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < until) {
        {
            std::lock_guard<std::mutex> lk(m);
            if (!texts.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    UltraNet_WebSocketClose(h);
    UltraNet_WebSocketSetCallbacks(previous);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0] == "through the tunnel");
    CHECK(ProxyLog().find("\"" + target + "\"") != std::string::npos);
#endif
}

TEST(ws_puts_a_message_together_from_its_fragments) {
#if defined(_WIN32)
    SKIP("the scripted server is POSIX-only");
#else
    NEED_SERVER();
    std::mutex m;
    std::vector<std::string> texts;
    std::vector<std::string> pings;
    UltraNetWebSocketCallbacks cb;
    cb.onText = [&](UltraNetHandle, const std::string& t) {
        std::lock_guard<std::mutex> lk(m);
        texts.push_back(t);
    };
    cb.onPing = [&](UltraNetHandle, const std::vector<uint8_t>& p) {
        std::lock_guard<std::mutex> lk(m);
        pings.emplace_back(p.begin(), p.end());
    };
    const auto previous = UltraNet_WebSocketSetCallbacks(cb);
    const UltraNetHandle h = UltraNet_WebSocketConnect(Url("/fragments"));
    if (h == UltraNetInvalidHandle) {
        UltraNet_WebSocketSetCallbacks(previous);
        REQUIRE(!"a WebSocket connection to the test's own server");
    }
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < until) {
        {
            std::lock_guard<std::mutex> lk(m);
            if (!texts.empty() && !pings.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    UltraNet_WebSocketClose(h);
    UltraNet_WebSocketSetCallbacks(previous);
    // One message, whole - the ping in its middle told of, and not part of it.
    REQUIRE(texts.size() == 1);
    CHECK(texts[0] == "Hello");
    REQUIRE(pings.size() == 1);
    CHECK(pings[0] == "p");
#endif
}

TEST(ws_wss_carries_messages_and_refuses_an_unknown_certificate) {
#if defined(_WIN32)
    SKIP("the scripted server is POSIX-only");
#else
    NEED_SERVER();
    if (!g_haveTls) SKIP("no openssl program for the test's certificates");
    std::mutex m;
    std::vector<std::string> texts;
    std::vector<std::vector<uint8_t>> binaries;
    std::atomic<int> errors{0};
    UltraNetWebSocketCallbacks cb;
    cb.onText = [&](UltraNetHandle, const std::string& t) {
        std::lock_guard<std::mutex> lk(m);
        texts.push_back(t);
    };
    cb.onBinary = [&](UltraNetHandle, const std::vector<uint8_t>& b) {
        std::lock_guard<std::mutex> lk(m);
        binaries.push_back(b);
    };
    cb.onError = [&](UltraNetHandle, const std::string&) { ++errors; };
    const auto previous = UltraNet_WebSocketSetCallbacks(cb);

    // Verified, as it is unless asked otherwise: no connection, and told why.
    CHECK(UltraNet_WebSocketConnect(TlsUrl("/echo")) == UltraNetInvalidHandle);
    CHECK(errors.load() == 1);

    UltraNetWebSocketOptions unverified;
    unverified.verifyTls = false;
    const UltraNetHandle h = UltraNet_WebSocketConnect(TlsUrl("/echo"), unverified);
    if (h == UltraNetInvalidHandle) {
        UltraNet_WebSocketSetCallbacks(previous);
        REQUIRE(!"a wss connection to the test's own server");
    }
    const std::vector<uint8_t> large = Pattern(150000);
    CHECK(bool(UltraNet_WebSocketSendText(h, "over TLS")));
    CHECK(bool(UltraNet_WebSocketSendBinary(h, large)));
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < until) {
        {
            std::lock_guard<std::mutex> lk(m);
            if (!texts.empty() && !binaries.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(bool(UltraNet_WebSocketClose(h, 1000, "done")));
    UltraNet_WebSocketSetCallbacks(previous);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0] == "over TLS");
    REQUIRE(binaries.size() == 1);
    CHECK(binaries[0] == large);
#endif
}
