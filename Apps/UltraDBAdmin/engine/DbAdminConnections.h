// Apps/UltraDBAdmin/engine/DbAdminConnections.h
// The connections UltraDBAdmin knows: kept in connections.json in its data
// directory, never with a password in it. A password goes into the app's own
// vault (UltraVault::DeviceKeyVault, encrypted at rest) under a key made from
// the connection's id, and the connection hands UltraDatabase only that key -
// the PostgreSQL and MySQL drivers read it from the vault when they connect.
// A server behind a bastion is reached through an SSH tunnel: the bastion,
// its user and its host key (the fingerprint the user trusted) are kept in
// the list; the SSH password or private key, and the key's passphrase, in
// the vault beside the database password (UltraDatabaseTunnel.h).
// Version: 0.4.0 - SSH tunnels
// Version: 0.3.0 - MySQL / MariaDB connections; EngineOf
// Version: 0.2.0 - auto-commit per connection
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminEngine.h"

#include <UltraDatabase/UltraDatabase.h>

#include <memory>
#include <string>
#include <vector>

#if !defined(ULTRADBADMIN_NO_VAULT)
namespace UltraVault { class DeviceKeyVault; }
#endif

namespace UltraDBAdmin {

struct ConnectionSpec {
    std::string id;                 // generated; ASCII, so it can name a vault key
    std::string name;               // what the tree shows
    std::string driver = "sqlite";  // "sqlite", "postgresql" or "mysql" (MariaDB too)
    std::string database;           // SQLite: the file; a server: the database
    std::string host;               // a server only; a socket directory (PostgreSQL) or socket file (MySQL) works too
    int         port = 0;           // 0: the driver's default
    std::string user;
    bool        hasPassword = false;   // a password is in the vault for this id
    UltraDbTls  tls = UltraDbTls::VerifyFull;
    bool        readOnly = false;
    bool        production = false;    // shown in red, so a slip is visible
    // Browse writes a row change as soon as it is made, as phpMyAdmin does,
    // instead of staging it for Commit. A deletion and anything on a
    // production connection still ask first.
    bool        autoCommit = false;
    // Through an SSH tunnel, when sshHost is set; host and port are then the
    // database's as the bastion reaches it.
    std::string sshHost;
    int         sshPort = 0;            // 0: 22
    std::string sshUser;
    std::string sshAuth = "agent";      // "agent" (the SSH agent, then the default keys), "password", "key"
    bool        hasSshSecret = false;   // the SSH password or private key is in the vault
    bool        hasSshPassphrase = false;
    std::string sshHostKey;             // "SHA256:..." trusted; "" until the user trusts one
    // Through an UltraDBAdmin relay, when driver is "relay"
    // (UltraDatabaseRelay.h): `host` is the relay's address, `database` the
    // name of a connection it offers, the token kept where a password is, and
    // this the engine behind it - whose SQL the app writes.
    std::string relayEngine;            // "sqlite", "postgresql" or "mysql"

    bool UsesTunnel() const { return !sshHost.empty(); }
    bool IsRelay() const { return driver == "relay"; }
    // The dialect, through a relay the engine's behind it.
    bool IsSqlite() const {
        return driver == "sqlite" || driver == "sqlite3" || (IsRelay() && relayEngine == "sqlite");
    }
    bool IsMysql() const {
        return driver == "mysql" || driver == "mariadb" || (IsRelay() && relayEngine == "mysql");
    }
    // A file on this computer, not a server or a relay.
    bool IsLocalFile() const { return driver == "sqlite" || driver == "sqlite3"; }
};

// The dialect `spec` speaks.
SqlEngine EngineOf(const ConnectionSpec& spec);

// The name a spec is registered under in UltraDatabase: the session the
// tree, Browse and Structure use.
std::string RegisteredName(const ConnectionSpec& spec);

// A second session to the same database, the SQL console's own. A script
// that runs for minutes holds its session (the drivers serialise each
// connection), so on a session of its own it never holds up the tree or a
// Browse page - and Stop cancels exactly the console's statement.
std::string ConsoleName(const ConnectionSpec& spec);

// The connection id a registered name - RegisteredName's or ConsoleName's -
// is for; "" for a name that is not the app's. The SQL log's filter.
std::string ConnectionIdOf(const std::string& registeredName);

// A one-line description for the tree's tooltip and the window title:
// "notes.db", "alice@db.example.com:5432/crm" or, through a tunnel,
// "alice@db.internal/crm via deploy@bastion.example.com"; through a relay
// "crm on wss://db.example.com/relay".
std::string Describe(const ConnectionSpec& spec);

class ConnectionStore {
public:
    ConnectionStore();
    ~ConnectionStore();

    // Read `directory`/connections.json and open the vault in
    // `directory`/vault. A missing file is an empty list, not an error.
    // Built without a vault (ULTRADBADMIN_NO_VAULT, the browser build) a
    // secret is kept in memory for the session only.
    bool Open(const std::string& directory, std::string& error);

    // Kept in memory only, with an in-memory vault - for tests.
    void OpenInMemory();

    const std::vector<ConnectionSpec>& All() const { return specs_; }
    const ConnectionSpec* Find(const std::string& id) const;

    // The secrets a Put changes in the vault: each stored when not empty,
    // removed when its clear flag is set, left alone otherwise.
    struct SecretChanges {
        std::string password;           // the database's
        bool clearPassword = false;
        std::string sshSecret;          // the SSH password, or a private key's text
        bool clearSshSecret = false;
        std::string sshPassphrase;      // the private key's passphrase
        bool clearSshPassphrase = false;
    };

    // Add (spec.id empty) or replace (same id) a connection and save the list.
    // `password` is stored in the vault when not empty; `clearPassword`
    // removes a stored one. Returns the id.
    std::string Put(ConnectionSpec spec, const std::string& password, bool clearPassword,
                    std::string& error);
    std::string Put(ConnectionSpec spec, const SecretChanges& secrets, std::string& error);

    // Forget a connection and its stored password, and close its sessions. A
    // query still running on one finishes first (UltraDatabase keeps a
    // connection until its last call returns).
    bool Remove(const std::string& id, std::string& error);

    // Register (or re-register) the spec with UltraDatabase under both its
    // names (RegisteredName, ConsoleName), so they can be queried. Opens
    // neither: the first query does. Re-registering is safe at any time: a
    // query running on the old settings finishes on them, and the next one
    // opens a session with the new.
    UltraDbResult Register(const ConnectionSpec& spec) const;

    // A new id: "c" followed by 16 hex digits of randomness.
    static std::string NewId();

private:
    bool Save(std::string& error) const;

    std::string file_;              // empty: in memory
    std::vector<ConnectionSpec> specs_;
#if !defined(ULTRADBADMIN_NO_VAULT)
    std::unique_ptr<UltraVault::DeviceKeyVault> vault_;
#endif
    bool memoryVault_ = false;
};

} // namespace UltraDBAdmin
