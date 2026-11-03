// include/UltraNet/UltraNetSsh.h
// SSH local port forwarding: a TCP port on 127.0.0.1 whose connections are
// carried, through an SSH server (a bastion), to a host and port that server
// reaches - a database behind a firewall, typically. Served by the "ssh"
// plug-in (Plugins/UltraNet/ssh, libssh), loaded on first use.
//
// The server's host key is checked before anything is sent: it must match
// `hostKeyFingerprint` when that is set (the key the user trusted before,
// "SHA256:..." as ssh-keygen -l prints it), or else be the one known_hosts
// holds for the host. A key nobody has seen fails with SshHostKeyUnknown and
// the key's fingerprint in UltraNetSshTunnelInfo, for the caller to show and,
// once the user agrees, to pass as `hostKeyFingerprint`; a key that differs
// from the one known fails with SshHostKeyChanged, and is never accepted.
//
// doc-check: example
//   #include <UltraNet/UltraNetSsh.h>
//
//   UltraNetSshTunnelOptions tunnel;
//   tunnel.url = "ssh://deploy@bastion.example.com";
//   tunnel.remote.privateKeyPem = key;              // or credentials.password
//   tunnel.remote.hostKeyFingerprint = "SHA256:Rf5ZfHLvj5c1Gkfg8PRZ1odq1jXTWJfCApPdnH1v3vg";
//   tunnel.targetHost = "db.internal";
//   tunnel.targetPort = 5432;
//   UltraNetHandle handle = UltraNetInvalidHandle;
//   UltraNetSshTunnelInfo info;
//   if (UltraNet_OpenSshTunnel(tunnel, handle, info)) {
//       // connect to 127.0.0.1:info.localPort ...
//       UltraNet_CloseSshTunnel(handle);
//   }
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"

#include <cstdint>
#include <string>
#include <vector>

// How to reach and sign in to an SSH server - the shell and the tunnel alike.
struct UltraNetRemoteOptions {
    UltraNetCredentials credentials;    // user; a password, when one signs in
    std::vector<uint8_t> privateKeyPem; // else a private key (OpenSSH / PEM text)
    std::string privateKeyPassword;     // its passphrase, when it has one
    int connectTimeoutMs = 10000;
    int idleTimeoutMs = 0;              // 0 = no idle timeout
    bool requestPty = false;
    std::string term;                   // e.g. "xterm-256color"
    // The server's host key: the fingerprint trusted for it ("SHA256:...";
    // known_hosts decides when empty), and the known_hosts file to read
    // ("" for ~/.ssh/known_hosts).
    std::string hostKeyFingerprint;
    std::string knownHostsFile;
};

struct UltraNetSshTunnelOptions {
    std::string url;                    // ssh://[user@]host[:port], the SSH server
    UltraNetRemoteOptions remote;       // signing in, the host key
    std::string targetHost;             // as the SSH server reaches it
    int targetPort = 0;
};

struct UltraNetSshTunnelInfo {
    int localPort = 0;                  // on 127.0.0.1, while the tunnel is open
    std::string hostKeyFingerprint;     // the server's, "SHA256:..." - set on SshHostKey* too
};

// Open a tunnel: signs in, checks the host key, and listens on a free port of
// 127.0.0.1; each connection made to it is forwarded on a channel of its own.
// `outHandle` closes it. PluginNotFound when no "ssh" plug-in can be loaded.
UltraNetResult UltraNet_OpenSshTunnel(const UltraNetSshTunnelOptions& options, UltraNetHandle& outHandle,
                                      UltraNetSshTunnelInfo& outInfo);

// Stop listening, end every forwarded connection and sign out.
UltraNetResult UltraNet_CloseSshTunnel(UltraNetHandle handle);
