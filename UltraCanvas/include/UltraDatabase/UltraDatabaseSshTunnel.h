// include/UltraDatabase/UltraDatabaseSshTunnel.h
// SSH tunnels for UltraDatabase (UltraDatabaseTunnel.h) over UltraNet's SSH
// plug-in (UltraNetSsh.h): one call at start-up, and every connection with an
// ssh_host option reaches its database through its bastion.
//
// Header-only on purpose: UltraDatabase links no network library of its own,
// and only a program that includes this - and links UltraNet, as every
// UltraCanvas application does - carries the bridge.
//
// doc-check: example
//   #include <UltraDatabase/UltraDatabaseSshTunnel.h>
//
//   UltraDb_UseUltraNetSshTunnels();
//
//   UltraDbConnectionConfig cfg;
//   cfg.name = "crm";
//   cfg.driver = "postgresql";
//   cfg.host = "db.internal";                 // as the bastion reaches it
//   cfg.database = "crm";
//   cfg.options["ssh_host"] = "bastion.example.com";
//   cfg.options["ssh_user"] = "deploy";
//   cfg.options["ssh_credentials"] = "vault:crm.ssh";   // the key, in UltraVault
//   cfg.options["ssh_host_key"] = "SHA256:Rf5ZfHLvj5c1Gkfg8PRZ1odq1jXTWJfCApPdnH1v3vg";
//   UltraDb_RegisterConnection(cfg);
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseTunnel.h"

#include <UltraNet/UltraNetSsh.h>

#include <memory>
#include <string>

namespace ultradb_ssh {

// A tunnel UltraNet opened; closed when destroyed.
class UltraNetTunnel : public IUltraDbTunnel {
public:
    UltraNetTunnel(UltraNetHandle handle, int port) : handle_(handle), port_(port) {}
    ~UltraNetTunnel() override { UltraNet_CloseSshTunnel(handle_); }
    int LocalPort() const override { return port_; }

private:
    UltraNetHandle handle_;
    int port_;
};

inline void Wipe(std::string& s) {
    volatile char* p = s.empty() ? nullptr : &s[0];
    for (size_t i = 0; i < s.size(); ++i) p[i] = 0;
    s.clear();
}

inline std::unique_ptr<IUltraDbTunnel> Open(const UltraDbTunnelRequest& request, UltraDbResult& error) {
    UltraNetSshTunnelOptions options;
    // An IPv6 address goes in brackets, as in any URL.
    const bool v6 = request.sshHost.find(':') != std::string::npos;
    options.url = "ssh://" + (v6 ? "[" + request.sshHost + "]" : request.sshHost) + ":" +
                  std::to_string(request.sshPort);
    options.remote.credentials.username = request.sshUser;
    options.remote.credentials.password = request.password;
    options.remote.privateKeyPem = request.privateKey;
    options.remote.privateKeyPassword = request.keyPassphrase;
    options.remote.hostKeyFingerprint = request.hostKeyFingerprint;
    options.targetHost = request.targetHost;
    options.targetPort = request.targetPort;
    UltraNetHandle handle = UltraNetInvalidHandle;
    UltraNetSshTunnelInfo info;
    const UltraNetResult opened = UltraNet_OpenSshTunnel(options, handle, info);
    Wipe(options.remote.credentials.password);
    Wipe(options.remote.privateKeyPassword);
    for (auto& b : options.remote.privateKeyPem) b = 0;
    if (!opened) {
        UltraDbResultCode code = UltraDbResultCode::ConnectionFailed;
        if (opened.code == UltraNetResultCode::SshHostKeyUnknown) code = UltraDbResultCode::HostKeyUnknown;
        else if (opened.code == UltraNetResultCode::SshHostKeyChanged) code = UltraDbResultCode::HostKeyChanged;
        else if (opened.code == UltraNetResultCode::PluginNotFound) code = UltraDbResultCode::Unsupported;
        error = UltraDbResult::Error(code, "SSH tunnel through " + request.sshHost + ": " + opened.message);
        error.hostKeyFingerprint = info.hostKeyFingerprint;
        return nullptr;
    }
    return std::make_unique<UltraNetTunnel>(handle, info.localPort);
}

} // namespace ultradb_ssh

// Open every tunnelled connection's tunnel through UltraNet's SSH plug-in.
inline void UltraDb_UseUltraNetSshTunnels() {
    UltraDb_SetTunnelOpener(&ultradb_ssh::Open);
}
