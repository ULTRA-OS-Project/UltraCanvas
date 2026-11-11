// Apps/UltraStore/core/Registry.h
// What is installed, remembered in <root>/registry.sqlite on UltraDatabase:
// one row per installed app or component version (PackageInstaller.h,
// InstalledRecord). Two versions of one runtime are two rows; an app's
// `current` row is the one the launcher starts.
//
// The table:
//   installed(kind, id, version, folder, entry, sha256, installed_at,
//             channel, depends, current)   primary key (id, version)
//   meta(key, value)                        schema_version
// `depends` is the exact versions the row was installed with, written as
// "id@version id@version", which the garbage collector follows.
//
// Version: 0.2.0 - licences (schema 2)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "PackageInstaller.h"

#include <string>
#include <vector>

namespace UltraStore {

// A purchase the store made through the payment service: the licence the
// app checks, and the order it came from (its secret signs the downloads
// of the app's restricted packages). One per app.
struct LicenceRecord {
    std::string app;
    std::string licenceId;
    std::string orderId;
    std::string orderSecret;
    std::string token;        // "ULIC1.<payload>.<signature>"
    std::string issuedAt;     // "YYYY-MM-DD"
    std::string amount, currency;
    int receiptNo = 0;
};

class SqliteRegistry final : public IInstallRegistry {
public:
    SqliteRegistry() = default;
    ~SqliteRegistry() override;
    SqliteRegistry(const SqliteRegistry&) = delete;
    SqliteRegistry& operator=(const SqliteRegistry&) = delete;

    // Opens (and creates) the database at this UTF-8 path and brings its
    // schema to the current version. A database written by a newer store is
    // refused (SchemaTooNew): an old binary must not write into columns it
    // does not know.
    Result<void> Open(const std::string& databasePathUtf8);
    void Close();
    bool IsOpen() const { return !connection_.empty(); }

    Result<std::vector<InstalledRecord>> All() const override;
    Result<std::vector<InstalledRecord>> Of(std::string_view id) const override;
    Result<void> Add(const InstalledRecord& record) override;
    Result<void> Remove(std::string_view id, std::string_view version) override;
    Result<void> SetCurrent(std::string_view id, std::string_view version) override;
    InstalledVersions Versions() const override;

    // The one row of an app or component version, or NotFound.
    Result<InstalledRecord> Get(std::string_view id, std::string_view version) const;
    // The app's current row, or NotFound.
    Result<InstalledRecord> Current(std::string_view id) const;

    // ----- licences -----
    Result<void> PutLicence(const LicenceRecord& licence);     // adds or replaces the app's
    Result<LicenceRecord> GetLicence(std::string_view app) const;   // NotFound when the app was not bought
    Result<std::vector<LicenceRecord>> Licences() const;
    Result<void> RemoveLicence(std::string_view app);

    static constexpr int kSchemaVersion = 2;   // 2: licences

    // The UltraDatabase connection name while open (for a tool that wants
    // to query the same database), empty when closed.
    const std::string& ConnectionName() const { return connection_; }

    // "id@version id@version" <-> dependencies, for the depends column.
    static std::string JoinDepends(const std::vector<Dependency>& depends);
    static std::vector<Dependency> SplitDepends(const std::string& text);

private:
    std::string connection_;   // the UltraDatabase connection name; empty when closed
};

} // namespace UltraStore
