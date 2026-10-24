// Apps/UltraDBAdmin/relay/RelayConfig.h
// The relay's configuration file: an INI file an administrator writes.
//
//   listen = 127.0.0.1:8765        # loopback: TLS is the proxy's in front
//   path = /relay                  # the WebSocket's path
//   vault = /var/lib/ultradbadmin-relay/vault
//   audit = /var/log/ultradbadmin-relay/audit.log
//   origins = https://db.example.com   # pages allowed to connect; empty: any
//   plain_ok = false               # listen beyond loopback without TLS
//   public_url = https://db.example.com/relay   # how browsers reach it (the sign-in's return)
//
//   [signin]                       # OpenID Connect (RelaySignIn.h); optional
//   label = Example SSO            # "Sign in with Example SSO"
//   issuer = https://sso.example.com/realms/staff
//   client_id = ultradbadmin
//   client_secret_secret = sso     # in the relay's vault (--set-secret sso); none: a public client
//   scopes = openid email
//   ticket_hours = 8               # how long a sign-in lasts
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
//   email = anna@example.com       # signed in: the provider's verified email...
//   subject = 248289761001         # ...or its subject ("sub"); any of the three
//   crm = write                    # or read; a connection not named: none
//
//   [limits]
//   rows_per_page = 1000
//   rows_per_result = 100000
//   requests_per_minute = 1200
//   sessions_per_user = 16
//   statement_timeout = 300        # seconds; 0: none
//
// A line is "key = value"; '#' and ';' start a comment; a value may not hold
// a password - the vault does.
// Version: 0.2.0 - [signin], public_url, a user's email or subject; statement_timeout
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

// The OpenID Connect provider users sign in with.
struct SignInConfig {
    bool enabled = false;
    std::string label = "your account";
    std::string issuer;
    std::string clientId;
    std::string clientSecretName;       // the vault's name for the client secret; "": public client
    std::string scopes = "openid email";
    int ticketHours = 8;
};

// Who a user is at the provider: a verified email, or the subject.
struct UserIdentity {
    std::string email;
    std::string subject;
};

struct RelayConfig {
    std::string listenHost = "127.0.0.1";
    int listenPort = 8765;
    std::string path = "/relay";
    std::string vaultDirectory;
    std::string auditFile;
    std::vector<std::string> origins;
    bool plainOk = false;
    std::string publicUrl;
    SignInConfig signIn;
    std::map<std::string, UserIdentity> identities;     // by user name
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

// The host of a URL ("https://a.example:8443/x" -> "a.example"), "" for none.
std::string UrlHost(const std::string& url);

} // namespace UltraDBAdminRelay
