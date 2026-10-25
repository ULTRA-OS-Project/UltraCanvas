// Tests/UltraDatabase/test_tunnel.cpp
// SSH tunnels (UltraDatabaseTunnel.h), with an opener of the test's own: the
// request a connection's ssh_* options become (the database's host and port
// as the bastion reaches them, the engine's default port), none of those
// options reaching the driver, the opener's refusals - an unknown or changed
// host key with its fingerprint - passed on as they are, the tunnel closed
// with the connection and when the driver fails, and what is refused before
// any tunnel is asked for: no opener, an engine without a server, a socket,
// MySQL asked to verify a certificate it cannot. A tunnel over real SSH to a
// real server is UltraDBAdmin's test (it links UltraNet).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraDatabase/UltraDatabase.h>

#include <atomic>
#include <string>

namespace {

struct Recorder {
    UltraDbTunnelRequest request;
    int opened = 0;
    std::atomic<int> closed{0};
    UltraDbResultCode refuse = UltraDbResultCode::Ok;
    int localPort = 1;   // nothing listens there: the driver's connect fails
};

Recorder* g_recorder = nullptr;

class FakeTunnel : public IUltraDbTunnel {
public:
    explicit FakeTunnel(Recorder& r) : recorder_(r) {}
    ~FakeTunnel() override { ++recorder_.closed; }
    int LocalPort() const override { return recorder_.localPort; }

private:
    Recorder& recorder_;
};

std::unique_ptr<IUltraDbTunnel> FakeOpener(const UltraDbTunnelRequest& request, UltraDbResult& error) {
    Recorder& r = *g_recorder;
    r.request = request;
    ++r.opened;
    if (r.refuse != UltraDbResultCode::Ok) {
        error = UltraDbResult::Error(r.refuse, "refused by the test");
        error.hostKeyFingerprint = "SHA256:test";
        return nullptr;
    }
    return std::make_unique<FakeTunnel>(r);
}

UltraDbConnectionConfig Tunnelled(const std::string& name, const std::string& driver) {
    UltraDbConnectionConfig cfg;
    cfg.name = name;
    cfg.driver = driver;
    cfg.database = "crm";
    cfg.host = "db.internal";
    cfg.user = "app";
    cfg.tls = UltraDbTls::Disable;
    cfg.options["ssh_host"] = "bastion.example.com";
    cfg.options["ssh_port"] = "2200";
    cfg.options["ssh_user"] = "deploy";
    cfg.options["ssh_host_key"] = "SHA256:trusted";
    cfg.options["connect_timeout"] = "2";
    return cfg;
}

} // namespace

TEST(tunnel_request_and_lifetime) {
    Recorder recorder;
    g_recorder = &recorder;
    UltraDb_SetTunnelOpener(&FakeOpener);

    UltraDbConnectionConfig cfg = Tunnelled("tunnel-pg", "postgresql");
    REQUIRE(UltraDb_UsesTunnel(cfg));
    REQUIRE(UltraDb_RegisterConnection(cfg).success);
    UltraDbResult opened = UltraDb_OpenConnection(cfg.name);
    if (UltraDatabase_GetDriver("postgresql") == nullptr) {
        UltraDb_CloseConnection(cfg.name);
        UltraDb_SetTunnelOpener(nullptr);
        return;   // built without libpq: nothing to open a tunnel for
    }
    // The driver could not connect (nothing listens on port 1), so the
    // tunnel went with it.
    REQUIRE(!opened.success);
    REQUIRE(opened.message.find("through the SSH tunnel via bastion.example.com") != std::string::npos);
    REQUIRE_EQ(recorder.opened, 1);
    REQUIRE_EQ(recorder.closed.load(), 1);
    REQUIRE_EQ(recorder.request.sshHost, std::string("bastion.example.com"));
    REQUIRE_EQ(recorder.request.sshPort, 2200);
    REQUIRE_EQ(recorder.request.sshUser, std::string("deploy"));
    REQUIRE_EQ(recorder.request.hostKeyFingerprint, std::string("SHA256:trusted"));
    REQUIRE_EQ(recorder.request.targetHost, std::string("db.internal"));
    REQUIRE_EQ(recorder.request.targetPort, 5432);              // the engine's own
    REQUIRE(recorder.request.password.empty() && recorder.request.privateKey.empty());   // agent / default keys
    UltraDb_CloseConnection(cfg.name);

    // A port given is the one reached; "" is the bastion itself.
    cfg.name = "tunnel-pg-2";
    cfg.host.clear();
    cfg.port = 6543;
    REQUIRE(UltraDb_RegisterConnection(cfg).success);
    UltraDb_OpenConnection(cfg.name);
    REQUIRE_EQ(recorder.request.targetHost, std::string("127.0.0.1"));
    REQUIRE_EQ(recorder.request.targetPort, 6543);
    UltraDb_CloseConnection(cfg.name);
    UltraDb_SetTunnelOpener(nullptr);
    g_recorder = nullptr;
}

TEST(tunnel_refusals_pass_through) {
    Recorder recorder;
    g_recorder = &recorder;
    UltraDb_SetTunnelOpener(&FakeOpener);
    UltraDbConnectionConfig cfg = Tunnelled("tunnel-refused", "postgresql");
    REQUIRE(UltraDb_RegisterConnection(cfg).success);
    if (UltraDatabase_GetDriver("postgresql") != nullptr) {
        recorder.refuse = UltraDbResultCode::HostKeyUnknown;
        UltraDbResult r = UltraDb_OpenConnection(cfg.name);
        REQUIRE(r.code == UltraDbResultCode::HostKeyUnknown);
        REQUIRE_EQ(r.hostKeyFingerprint, std::string("SHA256:test"));
        recorder.refuse = UltraDbResultCode::HostKeyChanged;
        r = UltraDb_OpenConnection(cfg.name);
        REQUIRE(r.code == UltraDbResultCode::HostKeyChanged);
        REQUIRE_EQ(recorder.closed.load(), 0);   // none was opened
    }
    UltraDb_CloseConnection(cfg.name);

    // No opener: refused before anything is tried.
    UltraDb_SetTunnelOpener(nullptr);
    cfg.name = "tunnel-no-opener";
    REQUIRE(UltraDb_RegisterConnection(cfg).success);
    if (UltraDatabase_GetDriver("postgresql") != nullptr)
        REQUIRE(UltraDb_OpenConnection(cfg.name).code == UltraDbResultCode::Unsupported);
    UltraDb_CloseConnection(cfg.name);
    g_recorder = nullptr;
}

TEST(tunnel_refused_where_it_cannot_lead) {
    Recorder recorder;
    g_recorder = &recorder;
    UltraDb_SetTunnelOpener(&FakeOpener);

    // SQLite has no server.
    UltraDbConnectionConfig cfg = Tunnelled("tunnel-sqlite", "sqlite");
    cfg.database = ":memory:";
    REQUIRE(UltraDb_RegisterConnection(cfg).success);
    REQUIRE(UltraDb_OpenConnection(cfg.name).code == UltraDbResultCode::InvalidArgument);
    UltraDb_CloseConnection(cfg.name);

    if (UltraDatabase_GetDriver("postgresql") != nullptr) {
        // A socket is no host and port.
        cfg = Tunnelled("tunnel-socket", "postgresql");
        cfg.host = "/var/run/postgresql";
        REQUIRE(UltraDb_RegisterConnection(cfg).success);
        REQUIRE(UltraDb_OpenConnection(cfg.name).code == UltraDbResultCode::InvalidArgument);
        UltraDb_CloseConnection(cfg.name);
    }
    if (UltraDatabase_GetDriver("mysql") != nullptr) {
        // MySQL checks a certificate against 127.0.0.1 through a tunnel.
        cfg = Tunnelled("tunnel-mysql", "mysql");
        cfg.options.erase("connect_timeout");
        cfg.tls = UltraDbTls::VerifyFull;
        REQUIRE(UltraDb_RegisterConnection(cfg).success);
        REQUIRE(UltraDb_OpenConnection(cfg.name).code == UltraDbResultCode::Unsupported);
        REQUIRE_EQ(recorder.request.targetPort, 3306);
        REQUIRE_EQ(recorder.closed.load(), recorder.opened);   // the tunnel opened, and closed again
        UltraDb_CloseConnection(cfg.name);
    }
    REQUIRE_EQ(recorder.opened, recorder.closed.load());
    UltraDb_SetTunnelOpener(nullptr);
    g_recorder = nullptr;
}
