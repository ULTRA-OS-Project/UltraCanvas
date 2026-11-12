// Tests/UltraNet/test_extra_plugins.cpp
// Registration / interface sanity tests for the four libcurl-native
// "quick-win" plug-ins added on top of the mail trio:
//   - WebDAV   (IFileShareProtocolPlugin)
//   - LDAP     (IDirectoryProtocolPlugin)
//   - Telnet   (IRemoteAccessPlugin)
//   - RTSP     (IStreamingProtocolPlugin)
//
// Live-server testing for each protocol needs real services and is out
// of scope for CI. We verify each plug-in compiles standalone as a DSO,
// loads via UltraNet_RefreshPlugins, registers for the expected schemes,
// and rejects malformed input cleanly.
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetSsh.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <cctype>
#include <cstdio>
#include <cstring>

#if !defined(_WIN32)
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

// Build (or locate a pre-built) plug-in DSO for `name`. Same pattern as
// the SMTP / IMAP / POP3 tests — checks an env var, then a CMake-supplied
// path, then compiles from source via g++ as a last resort.
fs::path FindOrBuild(const std::string& name) {
    std::string envName = "ULTRANET_" + name + "_PLUGIN_PATH";
    for (auto& c : envName) c = static_cast<char>(std::toupper(c));
    if (const char* p = std::getenv(envName.c_str())) {
        if (fs::exists(p)) return p;
    }
#ifdef ULTRANET_WEBDAV_PLUGIN_PATH_DEFINE
    if (name == "WEBDAV" && fs::exists(ULTRANET_WEBDAV_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_WEBDAV_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_LDAP_PLUGIN_PATH_DEFINE
    if (name == "LDAP" && fs::exists(ULTRANET_LDAP_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_LDAP_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_TELNET_PLUGIN_PATH_DEFINE
    if (name == "TELNET" && fs::exists(ULTRANET_TELNET_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_TELNET_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_RTSP_PLUGIN_PATH_DEFINE
    if (name == "RTSP" && fs::exists(ULTRANET_RTSP_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_RTSP_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_MQTT_PLUGIN_PATH_DEFINE
    if (name == "MQTT" && fs::exists(ULTRANET_MQTT_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_MQTT_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_AMQP_PLUGIN_PATH_DEFINE
    if (name == "AMQP" && fs::exists(ULTRANET_AMQP_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_AMQP_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_COAP_PLUGIN_PATH_DEFINE
    if (name == "COAP" && fs::exists(ULTRANET_COAP_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_COAP_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_SNMP_PLUGIN_PATH_DEFINE
    if (name == "SNMP" && fs::exists(ULTRANET_SNMP_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_SNMP_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_SSH_PLUGIN_PATH_DEFINE
    if (name == "SSH" && fs::exists(ULTRANET_SSH_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_SSH_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_MDNS_PLUGIN_PATH_DEFINE
    if (name == "MDNS" && fs::exists(ULTRANET_MDNS_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_MDNS_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_RTMP_PLUGIN_PATH_DEFINE
    if (name == "RTMP" && fs::exists(ULTRANET_RTMP_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_RTMP_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_GRPC_PLUGIN_PATH_DEFINE
    if (name == "GRPC" && fs::exists(ULTRANET_GRPC_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_GRPC_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_SIP_PLUGIN_PATH_DEFINE
    if (name == "SIP" && fs::exists(ULTRANET_SIP_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_SIP_PLUGIN_PATH_DEFINE};
#endif
#ifdef ULTRANET_RTP_PLUGIN_PATH_DEFINE
    if (name == "RTP" && fs::exists(ULTRANET_RTP_PLUGIN_PATH_DEFINE))
        return fs::path{ULTRANET_RTP_PLUGIN_PATH_DEFINE};
#endif

    // Map test name -> source/class name pair.
    std::string lower = name, cls = name;
    for (auto& c : lower) c = static_cast<char>(std::tolower(c));
    if      (name == "WEBDAV") cls = "WebDav";
    else if (name == "LDAP")   cls = "Ldap";
    else if (name == "TELNET") cls = "Telnet";
    else if (name == "RTSP")   cls = "Rtsp";
    else if (name == "MQTT")   cls = "Mqtt";
    else if (name == "AMQP")   cls = "Amqp";
    else if (name == "COAP")   cls = "Coap";
    else if (name == "SNMP")   cls = "Snmp";
    else if (name == "SSH")    cls = "Ssh";
    else if (name == "MDNS")   cls = "Mdns";
    else if (name == "RTMP")   cls = "Rtmp";
    else if (name == "GRPC")   cls = "Grpc";
    else if (name == "SIP")    cls = "Sip";
    else if (name == "RTP")    cls = "Rtp";

    const fs::path src = fs::path{"UltraCanvas/Plugins/UltraNet"} / lower /
                         (cls + "Plugin.cpp");
    if (!fs::exists(src)) return {};
    if (std::system("command -v g++ > /dev/null 2>&1") != 0) return {};

    const fs::path outDir =
        fs::temp_directory_path() / ("ultranet_" + lower + "_test");
    fs::remove_all(outDir);
    fs::create_directories(outDir);
    const fs::path outFile = outDir / ("ultranet_" + lower + ".so");

    // Each plug-in links a different backend library. Pick the right
    // link flags per plug-in.
    std::string extraLinkFlags;
    if      (name == "MQTT") extraLinkFlags = " -lpaho-mqtt3as";
    else if (name == "AMQP") extraLinkFlags = " -lrabbitmq";
    else if (name == "COAP") extraLinkFlags = " -lcoap-3-openssl";
    else if (name == "SNMP") extraLinkFlags = " -lsnmp";
    else if (name == "SSH")  extraLinkFlags = " -lssh";
    else if (name == "MDNS") extraLinkFlags = " -lavahi-client -lavahi-common";
    else if (name == "SIP")  extraLinkFlags = "";   // BSD sockets only, no extra libs
    else if (name == "RTP")  extraLinkFlags = "";   // BSD sockets only, no extra libs
    else                     extraLinkFlags = " $(pkg-config --cflags --libs libcurl)";

    // As the plug-in's CMake target builds it: with the host shim
    // (UltraNetPluginHostShim.cpp - every core function the plug-in calls
    // comes through the host's table), the shared plug-in headers, and every
    // symbol hidden but the entry point (ULTRANET_PLUGIN_EXPORT), so the
    // shim never stands in for the host's own functions. The compiler's
    // output goes to a log beside the library: a backend library missing
    // here (Avahi, paho, ...) is a skip, not a page of errors in the test
    // output.
    const fs::path log = outDir / "build.log";
    const std::string cmd =
        "g++ -std=c++20 -fPIC -shared -fvisibility=hidden "
        "-I UltraCanvas/include -I UltraCanvas/Plugins/UltraNet/common "
        + src.string() + " UltraCanvas/Plugins/UltraNet/common/UltraNetPluginHostShim.cpp" + extraLinkFlags +
        " -o " + outFile.string() + " > " + log.string() + " 2>&1";
    if (std::system(cmd.c_str()) != 0) {
        std::fprintf(stderr, "    (%s plug-in not built here - %s)\n", name.c_str(), log.string().c_str());
        return {};
    }
    return fs::exists(outFile) ? outFile : fs::path{};
}

bool LoadInto(const std::string& name, const std::string& expectedScheme) {
    const fs::path p = FindOrBuild(name);
    if (p.empty()) return false;
    UltraNet_SetPluginDirectory(p.parent_path().string());
    UltraNet_RefreshPlugins();
    auto plugin = UltraNet_GetPlugin(expectedScheme);
    return plugin != nullptr;
}

} // namespace

// ===== WebDAV =====
TEST(webdav_plugin_loads_and_registers) {
    UltraNet_Initialize();
    if (!LoadInto("WEBDAV", "webdav")) SKIP("WebDAV plug-in not buildable");

    auto p = UltraNet_GetPlugin("webdav");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-WebDAV"});
    auto schemes = p->GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "webdav")  != schemes.end());
    CHECK(std::find(schemes.begin(), schemes.end(), "webdavs") != schemes.end());
    auto* dav = dynamic_cast<IFileShareProtocolPlugin*>(p.get());
    REQUIRE(dav != nullptr);
}

// ===== LDAP =====
TEST(ldap_plugin_loads_and_registers) {
    if (!LoadInto("LDAP", "ldap")) SKIP("LDAP plug-in not buildable");

    auto p = UltraNet_GetPlugin("ldap");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-LDAP"});
    auto schemes = p->GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "ldap")  != schemes.end());
    CHECK(std::find(schemes.begin(), schemes.end(), "ldaps") != schemes.end());
    auto* dir = dynamic_cast<IDirectoryProtocolPlugin*>(p.get());
    REQUIRE(dir != nullptr);
}

// ===== Telnet =====
TEST(telnet_plugin_loads_and_registers) {
    if (!LoadInto("TELNET", "telnet")) SKIP("Telnet plug-in not buildable");

    auto p = UltraNet_GetPlugin("telnet");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-Telnet"});
    auto* shell = dynamic_cast<IRemoteAccessPlugin*>(p.get());
    REQUIRE(shell != nullptr);

    // Bad URL -> InvalidHandle, no crash.
    UltraNetRemoteOptions opt;
    UltraNetHandle h = shell->OpenShell("not-a-telnet-url", opt);
    REQUIRE_EQ(h, UltraNetInvalidHandle);

    // ExecuteCommand on bogus handle -> InvalidHandle result.
    std::string out, err; int exitCode = 0;
    auto r = shell->ExecuteCommand(99999u, "ls", out, err, exitCode);
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidHandle);
}

// ===== RTSP =====
TEST(rtsp_plugin_loads_and_registers) {
    if (!LoadInto("RTSP", "rtsp")) SKIP("RTSP plug-in not buildable");

    auto p = UltraNet_GetPlugin("rtsp");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-RTSP"});
    auto* stream = dynamic_cast<IStreamingProtocolPlugin*>(p.get());
    REQUIRE(stream != nullptr);

    // ReadFrame on a fresh / non-existent handle: must report
    // UnsupportedScheme (the documented "control-only" limitation) or
    // InvalidHandle, never empty success.
    std::vector<uint8_t> frame;
    auto r = stream->ReadFrame(99999u, frame);
    CHECK(!bool(r));
}

// ===== MQTT =====
TEST(mqtt_plugin_loads_and_registers) {
    if (!LoadInto("MQTT", "mqtt")) SKIP("MQTT plug-in not buildable (paho-mqtt-c missing?)");

    auto p = UltraNet_GetPlugin("mqtt");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-MQTT"});
    auto schemes = p->GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "mqtt")  != schemes.end());
    CHECK(std::find(schemes.begin(), schemes.end(), "mqtts") != schemes.end());
    auto* msg = dynamic_cast<IMessagingProtocolPlugin*>(p.get());
    REQUIRE(msg != nullptr);

    // Publish on a bogus session handle -> InvalidHandle, no crash.
    auto r = msg->Publish(99999u, "test/topic", {1, 2, 3});
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidHandle);
}

// ===== AMQP =====
TEST(amqp_plugin_loads_and_registers) {
    if (!LoadInto("AMQP", "amqp")) SKIP("AMQP plug-in not buildable (rabbitmq-c missing?)");

    auto p = UltraNet_GetPlugin("amqp");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-AMQP"});
    auto schemes = p->GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "amqp")  != schemes.end());
    CHECK(std::find(schemes.begin(), schemes.end(), "amqps") != schemes.end());
    auto* msg = dynamic_cast<IMessagingProtocolPlugin*>(p.get());
    REQUIRE(msg != nullptr);

    auto r = msg->Publish(99999u, "queue", {1});
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidHandle);
}

// ===== CoAP =====
TEST(coap_plugin_loads_and_registers) {
    if (!LoadInto("COAP", "coap")) SKIP("CoAP plug-in not buildable (libcoap3 missing?)");

    auto p = UltraNet_GetPlugin("coap");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-CoAP"});
    auto schemes = p->GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "coap")  != schemes.end());
    CHECK(std::find(schemes.begin(), schemes.end(), "coaps") != schemes.end());
    auto* msg = dynamic_cast<IMessagingProtocolPlugin*>(p.get());
    REQUIRE(msg != nullptr);

    // Bad URL -> InvalidHandle, no crash.
    UltraNetMessagingOptions opt;
    REQUIRE_EQ(msg->Connect("http://not-coap/", opt), UltraNetInvalidHandle);
}

// Shutdown with a session still open must stop its worker before the session
// and the library go. It used to drop the table only: the worker kept running
// coap_io_process through coap_cleanup. CoAP is UDP, so no server is needed.
TEST(coap_plugin_shutdown_stops_open_sessions) {
    if (!LoadInto("COAP", "coap")) SKIP("CoAP plug-in not buildable (libcoap3 missing?)");
    auto p = UltraNet_GetPlugin("coap");
    REQUIRE(p != nullptr);
    auto* msg = dynamic_cast<IMessagingProtocolPlugin*>(p.get());
    REQUIRE(msg != nullptr);
    REQUIRE(bool(msg->Initialize(UltraNet_GetConfig())));

    // Linux: the process's thread count, to see the worker go away.
    auto threadCount = []() -> long {
        std::error_code ec;
        long n = 0;
        for (auto it = fs::directory_iterator("/proc/self/task", ec);
             !ec && it != fs::directory_iterator(); it.increment(ec)) ++n;
        return ec ? -1 : n;
    };
    const long threadsBefore = threadCount();

    UltraNetMessagingOptions opt;
    const UltraNetHandle h = msg->Connect("coap://127.0.0.1:5683", opt);
    REQUIRE(h != UltraNetInvalidHandle);
    // An Observe GET nobody answers: the worker has retransmissions to run.
    CHECK(bool(msg->Subscribe(h, "sensors/temp",
                              [](const std::string&, const std::vector<uint8_t>&) {})));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    msg->Shutdown();
    REQUIRE_EQ(msg->Publish(h, "sensors/temp", {1}).code, UltraNetResultCode::InvalidHandle);
    // The worker is joined, not left running on a cleaned-up libcoap.
    if (threadsBefore > 0) REQUIRE(threadCount() <= threadsBefore);
}

// ===== SNMP =====
TEST(snmp_plugin_loads_and_registers) {
    if (!LoadInto("SNMP", "snmp")) SKIP("SNMP plug-in not buildable (net-snmp missing?)");

    auto p = UltraNet_GetPlugin("snmp");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-SNMP"});
    auto schemes = p->GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "snmp") != schemes.end());
    auto* dir = dynamic_cast<IDirectoryProtocolPlugin*>(p.get());
    REQUIRE(dir != nullptr);

    // Non-snmp URL -> InvalidUrl.
    UltraNetDirectoryQuery q;
    std::vector<UltraNetDirectoryEntry> e;
    auto r = dir->Search("http://wrong-scheme/", q, e);
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidUrl);
}

// ===== SSH =====
TEST(ssh_plugin_loads_and_registers) {
    if (!LoadInto("SSH", "ssh")) SKIP("SSH plug-in not buildable (libssh missing?)");

    auto p = UltraNet_GetPlugin("ssh");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-SSH"});
    auto* sh = dynamic_cast<IRemoteAccessPlugin*>(p.get());
    REQUIRE(sh != nullptr);

    // Bad URL -> InvalidHandle, no crash.
    UltraNetRemoteOptions opt;
    REQUIRE_EQ(sh->OpenShell("not-an-ssh-url", opt), UltraNetInvalidHandle);
}

// ===== SSH tunnels =====
// Against a real sshd: ULTRANET_TEST_SSH_URL (ssh://user@host:port) and
// ULTRANET_TEST_SSH_KEY (the private key file that signs in there). Without
// them the tests skip - unless ULTRANET_TEST_SSH_REQUIRED is set, as in CI,
// where a missing server is a failure. The tunnel's far end is an echo
// server of the test's own on 127.0.0.1, which the sshd reaches as the test
// does.
#if !defined(_WIN32)
namespace {

struct SshServer {
    std::string url;
    std::vector<uint8_t> key;
};

bool SshServerFromEnv(SshServer& out, bool& required) {
    required = std::getenv("ULTRANET_TEST_SSH_REQUIRED") != nullptr;
    const char* url = std::getenv("ULTRANET_TEST_SSH_URL");
    const char* key = std::getenv("ULTRANET_TEST_SSH_KEY");
    if (!url || !*url || !key || !*key) return false;
    std::FILE* f = std::fopen(key, "rb");
    if (!f) return false;
    char buffer[4096];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) out.key.insert(out.key.end(), buffer, buffer + n);
    std::fclose(f);
    out.url = url;
    return !out.key.empty();
}

// Echoes every byte back on each connection, until stopped.
class EchoServer {
public:
    EchoServer() {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t length = sizeof addr;
        bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
        listen(listener_, 8);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &length);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] {
            for (;;) {
                const int client = accept(listener_, nullptr, nullptr);
                if (client < 0) return;
                std::thread([client] {
                    char buffer[16384];
                    for (;;) {
                        const ssize_t n = recv(client, buffer, sizeof buffer, 0);
                        if (n <= 0) break;
                        ssize_t sent = 0;
                        while (sent < n) {
                            const ssize_t m = send(client, buffer + sent, static_cast<size_t>(n - sent), 0);
                            if (m <= 0) break;
                            sent += m;
                        }
                    }
                    close(client);
                }).detach();
            }
        });
    }
    ~EchoServer() {
        shutdown(listener_, SHUT_RDWR);
        close(listener_);
        thread_.join();
    }
    int Port() const { return port_; }

private:
    int listener_ = -1;
    int port_ = 0;
    std::thread thread_;
};

// Connect to 127.0.0.1:port, send `data`, read as many bytes back.
std::string RoundTrip(int port, const std::string& data) {
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        close(s);
        return "(no connection)";
    }
    std::string back;
    std::thread writer([&] {
        size_t sent = 0;
        while (sent < data.size()) {
            const ssize_t n = send(s, data.data() + sent, data.size() - sent, 0);
            if (n <= 0) break;
            sent += static_cast<size_t>(n);
        }
    });
    char buffer[16384];
    while (back.size() < data.size()) {
        const ssize_t n = recv(s, buffer, sizeof buffer, 0);
        if (n <= 0) break;
        back.append(buffer, static_cast<size_t>(n));
    }
    writer.join();
    close(s);
    return back;
}

// A file with nothing in it, for a known_hosts that knows no host.
std::string EmptyKnownHosts() {
    const fs::path path = fs::temp_directory_path() / "ultranet_ssh_test_known_hosts";
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    if (f) std::fclose(f);
    return path.string();
}

} // namespace

TEST(ssh_tunnel_checks_the_host_key_and_forwards) {
    SshServer server;
    bool required = false;
    if (!SshServerFromEnv(server, required)) {
        REQUIRE(!required);
        SKIP("no SSH server configured (ULTRANET_TEST_SSH_URL, ULTRANET_TEST_SSH_KEY)");
    }
    REQUIRE(LoadInto("SSH", "ssh"));
    EchoServer echo;
    UltraNetSshTunnelOptions options;
    options.url = server.url;
    options.remote.privateKeyPem = server.key;
    options.remote.knownHostsFile = EmptyKnownHosts();
    options.targetHost = "127.0.0.1";
    options.targetPort = echo.Port();

    // A host nobody has seen: refused, its fingerprint handed back.
    UltraNetHandle handle = UltraNetInvalidHandle;
    UltraNetSshTunnelInfo info;
    UltraNetResult r = UltraNet_OpenSshTunnel(options, handle, info);
    REQUIRE_EQ(r.code, UltraNetResultCode::SshHostKeyUnknown);
    REQUIRE(info.hostKeyFingerprint.rfind("SHA256:", 0) == 0);
    REQUIRE_EQ(handle, UltraNetInvalidHandle);
    const std::string fingerprint = info.hostKeyFingerprint;

    // Another key than the one trusted: refused.
    options.remote.hostKeyFingerprint = "SHA256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    r = UltraNet_OpenSshTunnel(options, handle, info);
    REQUIRE_EQ(r.code, UltraNetResultCode::SshHostKeyChanged);

    // A key that does not sign in: refused.
    options.remote.hostKeyFingerprint = fingerprint;
    UltraNetSshTunnelOptions wrongKey = options;
    wrongKey.remote.privateKeyPem.clear();
    wrongKey.remote.credentials.password = "not-the-password";
    r = UltraNet_OpenSshTunnel(wrongKey, handle, info);
    REQUIRE_EQ(r.code, UltraNetResultCode::AuthenticationFailed);

    // The key trusted: open, and every connection carried to the far end.
    r = UltraNet_OpenSshTunnel(options, handle, info);
    if (!r) std::fprintf(stderr, "\n    %s", r.message.c_str());
    REQUIRE(r.success);
    REQUIRE(handle != UltraNetInvalidHandle);
    REQUIRE(info.localPort > 0);
    REQUIRE_EQ(info.hostKeyFingerprint, fingerprint);
    REQUIRE_EQ(RoundTrip(info.localPort, "hello through the bastion"), std::string("hello through the bastion"));

    // Three at once, each with half a megabyte both ways.
    std::string big(512 * 1024, '\0');
    for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>((i * 7919u) >> 3);
    std::string results[3];
    std::thread a([&] { results[0] = RoundTrip(info.localPort, big); });
    std::thread b([&] { results[1] = RoundTrip(info.localPort, big); });
    std::thread c([&] { results[2] = RoundTrip(info.localPort, big); });
    a.join();
    b.join();
    c.join();
    for (const auto& result : results) REQUIRE(result == big);

    // Closed: the port takes no more connections.
    REQUIRE(UltraNet_CloseSshTunnel(handle).success);
    REQUIRE_EQ(RoundTrip(info.localPort, "x"), std::string("(no connection)"));
    REQUIRE_EQ(UltraNet_CloseSshTunnel(handle).code, UltraNetResultCode::InvalidHandle);
}

TEST(ssh_tunnel_to_a_closed_port_ends_the_connection) {
    SshServer server;
    bool required = false;
    if (!SshServerFromEnv(server, required)) {
        REQUIRE(!required);
        SKIP("no SSH server configured");
    }
    REQUIRE(LoadInto("SSH", "ssh"));
    int closedPort = 0;
    {
        EchoServer gone;   // a port that was free a moment ago
        closedPort = gone.Port();
    }
    UltraNetSshTunnelOptions options;
    options.url = server.url;
    options.remote.privateKeyPem = server.key;
    options.remote.knownHostsFile = EmptyKnownHosts();
    options.targetHost = "127.0.0.1";
    options.targetPort = closedPort;
    UltraNetHandle handle = UltraNetInvalidHandle;
    UltraNetSshTunnelInfo info;
    UltraNet_OpenSshTunnel(options, handle, info);           // learns the fingerprint
    options.remote.hostKeyFingerprint = info.hostKeyFingerprint;
    REQUIRE(UltraNet_OpenSshTunnel(options, handle, info).success);
    // The SSH server cannot reach the far end: the client sees its
    // connection end, no bytes, and the tunnel stays up for the next.
    REQUIRE_EQ(RoundTrip(info.localPort, "anyone there?"), std::string());
    REQUIRE(UltraNet_CloseSshTunnel(handle).success);
}
#endif

// ===== mDNS =====
TEST(mdns_plugin_loads_and_registers) {
    if (!LoadInto("MDNS", "mdns")) SKIP("mDNS plug-in not buildable");

    auto p = UltraNet_GetPlugin("mdns");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-mDNS"});
    auto schemes = p->GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "mdns")  != schemes.end());
    CHECK(std::find(schemes.begin(), schemes.end(), "dns-sd") != schemes.end());
    auto* dir = dynamic_cast<IDirectoryProtocolPlugin*>(p.get());
    REQUIRE(dir != nullptr);

    // Missing baseDn (service type) -> InvalidUrl.
    UltraNetDirectoryQuery q;
    std::vector<UltraNetDirectoryEntry> e;
    auto r = dir->Search("mdns://", q, e);
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidUrl);
}

// ===== gRPC =====
TEST(grpc_plugin_loads_and_registers) {
    if (!LoadInto("GRPC", "grpc")) SKIP("gRPC plug-in not buildable");

    auto p = UltraNet_GetPlugin("grpc");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-gRPC"});
    auto schemes = p->GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "grpc")  != schemes.end());
    CHECK(std::find(schemes.begin(), schemes.end(), "grpcs") != schemes.end());
    auto* rpc = dynamic_cast<IRpcProtocolPlugin*>(p.get());
    REQUIRE(rpc != nullptr);

    // UnaryCall on a bogus handle -> InvalidHandle, no crash.
    UltraNetRpcOptions opt;
    std::vector<uint8_t> response;
    auto r = rpc->UnaryCall(99999u, "svc", "Method", {1, 2, 3}, response, opt);
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidHandle);

    // Bogus URL on Connect -> InvalidHandle.
    REQUIRE_EQ(rpc->Connect("not-a-grpc-url", opt), UltraNetInvalidHandle);
}

// ===== SIP =====
TEST(sip_plugin_loads_and_registers) {
    if (!LoadInto("SIP", "sip")) SKIP("SIP plug-in not buildable");

    auto p = UltraNet_GetPlugin("sip");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-SIP"});
    auto schemes = p->GetSupportedSchemes();
    CHECK(std::find(schemes.begin(), schemes.end(), "sip") != schemes.end());
    auto* sh = dynamic_cast<IRemoteAccessPlugin*>(p.get());
    REQUIRE(sh != nullptr);

    // Bad URL -> InvalidHandle (parser rejects non-sip:// scheme).
    UltraNetRemoteOptions opt;
    REQUIRE_EQ(sh->OpenShell("http://not-sip/", opt), UltraNetInvalidHandle);

    // ExecuteCommand on bogus handle -> InvalidHandle result.
    std::string out, err; int exitCode = 0;
    auto r = sh->ExecuteCommand(99999u, "hello", out, err, exitCode);
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidHandle);
}

// ===== RTP (real loopback test) =====
// The RTP plug-in has no external dependency, so we can actually shove a
// synthetic RTP packet at it over UDP and verify the header parser +
// ReadFrame path end-to-end.
TEST(rtp_plugin_loopback_receives_payload) {
    if (!LoadInto("RTP", "rtp")) SKIP("RTP plug-in not buildable");

    auto p = UltraNet_GetPlugin("rtp");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-RTP"});
    auto* stream = dynamic_cast<IStreamingProtocolPlugin*>(p.get());
    REQUIRE(stream != nullptr);

#if defined(_WIN32) || defined(_WIN64)
    SKIP("RTP loopback socket send not wired for Windows in this test");
#else
    // Pick an ephemeral-ish port well above the well-known range so
    // parallel test runs don't collide with each other.
    const int port = 47654;
    const std::string url = "rtp://127.0.0.1:" + std::to_string(port);

    UltraNetStreamOptions opt;
    opt.connectTimeoutMs = 500;
    UltraNetHandle h = stream->OpenStream(url, opt);
    REQUIRE(h != UltraNetInvalidHandle);

    // Build a synthetic RTP v2 packet: version=2, PT=96, seq=42, ts=1234,
    // ssrc=0xdeadbeef, payload = "hello".
    const uint8_t packet[] = {
        0x80, 0x60, 0x00, 0x2a,                 // V=2 P=0 X=0 CC=0 | M=0 PT=96 | seq=42
        0x00, 0x00, 0x04, 0xd2,                 // ts = 1234
        0xde, 0xad, 0xbe, 0xef,                 // ssrc = 0xdeadbeef
        'h', 'e', 'l', 'l', 'o'                 // payload
    };

    int sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    REQUIRE(sock >= 0);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(static_cast<uint16_t>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);
    const ssize_t sent = ::sendto(sock, packet, sizeof packet, 0,
                                   reinterpret_cast<sockaddr*>(&dst),
                                   sizeof dst);
    ::close(sock);
    REQUIRE_EQ(sent, static_cast<ssize_t>(sizeof packet));

    std::vector<uint8_t> frame;
    auto r = stream->ReadFrame(h, frame);
    REQUIRE(bool(r));
    REQUIRE_EQ(frame.size(), static_cast<std::size_t>(5));
    REQUIRE_EQ(frame[0], 'h');
    REQUIRE_EQ(frame[1], 'e');
    REQUIRE_EQ(frame[2], 'l');
    REQUIRE_EQ(frame[3], 'l');
    REQUIRE_EQ(frame[4], 'o');
#endif
}

TEST(rtp_plugin_rejects_bad_url) {
    if (!LoadInto("RTP", "rtp")) SKIP("RTP plug-in not buildable");
    auto p = UltraNet_GetPlugin("rtp");
    REQUIRE(p != nullptr);
    auto* stream = dynamic_cast<IStreamingProtocolPlugin*>(p.get());
    REQUIRE(stream != nullptr);

    UltraNetStreamOptions opt;
    REQUIRE_EQ(stream->OpenStream("not-an-rtp-url", opt), UltraNetInvalidHandle);

    // ReadFrame on bogus handle -> InvalidHandle.
    std::vector<uint8_t> frame;
    auto r = stream->ReadFrame(99999u, frame);
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidHandle);
}

// ===== RTMP =====
TEST(rtmp_plugin_loads_and_registers) {
    if (!LoadInto("RTMP", "rtmp")) SKIP("RTMP plug-in not buildable");

    auto p = UltraNet_GetPlugin("rtmp");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->GetName(), std::string{"UltraNet-RTMP"});
    auto* stream = dynamic_cast<IStreamingProtocolPlugin*>(p.get());
    REQUIRE(stream != nullptr);

    // ReadFrame on bogus handle -> InvalidHandle, no crash.
    std::vector<uint8_t> frame;
    auto r = stream->ReadFrame(99999u, frame);
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidHandle);
}

// A stream played from an RTMP server of the test's own on 127.0.0.1: the
// handshake, connect (app and tcUrl), createStream and play (the play path
// with its query), then a 300-byte video message in 128-byte chunks (one
// full header, two continuation chunks), an audio message under a delta
// header, and a ping. The plug-in gives the stream back as FLV and answers
// the ping.
#if !defined(_WIN32)
namespace {

void PutBe(std::string& out, uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i) out += static_cast<char>((v >> (8 * i)) & 0xFF);
}

std::string AmfStr(const std::string& s) {
    std::string out(1, '\x02');
    PutBe(out, static_cast<uint32_t>(s.size()), 2);
    return out + s;
}

std::string AmfNum(double d) {
    uint64_t bits = 0;
    std::memcpy(&bits, &d, sizeof bits);
    std::string out(1, '\x00');
    for (int i = 7; i >= 0; --i) out += static_cast<char>((bits >> (8 * i)) & 0xFF);
    return out;
}

std::string AmfStatus(const std::string& code) {
    std::string out(1, '\x03');
    PutBe(out, 5, 2); out += "level"; out += AmfStr("status");
    PutBe(out, 4, 2); out += "code"; out += AmfStr(code);
    out += std::string("\x00\x00\x09", 3);
    return out;
}

// The string value of `key` in an AMF0 object somewhere in `body`.
std::string AmfProp(const std::string& body, const std::string& key) {
    std::string marker;
    PutBe(marker, static_cast<uint32_t>(key.size()), 2);
    marker += key;
    marker += '\x02';
    const std::size_t at = body.find(marker);
    if (at == std::string::npos) return {};
    const std::size_t len = (static_cast<unsigned char>(body[at + marker.size()]) << 8) |
                            static_cast<unsigned char>(body[at + marker.size() + 1]);
    return body.substr(at + marker.size() + 2, len);
}

class FakeRtmpServer {
public:
    std::string app, tcUrl, playPath;
    bool pongSeen = false;
    int port = 0;

    bool Start() {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t length = sizeof addr;
        if (bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) return false;
        listen(listener_, 1);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &length);
        port = ntohs(addr.sin_port);
        thread_ = std::thread([this] {
            const int c = accept(listener_, nullptr, nullptr);
            if (c < 0) return;
            timeval tv{5, 0};
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            Serve(c);
            close(c);
        });
        return true;
    }
    void Join() { if (thread_.joinable()) thread_.join(); }
    ~FakeRtmpServer() {
        shutdown(listener_, SHUT_RDWR);
        Join();
        close(listener_);
    }

private:
    int listener_ = -1;
    std::thread thread_;

    static std::string Read(int c, std::size_t n) {
        std::string out;
        char buf[4096];
        while (out.size() < n) {
            const ssize_t got = recv(c, buf, std::min(sizeof buf, n - out.size()), 0);
            if (got <= 0) break;
            out.append(buf, static_cast<std::size_t>(got));
        }
        return out;
    }
    static void Send(int c, const std::string& s) { send(c, s.data(), s.size(), MSG_NOSIGNAL); }

    // A message in chunks of 128: a full header, then continuation headers.
    static std::string Chunked(int csid, int type, uint32_t streamId, uint32_t ts, const std::string& payload) {
        std::string out;
        std::size_t pos = 0;
        do {
            if (pos == 0) {
                out += static_cast<char>(csid);
                PutBe(out, ts, 3);
                PutBe(out, static_cast<uint32_t>(payload.size()), 3);
                out += static_cast<char>(type);
                for (int i = 0; i < 4; ++i) out += static_cast<char>((streamId >> (8 * i)) & 0xFF);
            } else {
                out += static_cast<char>(0xC0 | csid);
            }
            const std::size_t take = std::min<std::size_t>(128, payload.size() - pos);
            out += payload.substr(pos, take);
            pos += take;
        } while (pos < payload.size());
        return out;
    }

    void Serve(int c) {
        const std::string c01 = Read(c, 1537);
        if (c01.size() != 1537 || c01[0] != '\x03') return;
        Send(c, std::string(1, '\x03') + std::string(1536, '\x01') + c01.substr(1));   // S0 S1 S2
        if (Read(c, 1536).size() != 1536) return;                                     // C2
        for (;;) {
            // The client sends every message in one chunk with a full header.
            const std::string head = Read(c, 12);
            if (head.size() != 12 || (static_cast<unsigned char>(head[0]) >> 6) != 0) return;
            const uint32_t len = (static_cast<unsigned char>(head[4]) << 16) |
                                 (static_cast<unsigned char>(head[5]) << 8) | static_cast<unsigned char>(head[6]);
            const int type = static_cast<unsigned char>(head[7]);
            const std::string body = Read(c, len);
            if (type == 4 && body.size() >= 2 && body[1] == '\x07') {   // our ping answered
                pongSeen = true;
                return;
            }
            if (type != 20) continue;
            if (body.find(AmfStr("connect")) == 0) {
                app = AmfProp(body, "app");
                tcUrl = AmfProp(body, "tcUrl");
                std::string ack, bw;
                PutBe(ack, 2500000, 4);
                PutBe(bw, 2500000, 4);
                bw += '\x02';
                Send(c, Chunked(2, 5, 0, 0, ack) + Chunked(2, 6, 0, 0, bw));
                Send(c, Chunked(3, 20, 0, 0, AmfStr("_result") + AmfNum(1) + std::string("\x05", 1) +
                                                 AmfStatus("NetConnection.Connect.Success")));
            } else if (body.find(AmfStr("createStream")) == 0) {
                Send(c, Chunked(3, 20, 0, 0, AmfStr("_result") + AmfNum(2) + std::string("\x05", 1) + AmfNum(1)));
            } else if (body.find(AmfStr("play")) == 0) {
                // play, 0, null, the play path
                const std::size_t at = AmfStr("play").size() + 9 + 1;
                if (body.size() > at + 3 && body[at] == '\x02') {
                    const std::size_t n = (static_cast<unsigned char>(body[at + 1]) << 8) |
                                          static_cast<unsigned char>(body[at + 2]);
                    playPath = body.substr(at + 3, n);
                }
                Send(c, Chunked(5, 20, 1, 0, AmfStr("onStatus") + AmfNum(0) + std::string("\x05", 1) +
                                                 AmfStatus("NetStream.Play.Start")));
                std::string video;
                for (int i = 0; i < 300; ++i) video += static_cast<char>(i & 0xFF);
                std::string media = Chunked(6, 9, 1, 40, video);
                // Audio on the same chunk stream under a delta header: 40 + 20.
                media += static_cast<char>(0x40 | 6);
                PutBe(media, 20, 3);
                PutBe(media, 10, 3);
                media += '\x08';
                media += std::string(10, '\x7F');
                std::string ping;
                PutBe(ping, 6, 2);
                PutBe(ping, 1234, 4);
                media += Chunked(2, 4, 0, 0, ping);
                Send(c, media);
            }
        }
    }
};

uint32_t BeAt(const std::vector<uint8_t>& v, std::size_t at, int n) {
    uint32_t out = 0;
    for (int i = 0; i < n; ++i) out = (out << 8) | v[at + static_cast<std::size_t>(i)];
    return out;
}

} // namespace

TEST(rtmp_plugin_plays_a_stream_as_flv) {
    if (!LoadInto("RTMP", "rtmp")) SKIP("RTMP plug-in not buildable");
    auto p = UltraNet_GetPlugin("rtmp");
    auto* stream = dynamic_cast<IStreamingProtocolPlugin*>(p.get());
    REQUIRE(stream != nullptr);

    FakeRtmpServer server;
    REQUIRE(server.Start());
    UltraNetStreamOptions options;
    options.connectTimeoutMs = 5000;
    const UltraNetHandle h = stream->OpenStream(
        "rtmp://127.0.0.1:" + std::to_string(server.port) + "/live/test?key=abc", options);
    REQUIRE(h != UltraNetInvalidHandle);

    std::vector<uint8_t> flv;
    const std::size_t expected = 13 + (11 + 300 + 4) + (11 + 10 + 4);
    for (int i = 0; i < 6 && flv.size() < expected; ++i) {
        std::vector<uint8_t> frame;
        REQUIRE(stream->ReadFrame(h, frame).success);
        flv.insert(flv.end(), frame.begin(), frame.end());
    }
    REQUIRE_EQ(flv.size(), expected);
    REQUIRE(flv[0] == 'F' && flv[1] == 'L' && flv[2] == 'V' && flv[3] == 1);
    // The video tag: type 9, 300 bytes, at 40 ms, put back together whole.
    REQUIRE_EQ(flv[13], 9);
    REQUIRE_EQ(BeAt(flv, 14, 3), 300u);
    REQUIRE_EQ(BeAt(flv, 17, 3), 40u);
    bool intact = true;
    for (int i = 0; i < 300; ++i) intact = intact && flv[24 + static_cast<std::size_t>(i)] == (i & 0xFF);
    REQUIRE(intact);
    REQUIRE_EQ(BeAt(flv, 324, 4), 311u);
    // The audio tag: type 8, 10 bytes, at 40 + 20 ms.
    REQUIRE_EQ(flv[328], 8);
    REQUIRE_EQ(BeAt(flv, 329, 3), 10u);
    REQUIRE_EQ(BeAt(flv, 332, 3), 60u);

    // The client's side of it, as the server saw it.
    std::vector<uint8_t> more;
    stream->ReadFrame(h, more);   // gives the reader the ping to answer
    server.Join();
    REQUIRE_EQ(server.app, std::string("live"));
    REQUIRE_EQ(server.tcUrl, "rtmp://127.0.0.1:" + std::to_string(server.port) + "/live");
    REQUIRE_EQ(server.playPath, std::string("test?key=abc"));
    REQUIRE(server.pongSeen);
}

TEST(rtmp_plugin_refuses_an_address_without_a_stream) {
    if (!LoadInto("RTMP", "rtmp")) SKIP("RTMP plug-in not buildable");
    auto p = UltraNet_GetPlugin("rtmp");
    auto* stream = dynamic_cast<IStreamingProtocolPlugin*>(p.get());
    REQUIRE(stream != nullptr);
    UltraNetStreamOptions options;
    options.connectTimeoutMs = 1000;
    REQUIRE_EQ(stream->OpenStream("rtmp://127.0.0.1:1/live", options), UltraNetInvalidHandle);
}
#endif

// ===== Scheme registry coverage =====
TEST(all_quickwin_plugins_appear_in_supported_schemes) {
    // After loading any of them, the supported-scheme list should grow.
    const auto schemes = UltraNet_GetSupportedSchemes();
    // Core schemes always present.
    CHECK(std::find(schemes.begin(), schemes.end(), "https") != schemes.end());
    // At least one of the four — whichever loaded most recently — should
    // show up. (Each test above is independent; we don't assert all four
    // are present together because earlier tests may have torn things down.)
    bool anyQuickWin =
        std::find(schemes.begin(), schemes.end(), "webdav") != schemes.end() ||
        std::find(schemes.begin(), schemes.end(), "ldap")   != schemes.end() ||
        std::find(schemes.begin(), schemes.end(), "telnet") != schemes.end() ||
        std::find(schemes.begin(), schemes.end(), "rtsp")   != schemes.end();
    CHECK(anyQuickWin);
}
