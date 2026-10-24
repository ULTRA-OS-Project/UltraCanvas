// Apps/UltraDBAdmin/relay/RelayConfig.h
// The relay's configuration file: an INI file an administrator writes.
//
//   listen = 127.0.0.1:8765        # loopback: TLS is the proxy's in front
//   path = /relay                  # the WebSocket's path
//   vault = /var/lib/ultradbadmin-relay/vault
//   audit = /var/log/ultradbadmin-relay/audit.log
//   origins = https://db.example.com   # pages allowed to connect; empty: any
//   plain_ok = false               # listen beyond loopback without TLS
//
//   [connection crm]
//   driver = postgresql
//   host = db.internal
//   port = 5432
//   database = crm
//   user = crm_app
//   password_secret = crm          # in the relay's vault (--set-secret crm)
//   tls = verify-full
//   read_only = false
//
//   [user anna]
//   token_sha256 = 9f86d0...       # ultradbadmin-relay --hash-token
//   crm = write                    # or read; a connection not named: none
//
//   [limits]
//   rows_per_page = 1000
//   rows_per_result = 100000
//   requests_per_minute = 1200
//   sessions_per_user = 16
//
// A line is "key = value"; '#' and ';' start a comment; a value may not hold
// a password - the vault does.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabase.h>

#include <map>
#include <string>
#include <vector>

namespace UltraDBAdminRelay {

struct OfferedConnection {
    std::string name;
    UltraDbConnectionConfig config;
};

struct RelayConfig {
    std::string listenHost = "127.0.0.1";
    int listenPort = 8765;
    std::string path = "/relay";
    std::string vaultDirectory;
    std::string auditFile;
    std::vector<std::string> origins;
    bool plainOk = false;
    std::vector<OfferedConnection> connections;
    std::vector<UltraDbRelayUser> users;
    UltraDbRelayLimits limits;
};

// Reads `file`; false with `error` (a line number and what is wrong).
bool LoadRelayConfig(const std::string& file, RelayConfig& out, std::string& error);

// The same from text, for the tests.
bool ParseRelayConfig(const std::string& text, RelayConfig& out, std::string& error);

// Is `host` this computer only: 127.x, ::1, localhost?
bool IsLoopback(const std::string& host);

} // namespace UltraDBAdminRelay
