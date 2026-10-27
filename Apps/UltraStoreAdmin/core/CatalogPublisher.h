// Apps/UltraStoreAdmin/core/CatalogPublisher.h
// From the admin database to what the store fetches: the catalog built with
// the next sequence, the packages' URLs pointing at the published files,
// the media copied beside the catalog, the signature over the exact bytes,
// and - before anything is called published - the store's own ParseCatalog
// and VerifyCatalogSignature run over the output, so what the admin backend
// writes is what the store reads.
//
// The publish folder is what the web server serves:
//   <out>/catalog.json, catalog.json.sig    the signed catalog
//   <out>/media/<id>/<file>                 icons and screenshots (relative URLs in the catalog)
//   <out>/packages/<sha256>.<format>        the packages everyone may fetch (free apps)
//   <out>/restricted/<sha256>.<format>      the packages of paid apps - the server hands
//                                           these out only through a buyer's short-lived
//                                           signed URL (the access scheme, see the README)
// Version: 0.3.0 - the codes in the catalog, their landing pages under go/
// Version: 0.2.0 - trusted_keys from the active keys
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "AdminDatabase.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraStoreAdmin {

struct PublishOptions {
    std::string outDirUtf8;          // the publish folder
    std::string catalogBaseUrl;      // "https://store.example/v1"; empty: the setting catalog.base_url
    std::string packagesBaseUrl;     // where packages/ and restricted/ are served; empty: the catalog's
    std::string mediaRootUtf8;       // what the catalog's relative media paths ("media/<id>/<file>") are
                                     // relative to: the admin store folder; empty: media is not copied
    bool copyPackages = true;        // false: the catalog points at the files, nothing is copied (a dry run)
    std::string generatedAt;         // ISO 8601; empty: now
    uint64_t sequence = 0;           // 0: the database's next
};

struct PublishReport {
    uint64_t sequence = 0;
    std::string catalogPath;         // UTF-8
    std::string signaturePath;
    std::string publicKeyHex;
    size_t apps = 0, components = 0, packagesCopied = 0, mediaCopied = 0;
    size_t codePages = 0;            // go/<CODE>.html written, one per code in the catalog
    std::vector<std::string> warnings;
};

// The catalog as the database describes it, with the packages' URLs laid out
// for the publish folder (no files touched, nothing signed). Every app and
// component the database holds is included; what has no release yet is
// listed in the warnings and left out, as the store would refuse it.
Result<Catalog> BuildCatalog(const AdminDatabase& db, const PublishOptions& options, std::vector<std::string>* warnings = nullptr);

// Build, write, copy, sign with the key (its seed opened with the
// passphrase), verify, and only then record the sequence in the database.
Result<PublishReport> Publish(AdminDatabase& db, int64_t keyId, const std::string& passphrase, const PublishOptions& options);

// The published name of a package file: "packages/<sha256>.<format>" or
// "restricted/<sha256>.<format>".
std::string PublishedPackagePath(const PackageFile& file);

} // namespace UltraStoreAdmin
