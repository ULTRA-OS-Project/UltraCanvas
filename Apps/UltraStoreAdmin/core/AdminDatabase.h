// Apps/UltraStoreAdmin/core/AdminDatabase.h
// What the admin backend keeps: the publishers, every app and component with
// all its settings, the files behind the packages, the signing keys (the seed
// sealed with a passphrase), the store's settings and the catalog's sequence.
// On UltraDatabase (SQLite), one file.
//
// A component is kept whole, as the JSON object the catalog writes
// (Catalog.h's SerializeComponent), beside the few columns the lists need:
// the catalog's component object *is* the admin's settings record, so what
// the admin backend keeps is by construction what the store reads, and a
// setting the catalog grows is kept without a schema change. The tables that
// are queried on their own - package files by hash, keys, settings - are
// tables.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "UltraStoreTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraStoreAdmin {

using UltraStore::Catalog;
using UltraStore::Component;
using UltraStore::ComponentKind;
using UltraStore::ErrorCode;
using UltraStore::Publisher;
using UltraStore::Result;

inline constexpr int kAdminSchemaVersion = 1;

// A signing key: the public half in the open, the seed sealed (KeyVault.h).
struct KeyRecord {
    int64_t id = 0;
    std::string name;
    std::string publicKeyHex;
    std::string createdAt;
    std::string retiredAt;      // empty while the key signs
    bool Active() const { return retiredAt.empty(); }
};

// The file behind a package, by the package's SHA-256: where it is in the
// package store and whether it is served to everyone ("public") or only to
// the buyers ("restricted", the packages server's access scheme).
struct PackageFile {
    std::string sha256;
    std::string file;           // UTF-8, in the package store
    std::string access = "public";
    uint64_t size = 0;
    std::string format;
};

class AdminDatabase {
public:
    AdminDatabase() = default;
    ~AdminDatabase();
    AdminDatabase(const AdminDatabase&) = delete;
    AdminDatabase& operator=(const AdminDatabase&) = delete;

    Result<void> Open(const std::string& databasePathUtf8);
    void Close();
    bool IsOpen() const { return !connection_.empty(); }

    // ----- settings (catalog base URL, packages base URL, languages, ...) -----
    std::string Setting(const std::string& key, const std::string& fallback = {}) const;
    Result<void> SetSetting(const std::string& key, const std::string& value);
    Result<std::vector<std::pair<std::string, std::string>>> Settings() const;

    // ----- publishers -----
    Result<void> PutPublisher(const Publisher& publisher);
    Result<void> RemovePublisher(const std::string& id);
    Result<std::vector<Publisher>> Publishers() const;

    // ----- apps and components, whole -----
    Result<void> PutComponent(const Component& component);     // adds or replaces
    Result<Component> GetComponent(const std::string& id) const;
    Result<std::vector<Component>> Components() const;         // apps and components, by id
    Result<void> RemoveComponent(const std::string& id);

    // ----- the files behind the packages -----
    Result<void> PutPackageFile(const PackageFile& file);
    Result<PackageFile> GetPackageFile(const std::string& sha256) const;
    Result<std::vector<PackageFile>> PackageFiles() const;
    Result<void> RemovePackageFile(const std::string& sha256);

    // ----- signing keys -----
    Result<KeyRecord> AddKey(const std::string& name, const std::string& publicKeyHex,
                             const std::vector<uint8_t>& sealedSeed);
    Result<std::vector<KeyRecord>> Keys() const;
    Result<KeyRecord> GetKey(int64_t id) const;
    Result<std::vector<uint8_t>> SealedSeed(int64_t id) const;
    Result<void> RetireKey(int64_t id);

    // ----- the catalog's sequence -----
    uint64_t Sequence() const;                 // the last published; 0 before the first
    Result<void> SetSequence(uint64_t sequence);

    // ----- everything at once, for the catalog -----
    Result<void> BeginTransaction();
    Result<void> Commit();
    Result<void> Rollback();

    const std::string& Path() const { return path_; }

private:
    std::string connection_;
    std::string path_;
};

// "2026-10-27T10:00:00Z".
std::string NowIso8601();

} // namespace UltraStoreAdmin
