// UltraCanvas/Plugins/UltraNet/ssh/SshPlugin.cpp
// SSH plug-in. Implements IRemoteAccessPlugin (OpenShell / ExecuteCommand)
// using libssh — distinct from libssh2-via-libcurl that the FTP module
// uses for SFTP, this is for interactive remote-shell semantics.
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
// Version: 0.1.1
// Last Modified: 2026-07-05
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetUrl.h>

#include <libssh/libssh.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

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

// Attempts each available auth method in turn — explicit password,
// in-memory private key, agent, default identity files.
bool TryAuthenticate(ssh_session s, const UltraNetRemoteOptions& opt) {
    if (!opt.credentials.password.empty()) {
        return ssh_userauth_password(s, nullptr,
                                     opt.credentials.password.c_str())
               == SSH_AUTH_SUCCESS;
    }
    if (!opt.privateKeyPem.empty()) {
        ssh_key key = nullptr;
        if (ssh_pki_import_privkey_base64(
                reinterpret_cast<const char*>(opt.privateKeyPem.data()),
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

class SshPlugin : public IRemoteAccessPlugin {
public:
    std::string GetName() const override { return "UltraNet-SSH"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"ssh"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return UltraNetResult::Ok();
    }
    void Shutdown() override {
        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        g_sessions.clear();
    }

    UltraNetHandle OpenShell(const std::string& url,
                             const UltraNetRemoteOptions& options) override {
        UltraNetUrlComponents c;
        if (!UltraNet_ParseUrl(url, c) || c.scheme != "ssh") {
            return UltraNetInvalidHandle;
        }
        if (c.host.empty()) return UltraNetInvalidHandle;
        const int port = c.port > 0 ? c.port : 22;
        const std::string user =
            !options.credentials.username.empty()
                ? options.credentials.username
                : (!c.username.empty() ? c.username : std::string{});

        auto s = std::make_shared<Session>();
        s->opts = options;
        s->user = user;
        s->session = ssh_new();
        if (!s->session) return UltraNetInvalidHandle;

        ssh_options_set(s->session, SSH_OPTIONS_HOST, c.host.c_str());
        ssh_options_set(s->session, SSH_OPTIONS_PORT, &port);
        if (!user.empty()) {
            ssh_options_set(s->session, SSH_OPTIONS_USER, user.c_str());
        }
        if (options.connectTimeoutMs > 0) {
            const long sec = options.connectTimeoutMs / 1000;
            ssh_options_set(s->session, SSH_OPTIONS_TIMEOUT, &sec);
        }

        if (ssh_connect(s->session) != SSH_OK) return UltraNetInvalidHandle;
        if (!TryAuthenticate(s->session, options)) return UltraNetInvalidHandle;

        const UltraNetHandle h =
            g_nextHandle.fetch_add(1, std::memory_order_relaxed);
        s->handle = h;
        {
            std::lock_guard<std::mutex> lk(g_sessionsMutex);
            g_sessions[h] = s;
        }
        return h;
    }

    UltraNetResult ExecuteCommand(UltraNetHandle handle,
                                  const std::string& command,
                                  std::string& outStdOut,
                                  std::string& outStdErr,
                                  int& outExitCode) override {
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
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<SshPlugin>());
}
#if !defined(_WIN32) && !defined(_WIN64)  // v1 resolves UltraNet_RegisterPlugin from the host at dlopen(); POSIX-only, Windows uses the v2 UltraNet_PluginInit vtable above
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<SshPlugin>());
}
#endif
