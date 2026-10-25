// UltraCanvas/Plugins/UltraNet/ssh/SshPlugin.cpp
// SSH plug-in. Implements IRemoteAccessPlugin (OpenShell / ExecuteCommand)
// and ISshTunnelPlugin (local port forwarding, UltraNetSsh.h) using libssh —
// distinct from libssh2-via-libcurl that the FTP module uses for SFTP, this is
// for interactive remote-shell semantics and channels of our own.
//
// Backend: libssh (libssh-dev on Debian, libssh on brew, libssh on
// MSYS2). Picks password OR public-key auth from
// UltraNetRemoteOptions:
//   - if options.credentials.password is non-empty → password auth
//   - else if options.privateKeyPem is non-empty   → key auth (in-memory
//                                                    PEM, optionally with
//                                                    options.privateKeyPassword)
//   - else                                         → user agent fallback
//                                                    (ssh-agent), then
//                                                    default identity files
// Before any of them the server's host key is checked (CheckHostKey): the
// fingerprint the caller trusted, else known_hosts. Nothing is sent to a
// server whose key is unknown or changed.
//
// A tunnel listens on 127.0.0.1 and forwards each connection on a direct-
// tcpip channel of its own. Its SSH session belongs to one thread, the pump,
// from the moment OpenTunnel returns: libssh allows a session to be used from
// one thread at a time, and that thread accepts, opens channels and copies
// bytes both ways.
// Version: 0.2.0 - host keys checked (known_hosts or a pinned fingerprint);
//                  ISshTunnelPlugin: local port forwarding
// Version: 0.1.2 - every entry point is guarded (UltraNetPluginGuard.h): an
//                  exception becomes a PluginError result or an invalid
//                  handle, never ends the process
// Version: 0.1.1
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include "UltraNetPluginGuard.h"
#include "UltraNetPluginHostShim.h"
#include <UltraNet/UltraNetUrl.h>

#include <libssh/libssh.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

// ---- Sockets, the few calls the pump needs ---------------------------------

#ifdef _WIN32
using Socket = SOCKET;
const Socket kNoSocket = INVALID_SOCKET;
void CloseSocket(Socket s) { closesocket(s); }
int PollSockets(pollfd* fds, size_t n, int ms) { return WSAPoll(fds, static_cast<ULONG>(n), ms); }
void SetNonBlocking(Socket s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
bool WouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
void EndWriting(Socket s) { shutdown(s, SD_SEND); }
#else
using Socket = int;
const Socket kNoSocket = -1;
void CloseSocket(Socket s) { close(s); }
int PollSockets(pollfd* fds, size_t n, int ms) { return poll(fds, static_cast<nfds_t>(n), ms); }
void SetNonBlocking(Socket s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
void EndWriting(Socket s) { shutdown(s, SHUT_WR); }
#endif

#ifdef _WIN32
// WSAStartup once per process; libssh needs it as much as the listener does.
bool StartSockets() {
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return started;
}
#else
bool StartSockets() { return true; }
#endif

// A socket listening on a free port of 127.0.0.1, and that port.
Socket Listen(int& port) {
    port = 0;
    Socket s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == kNoSocket) return kNoSocket;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    socklen_t length = sizeof addr;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(s, 16) != 0 ||
        getsockname(s, reinterpret_cast<sockaddr*>(&addr), &length) != 0) {
        CloseSocket(s);
        return kNoSocket;
    }
    port = ntohs(addr.sin_port);
    SetNonBlocking(s);
    return s;
}

// ---- Signing in ------------------------------------------------------------

// Attempts each available auth method in turn — explicit password,
// in-memory private key, agent, default identity files.
bool TryAuthenticate(ssh_session s, const UltraNetRemoteOptions& opt) {
    if (!opt.credentials.password.empty()) {
        return ssh_userauth_password(s, nullptr,
                                     opt.credentials.password.c_str())
               == SSH_AUTH_SUCCESS;
    }
    if (!opt.privateKeyPem.empty()) {
        // ssh_pki_import_privkey_base64 reads a NUL-terminated text.
        const std::string pem(opt.privateKeyPem.begin(), opt.privateKeyPem.end());
        ssh_key key = nullptr;
        if (ssh_pki_import_privkey_base64(
                pem.c_str(),
                opt.privateKeyPassword.empty() ? nullptr
                                               : opt.privateKeyPassword.c_str(),
                nullptr, nullptr, &key) != SSH_OK) {
            return false;
        }
        const int rc = ssh_userauth_publickey(s, nullptr, key);
        ssh_key_free(key);
        return rc == SSH_AUTH_SUCCESS;
    }
    // Try agent first, then default identity files.
    if (ssh_userauth_agent(s, nullptr) == SSH_AUTH_SUCCESS) return true;
    if (ssh_userauth_publickey_auto(s, nullptr, nullptr) == SSH_AUTH_SUCCESS) {
        return true;
    }
    return false;
}

// The server's host key as "SHA256:<base64>", as ssh-keygen -l prints it.
std::string ServerFingerprint(ssh_session s) {
    ssh_key key = nullptr;
    if (ssh_get_server_publickey(s, &key) != SSH_OK) return std::string();
    unsigned char* hash = nullptr;
    size_t length = 0;
    std::string out;
    if (ssh_get_publickey_hash(key, SSH_PUBLICKEY_HASH_SHA256, &hash, &length) == SSH_OK) {
        char* text = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, length);
        if (text) {
            out = text;
            ssh_string_free_char(text);
        }
        ssh_clean_pubkey_hash(&hash);
    }
    ssh_key_free(key);
    return out;
}

// Is this the server the caller meant? The fingerprint it trusted decides when
// it gave one; else known_hosts. `fingerprint` is the server's either way.
UltraNetResult CheckHostKey(ssh_session s, const UltraNetRemoteOptions& opt, const std::string& host,
                            std::string& fingerprint) {
    fingerprint = ServerFingerprint(s);
    if (fingerprint.empty())
        return UltraNetResult::Error(UltraNetResultCode::PluginError, host + " sent no host key that could be read");
    if (!opt.hostKeyFingerprint.empty()) {
        if (opt.hostKeyFingerprint == fingerprint) return UltraNetResult::Ok();
        return UltraNetResult::Error(UltraNetResultCode::SshHostKeyChanged,
                                     host + "'s host key is not the one trusted for it: it is " + fingerprint +
                                         ", trusted was " + opt.hostKeyFingerprint);
    }
    switch (ssh_session_is_known_server(s)) {
        case SSH_KNOWN_HOSTS_OK:
            return UltraNetResult::Ok();
        case SSH_KNOWN_HOSTS_CHANGED:
        case SSH_KNOWN_HOSTS_OTHER:
            return UltraNetResult::Error(UltraNetResultCode::SshHostKeyChanged,
                                         host + "'s host key differs from the one known_hosts holds for it (" +
                                             fingerprint + ")");
        case SSH_KNOWN_HOSTS_NOT_FOUND:
        case SSH_KNOWN_HOSTS_UNKNOWN:
            return UltraNetResult::Error(UltraNetResultCode::SshHostKeyUnknown,
                                         host + "'s host key is not known yet: " + fingerprint);
        case SSH_KNOWN_HOSTS_ERROR:
        default:
            return UltraNetResult::Error(UltraNetResultCode::PluginError,
                                         std::string("known_hosts could not be read: ") + ssh_get_error(s));
    }
}

// A session connected to `url`'s server, its host key checked and signed in.
struct Connected {
    ssh_session session = nullptr;
    std::string user;
    std::string fingerprint;
};

UltraNetResult Connect(const std::string& url, const UltraNetRemoteOptions& options, Connected& out) {
    UltraNetUrlComponents c;
    if (!UltraNet_ParseUrl(url, c) || c.scheme != "ssh" || c.host.empty())
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "not an ssh://host URL: " + url);
    if (!StartSockets()) return UltraNetResult::Error(UltraNetResultCode::PluginError, "sockets are not available");
    const int port = c.port > 0 ? c.port : 22;
    out.user = !options.credentials.username.empty() ? options.credentials.username : c.username;

    ssh_session s = ssh_new();
    if (!s) return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory, "ssh_new failed");
    ssh_options_set(s, SSH_OPTIONS_HOST, c.host.c_str());
    ssh_options_set(s, SSH_OPTIONS_PORT, &port);
    if (!out.user.empty()) ssh_options_set(s, SSH_OPTIONS_USER, out.user.c_str());
    if (!options.knownHostsFile.empty()) ssh_options_set(s, SSH_OPTIONS_KNOWNHOSTS, options.knownHostsFile.c_str());
    if (options.connectTimeoutMs > 0) {
        const long seconds = (options.connectTimeoutMs + 999) / 1000;
        ssh_options_set(s, SSH_OPTIONS_TIMEOUT, &seconds);
    }
    if (ssh_connect(s) != SSH_OK) {
        const UltraNetResult failed = UltraNetResult::Error(UltraNetResultCode::ConnectionRefused,
                                                            c.host + ":" + std::to_string(port) + ": " + ssh_get_error(s));
        ssh_free(s);
        return failed;
    }
    UltraNetResult checked = CheckHostKey(s, options, c.host, out.fingerprint);
    if (!checked) {
        ssh_disconnect(s);
        ssh_free(s);
        return checked;
    }
    if (!TryAuthenticate(s, options)) {
        const UltraNetResult failed = UltraNetResult::Error(UltraNetResultCode::AuthenticationFailed,
                                                            out.user + "@" + c.host + " did not sign in: " + ssh_get_error(s));
        ssh_disconnect(s);
        ssh_free(s);
        return failed;
    }
    out.session = s;
    return UltraNetResult::Ok();
}

// ---- Shells ----------------------------------------------------------------

struct Session {
    UltraNetHandle handle  = UltraNetInvalidHandle;
    ssh_session    session = nullptr;
    UltraNetRemoteOptions opts;
    std::string    user;

    ~Session() {
        if (session) {
            if (ssh_is_connected(session)) ssh_disconnect(session);
            ssh_free(session);
        }
    }
};

std::mutex g_sessionsMutex;
std::unordered_map<UltraNetHandle, std::shared_ptr<Session>> g_sessions;
std::atomic<UltraNetHandle> g_nextHandle{1};

std::shared_ptr<Session> Find(UltraNetHandle h) {
    std::lock_guard<std::mutex> lk(g_sessionsMutex);
    auto it = g_sessions.find(h);
    return it == g_sessions.end() ? nullptr : it->second;
}

// ---- Tunnels ---------------------------------------------------------------

// One forwarded connection: the client's socket and its channel, with what
// each side has sent that the other has not taken yet.
struct Forward {
    Socket client = kNoSocket;
    ssh_channel channel = nullptr;
    std::string toChannel, toClient;
    bool clientDone = false;     // the client sent all it will
    bool channelDone = false;    // the far end did
    bool eofSent = false;        // the client's end passed on to the channel
    bool clientShut = false;     // the far end's passed on to the client
    bool failed = false;

    ~Forward() {
        if (channel) {
            ssh_channel_close(channel);
            ssh_channel_free(channel);
        }
        if (client != kNoSocket) CloseSocket(client);
    }
    bool Finished() const { return failed || (eofSent && clientShut); }
};

constexpr size_t kChunk = 32 * 1024;
constexpr size_t kBacklog = 1024 * 1024;   // stop reading a side while this much waits for the other

class Tunnel {
public:
    Tunnel(ssh_session session, Socket listener, std::string targetHost, int targetPort)
        : session_(session), listener_(listener), targetHost_(std::move(targetHost)), targetPort_(targetPort) {}
    ~Tunnel() {
        Stop();
        if (listener_ != kNoSocket) CloseSocket(listener_);
        if (session_) {
            ssh_disconnect(session_);
            ssh_free(session_);
        }
    }
    void Start() { thread_ = std::thread([this] { Pump(); }); }
    void Stop() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
        forwards_.clear();   // channels before the session they belong to
    }

private:
    void Accept() {
        for (;;) {
            sockaddr_in peer{};
            socklen_t length = sizeof peer;
            const Socket client = accept(listener_, reinterpret_cast<sockaddr*>(&peer), &length);
            if (client == kNoSocket) return;
            auto forward = std::make_unique<Forward>();
            forward->client = client;
            SetNonBlocking(client);
            forward->channel = ssh_channel_new(session_);
            // A refusal by the SSH server (no such host, forwarding not
            // allowed) closes the client's connection: it sees the database
            // gone, and the error is the database driver's to report.
            if (!forward->channel ||
                ssh_channel_open_forward(forward->channel, targetHost_.c_str(), targetPort_, "127.0.0.1",
                                         ntohs(peer.sin_port)) != SSH_OK)
                continue;
            forwards_.push_back(std::move(forward));
        }
    }

    // Move what each side has for the other, as far as each will take it.
    void Exchange(Forward& f, bool clientReadable) {
        char buffer[kChunk];
        if (clientReadable && !f.clientDone && f.toChannel.size() < kBacklog) {
            const int n = static_cast<int>(recv(f.client, buffer, sizeof buffer, 0));
            if (n > 0) f.toChannel.append(buffer, static_cast<size_t>(n));
            else if (n == 0) f.clientDone = true;
            else if (!WouldBlock()) f.clientDone = true;
        }
        if (!f.toChannel.empty()) {
            const int n = ssh_channel_write(f.channel, f.toChannel.data(), static_cast<uint32_t>(f.toChannel.size()));
            if (n == SSH_ERROR) { f.failed = true; return; }
            if (n > 0) f.toChannel.erase(0, static_cast<size_t>(n));
        }
        if (f.clientDone && f.toChannel.empty() && !f.eofSent) {
            ssh_channel_send_eof(f.channel);
            f.eofSent = true;
        }
        while (!f.channelDone && f.toClient.size() < kBacklog) {
            const int n = ssh_channel_read_nonblocking(f.channel, buffer, sizeof buffer, 0);
            if (n == SSH_ERROR) { f.failed = true; return; }
            if (n > 0) { f.toClient.append(buffer, static_cast<size_t>(n)); continue; }
            if (ssh_channel_is_eof(f.channel) || ssh_channel_is_closed(f.channel)) f.channelDone = true;
            break;
        }
        while (!f.toClient.empty()) {
            const int n = static_cast<int>(send(f.client, f.toClient.data(), static_cast<int>(f.toClient.size()), 0));
            if (n > 0) { f.toClient.erase(0, static_cast<size_t>(n)); continue; }
            if (n < 0 && !WouldBlock()) f.failed = true;
            break;
        }
        if (f.channelDone && f.toClient.empty() && !f.clientShut) {
            EndWriting(f.client);
            f.clientShut = true;
        }
    }

    void Pump() {
        std::vector<pollfd> fds;
        while (!stop_) {
            fds.clear();
            fds.push_back({listener_, POLLIN, 0});
            const socket_t sshSocket = ssh_get_fd(session_);
            fds.push_back({static_cast<Socket>(sshSocket), POLLIN, 0});
            for (const auto& f : forwards_) {
                short events = 0;
                if (!f->clientDone && f->toChannel.size() < kBacklog) events |= POLLIN;
                if (!f->toClient.empty()) events |= POLLOUT;
                fds.push_back({f->client, events, 0});
            }
            // Short, so data libssh has already buffered for a channel - which
            // the socket poll cannot see - waits no longer than this.
            PollSockets(fds.data(), fds.size(), 20);
            if (fds[0].revents & POLLIN) Accept();
            if (!ssh_is_connected(session_)) {
                // The SSH server is gone: so is every connection through it.
                forwards_.clear();
                continue;
            }
            size_t at = 2;
            for (auto it = forwards_.begin(); it != forwards_.end();) {
                const bool readable = at < fds.size() && (fds[at].revents & (POLLIN | POLLHUP | POLLERR));
                ++at;
                Exchange(**it, readable);
                it = (*it)->Finished() ? forwards_.erase(it) : std::next(it);
            }
        }
    }

    ssh_session session_ = nullptr;
    Socket listener_ = kNoSocket;
    std::string targetHost_;
    int targetPort_ = 0;
    std::list<std::unique_ptr<Forward>> forwards_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

std::mutex g_tunnelsMutex;
std::unordered_map<UltraNetHandle, std::unique_ptr<Tunnel>> g_tunnels;

class SshPlugin : public ISshTunnelPlugin {
public:
    std::string GetName() const override { return "UltraNet-SSH"; }
    std::string GetVersion() const override { return "0.2.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"ssh"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return ultranet_plugin::Guarded("Initialize", [&]() -> UltraNetResult {
            return UltraNetResult::Ok();
        });
    }
    void Shutdown() override {
        ultranet_plugin::GuardedVoid([&]() {
            {
                std::lock_guard<std::mutex> lk(g_sessionsMutex);
                g_sessions.clear();
            }
            std::unordered_map<UltraNetHandle, std::unique_ptr<Tunnel>> tunnels;
            {
                std::lock_guard<std::mutex> lk(g_tunnelsMutex);
                tunnels.swap(g_tunnels);
            }
            tunnels.clear();   // each joins its pump, outside the lock
        });
    }

    UltraNetHandle OpenShell(const std::string& url,
                             const UltraNetRemoteOptions& options) override {
        return ultranet_plugin::GuardedHandle([&]() -> UltraNetHandle {
            Connected connected;
            if (!Connect(url, options, connected)) return UltraNetInvalidHandle;

            auto s = std::make_shared<Session>();
            s->opts = options;
            s->user = connected.user;
            s->session = connected.session;
            const UltraNetHandle h =
                g_nextHandle.fetch_add(1, std::memory_order_relaxed);
            s->handle = h;
            {
                std::lock_guard<std::mutex> lk(g_sessionsMutex);
                g_sessions[h] = s;
            }
            return h;
        });
    }

    UltraNetResult ExecuteCommand(UltraNetHandle handle,
                                  const std::string& command,
                                  std::string& outStdOut,
                                  std::string& outStdErr,
                                  int& outExitCode) override {
        return ultranet_plugin::Guarded("ExecuteCommand", [&]() -> UltraNetResult {
            outStdOut.clear();
            outStdErr.clear();
            outExitCode = -1;

            auto s = Find(handle);
            if (!s) return UltraNetResult::Error(
                UltraNetResultCode::InvalidHandle, "no such SSH session");

            ssh_channel ch = ssh_channel_new(s->session);
            if (!ch) return UltraNetResult::Error(
                UltraNetResultCode::InsufficientMemory, "ssh_channel_new failed");
            std::unique_ptr<ssh_channel_struct, decltype(&ssh_channel_free)>
                chGuard(ch, ssh_channel_free);

            if (ssh_channel_open_session(ch) != SSH_OK) {
                return UltraNetResult::Error(UltraNetResultCode::Unknown,
                                             "ssh_channel_open_session failed");
            }
            if (s->opts.requestPty) {
                ssh_channel_request_pty_size(
                    ch,
                    s->opts.term.empty() ? "xterm" : s->opts.term.c_str(),
                    80, 24);
            }
            if (ssh_channel_request_exec(ch, command.c_str()) != SSH_OK) {
                return UltraNetResult::Error(UltraNetResultCode::Unknown,
                                             "ssh_channel_request_exec failed");
            }

            // Drain stdout (is_stderr=0) and stderr (is_stderr=1) until EOF.
            char buf[4096];
            for (int stream = 0; stream <= 1; ++stream) {
                for (;;) {
                    const int n = ssh_channel_read(ch, buf, sizeof buf, stream);
                    if (n <= 0) break;
                    (stream == 0 ? outStdOut : outStdErr).append(buf, n);
                }
            }
            ssh_channel_send_eof(ch);
            ssh_channel_close(ch);
            outExitCode = ssh_channel_get_exit_status(ch);
            if (outExitCode < 0) outExitCode = 0;   // -1 = "unknown", treat as ok
            return UltraNetResult::Ok();
        });
    }

    UltraNetResult OpenTunnel(const UltraNetSshTunnelOptions& options, UltraNetHandle& outHandle,
                              UltraNetSshTunnelInfo& outInfo) override {
        return ultranet_plugin::Guarded("OpenTunnel", [&]() -> UltraNetResult {
            outHandle = UltraNetInvalidHandle;
            outInfo = UltraNetSshTunnelInfo();
            if (options.targetHost.empty() || options.targetPort <= 0 || options.targetPort > 65535)
                return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "a tunnel needs the host and port to reach");
            Connected connected;
            UltraNetResult result = Connect(options.url, options.remote, connected);
            outInfo.hostKeyFingerprint = connected.fingerprint;
            if (!result) return result;
            int port = 0;
            const Socket listener = Listen(port);
            if (listener == kNoSocket) {
                ssh_disconnect(connected.session);
                ssh_free(connected.session);
                return UltraNetResult::Error(UltraNetResultCode::PluginError, "no port of 127.0.0.1 could be listened on");
            }
            auto tunnel = std::make_unique<Tunnel>(connected.session, listener, options.targetHost, options.targetPort);
            tunnel->Start();
            outHandle = g_nextHandle.fetch_add(1, std::memory_order_relaxed);
            outInfo.localPort = port;
            std::lock_guard<std::mutex> lk(g_tunnelsMutex);
            g_tunnels[outHandle] = std::move(tunnel);
            return UltraNetResult::Ok();
        });
    }

    UltraNetResult CloseTunnel(UltraNetHandle handle) override {
        return ultranet_plugin::Guarded("CloseTunnel", [&]() -> UltraNetResult {
            std::unique_ptr<Tunnel> tunnel;
            {
                std::lock_guard<std::mutex> lk(g_tunnelsMutex);
                auto it = g_tunnels.find(handle);
                if (it == g_tunnels.end())
                    return UltraNetResult::Error(UltraNetResultCode::InvalidHandle, "no such SSH tunnel");
                tunnel = std::move(it->second);
                g_tunnels.erase(it);
            }
            tunnel.reset();   // joins its pump, outside the lock
            return UltraNetResult::Ok();
        });
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    // ABI 2: the core functions this plug-in calls come through `host`
    // (UltraNetPluginHostShim); an older host cannot serve them.
    if (!UltraNetPlugin_AttachHost(host)) return;
    host->RegisterPlugin(std::make_shared<SshPlugin>());
}
