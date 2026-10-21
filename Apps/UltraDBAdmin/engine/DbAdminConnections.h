// Apps/UltraDBAdmin/engine/DbAdminConnections.h
// The connections UltraDBAdmin knows: kept in connections.json in its data
// directory, never with a password in it. A password goes into the app's own
// vault (UltraVault::DeviceKeyVault, encrypted at rest) under a key made from
// the connection's id, and the connection hands UltraDatabase only that key -
// the PostgreSQL driver reads it from the vault when it connects.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <UltraDatabase/UltraDatabase.h>

#include <memory>
#include <string>
#include <vector>

namespace UltraVault { class DeviceKeyVault; }

namespace UltraDBAdmin {

struct ConnectionSpec {
    std::string id;                 // generated; ASCII, so it can name a vault key
    std::string name;               // what the tree shows
    std::string driver = "sqlite";  // "sqlite" or "postgresql"
    std::string database;           // SQLite: the file; PostgreSQL: the database
    std::string host;               // PostgreSQL only; a socket directory works too
    int         port = 0;           // 0: the driver's default
    std::string user;
    bool        hasPassword = false;   // a password is in the vault for this id
    UltraDbTls  tls = UltraDbTls::VerifyFull;
    bool        readOnly = false;
    bool        production = false;    // shown in red, so a slip is visible

    bool IsSqlite() const { return driver == "sqlite" || driver == "sqlite3"; }
};

// The name a spec is registered under in UltraDatabase: the session the
// tree, Browse and Structure use.
std::string RegisteredName(const ConnectionSpec& spec);

// A second session to the same database, the SQL console's own. A script
// that runs for minutes holds its session (the drivers serialise each
// connection), so on a session of its own it never holds up the tree or a
// Browse page - and Stop cancels exactly the console's statement.
std::string ConsoleName(const ConnectionSpec& spec);

// Register (or re-register) one session of `spec` under `name` with
// UltraDatabase. Replacing a registration drops the session it held, with
// the connection inside it: only when nothing is using that session.
UltraDbResult RegisterSession(const ConnectionSpec& spec, const std::string& name);

// A one-line description for the tree's tooltip and the window title:
// "notes.db" or "alice@db.example.com:5432/crm".
std::string Describe(const ConnectionSpec& spec);

class ConnectionStore {
public:
    ConnectionStore();
    ~ConnectionStore();

    // Read `directory`/connections.json and open the vault in
    // `directory`/vault. A missing file is an empty list, not an error.
    bool Open(const std::string& directory, std::string& error);

    // Kept in memory only, with an in-memory vault - for tests.
    void OpenInMemory();

    const std::vector<ConnectionSpec>& All() const { return specs_; }
    const ConnectionSpec* Find(const std::string& id) const;

    // Add (spec.id empty) or replace (same id) a connection and save the list.
    // `password` is stored in the vault when not empty; `clearPassword`
    // removes a stored one. Returns the id.
    std::string Put(ConnectionSpec spec, const std::string& password, bool clearPassword,
                    std::string& error);

    // Forget a connection and its stored password. Its sessions stay
    // registered: the caller closes them (UltraDb_CloseConnection) once
    // nothing is using them.
    bool Remove(const std::string& id, std::string& error);

    // Register (or re-register) the spec with UltraDatabase under both its
    // names (RegisteredName, ConsoleName), so they can be queried. Opens
    // neither: the first query does. Re-registering drops open sessions (see
    // RegisterSession): call it when neither is in use.
    UltraDbResult Register(const ConnectionSpec& spec) const;

    // A new id: "c" followed by 16 hex digits of randomness.
    static std::string NewId();

private:
    bool Save(std::string& error) const;

    std::string file_;              // empty: in memory
    std::vector<ConnectionSpec> specs_;
    std::unique_ptr<UltraVault::DeviceKeyVault> vault_;
    bool memoryVault_ = false;
};

} // namespace UltraDBAdmin
