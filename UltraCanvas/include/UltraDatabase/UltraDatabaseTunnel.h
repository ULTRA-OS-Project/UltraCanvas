// include/UltraDatabase/UltraDatabaseTunnel.h
// A server connection through an SSH tunnel: the database sits behind a
// bastion, and only the bastion's SSH port is open. The connection's options
// name the bastion; when the connection opens, a tunnel is opened first - a
// port on 127.0.0.1 carried to the database's host and port as the bastion
// reaches them - and the driver connects through it. The tunnel closes with
// the connection. Every session the driver opens for the connection (a
// cursor's, a cancel's) goes through the same tunnel.
//
// The options (UltraDbConnectionConfig::options), none of which reaches the
// driver:
//
//   ssh_host            the bastion - its presence is what asks for a tunnel
//   ssh_port            its SSH port (22)
//   ssh_user            who signs in there
//   ssh_credentials     "vault:<key>": an UltraVault entry holding the password
//                       or the private key (OpenSSH / PEM text). Empty: the
//                       SSH agent, then the default key files
//   ssh_key_passphrase  "vault:<key>": the private key's passphrase
//   ssh_host_key        the bastion's host key as trusted, "SHA256:..."; empty:
//                       known_hosts must know it
//
// `host` and `port` stay the database's, as the bastion reaches it ("" is the
// bastion itself). An unknown host key fails the connection with
// HostKeyUnknown and the key in UltraDbResult::hostKeyFingerprint: shown to
// the user, and put in ssh_host_key once they trust it. A changed one fails
// with HostKeyChanged.
//
// UltraDatabase opens no socket of its own: an application supplies the
// opener once (UltraDb_SetTunnelOpener). UltraDatabaseSshTunnel.h has one over
// UltraNet's SSH plug-in, in a line.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// What a tunnel is to be: the bastion, how to sign in there - the secrets
// read from UltraVault already - and where the tunnel leads.
struct UltraDbTunnelRequest {
    std::string sshHost;
    int sshPort = 22;
    std::string sshUser;
    std::string password;                 // or
    std::vector<uint8_t> privateKey;      // a key, its passphrase below; neither: agent / default keys
    std::string keyPassphrase;
    std::string hostKeyFingerprint;       // "SHA256:..." trusted; "" for known_hosts
    std::string targetHost;               // as the bastion reaches the database
    int targetPort = 0;
};

// An open tunnel; closed when destroyed.
class IUltraDbTunnel {
public:
    virtual ~IUltraDbTunnel() = default;
    virtual int LocalPort() const = 0;    // on 127.0.0.1
};

// Opens a tunnel, or returns nullptr and sets `error` - HostKeyUnknown /
// HostKeyChanged with `error.hostKeyFingerprint` for the bastion's key.
using UltraDbTunnelOpener =
    std::function<std::unique_ptr<IUltraDbTunnel>(const UltraDbTunnelRequest& request, UltraDbResult& error)>;

// Install the opener every tunnelled connection uses (an empty one removes
// it: such a connection then fails with Unsupported). Connections already open
// keep their tunnels.
void UltraDb_SetTunnelOpener(UltraDbTunnelOpener opener);

// Does `config` ask for a tunnel (an ssh_host option)?
bool UltraDb_UsesTunnel(const UltraDbConnectionConfig& config);
