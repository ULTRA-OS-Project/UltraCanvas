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

// The name a spec is registered under in UltraDatabase.
std::string RegisteredName(const ConnectionSpec& spec);

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

    bool Remove(const std::string& id, std::string& error);

    // Register (or re-register) the spec with UltraDatabase, so its name can
    // be queried. Does not open it: the first query does.
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
