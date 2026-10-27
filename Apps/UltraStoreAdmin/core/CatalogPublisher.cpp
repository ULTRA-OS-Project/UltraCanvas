// Apps/UltraStoreAdmin/core/CatalogPublisher.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "CatalogPublisher.h"

#include "KeyVault.h"
#include "Verifier.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCrypt/UltraCryptCore.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace UltraStoreAdmin {

namespace {

std::string TrimSlash(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

bool IsAbsoluteUrl(const std::string& s) {
    return s.rfind("https://", 0) == 0 || s.rfind("http://", 0) == 0;
}

bool WriteFileAtomically(const fs::path& target, const std::string& bytes, std::string& error) {
    std::error_code ec;
    const fs::path temp = UltraCanvas::PathFromUtf8(UltraCanvas::PathToUtf8(target) + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) { error = "cannot write " + UltraCanvas::PathToUtf8(target); return false; }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) { error = "cannot write " + UltraCanvas::PathToUtf8(target); return false; }
    }
    fs::remove(target, ec);
    ec.clear();
    fs::rename(temp, target, ec);
    if (ec) { error = "cannot place " + UltraCanvas::PathToUtf8(target) + ": " + ec.message(); return false; }
    return true;
}

// Copies when the target is missing or differs in size; the hash names the file.
bool CopyIfNeeded(const fs::path& from, const fs::path& to, std::string& error) {
    std::error_code ec;
    if (fs::is_regular_file(to, ec) && fs::file_size(to, ec) == fs::file_size(from, ec)) return true;
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) { error = "cannot copy " + UltraCanvas::PathToUtf8(from) + " to " + UltraCanvas::PathToUtf8(to) + ": " + ec.message(); return false; }
    return true;
}

} // namespace

std::string PublishedPackagePath(const PackageFile& file) {
    return (file.access == "restricted" ? "restricted/" : "packages/") + file.sha256 + "." + file.format;
}

Result<Catalog> BuildCatalog(const AdminDatabase& db, const PublishOptions& options, std::vector<std::string>* warnings) {
    using R = Result<Catalog>;
    std::vector<std::string> local;
    std::vector<std::string>& warn = warnings ? *warnings : local;
    const std::string catalogBase = TrimSlash(options.catalogBaseUrl.empty() ? db.Setting("catalog.base_url") : options.catalogBaseUrl);
    const std::string packagesBase = TrimSlash(options.packagesBaseUrl.empty() ? db.Setting("packages.base_url", catalogBase) : options.packagesBaseUrl);

    Catalog catalog;
    catalog.schema = UltraStore::kSupportedSchema;
    catalog.sequence = options.sequence ? options.sequence : db.Sequence() + 1;
    catalog.generated = options.generatedAt.empty() ? NowIso8601() : options.generatedAt;
    const auto publishers = db.Publishers();
    if (!publishers) return R::Failure(publishers.error.code, publishers.error.message, publishers.error.detail);
    catalog.publishers = publishers.Value();
    std::set<std::string> publisherIds;
    for (const Publisher& p : catalog.publishers) publisherIds.insert(p.id);

    const auto components = db.Components();
    if (!components) return R::Failure(components.error.code, components.error.message, components.error.detail);
    for (Component c : components.Value()) {
        if (c.releases.empty()) {
            warn.push_back(c.id + ": no release yet, left out");
            continue;
        }
        if (c.kind == ComponentKind::App && !c.publisher.empty() && !publisherIds.count(c.publisher)) {
            warn.push_back(c.id + ": publisher " + c.publisher + " is not in the database");
        }
        if (c.kind == ComponentKind::App && c.summary.Empty()) warn.push_back(c.id + ": no summary");
        // A package nothing serves - no file in the package store and no URL
        // of its own - is left out, and so is a release left without a
        // package and a component left without a release: the store would
        // refuse the catalog otherwise, and a warning names each.
        for (UltraStore::Release& r : c.releases) {
            std::vector<UltraStore::Package> served;
            for (UltraStore::Package& p : r.packages) {
                if (IsAbsoluteUrl(p.url) && p.sha256.size() == 64 && p.size) { served.push_back(p); continue; }   // hosted elsewhere, complete
                const auto file = db.GetPackageFile(p.sha256);
                if (!file) {
                    warn.push_back(c.id + " " + r.version + " " + p.target.Describe() + ": no file for hash " + p.sha256 + ", left out");
                    continue;
                }
                if (packagesBase.empty()) {
                    warn.push_back(c.id + " " + r.version + ": no packages base URL (settings packages.base_url or catalog.base_url), left out");
                    continue;
                }
                p.url = packagesBase + "/" + PublishedPackagePath(file.Value());
                p.size = file.Value().size;
                p.format = file.Value().format;
                served.push_back(p);
            }
            if (served.empty()) warn.push_back(c.id + " " + r.version + ": no package to serve, left out");
            r.packages = std::move(served);
        }
        c.releases.erase(std::remove_if(c.releases.begin(), c.releases.end(),
                                        [](const UltraStore::Release& r) { return r.packages.empty(); }), c.releases.end());
        if (c.releases.empty()) {
            warn.push_back(c.id + ": no release with a package, left out");
            continue;
        }
        if (c.kind == ComponentKind::App) catalog.apps.push_back(std::move(c));
        else catalog.components.push_back(std::move(c));
    }
    // What the store reads: the bytes must parse as a catalog before they
    // are offered to anyone.
    const auto check = UltraStore::ParseCatalog(UltraStore::SerializeCatalog(catalog));
    if (!check) return R::Failure(check.error.code, "The catalog would not read back: " + check.error.message, check.error.detail);
    return R::Succeed(std::move(catalog));
}

Result<PublishReport> Publish(AdminDatabase& db, int64_t keyId, const std::string& passphrase, const PublishOptions& options) {
    using R = Result<PublishReport>;
    if (options.outDirUtf8.empty()) return R::Failure(ErrorCode::InvalidArgument, "Publish needs an output folder");
    const auto key = db.GetKey(keyId);
    if (!key) return R::Failure(key.error.code, key.error.message, key.error.detail);
    if (!key.Value().Active()) return R::Failure(ErrorCode::InvalidArgument, "Key " + std::to_string(keyId) + " (" + key.Value().name + ") is retired");
    const auto sealedBytes = db.SealedSeed(keyId);
    if (!sealedBytes) return R::Failure(sealedBytes.error.code, sealedBytes.error.message, sealedBytes.error.detail);
    SealedSeed sealed;
    if (!SealedSeed::Parse(sealedBytes.Value(), sealed)) return R::Failure(ErrorCode::ParseError, "The key's sealed seed is malformed");
    auto seed = OpenSeed(sealed, passphrase);
    if (!seed) return R::Failure(seed.error.code, seed.error.message, seed.error.detail);

    PublishReport report;
    const auto built = BuildCatalog(db, options, &report.warnings);
    if (!built) return R::Failure(built.error.code, built.error.message, built.error.detail);
    const Catalog& catalog = built.Value();
    report.sequence = catalog.sequence;
    report.apps = catalog.apps.size();
    report.components = catalog.components.size();
    report.publicKeyHex = key.Value().publicKeyHex;

    std::error_code ec;
    const fs::path out = UltraCanvas::PathFromUtf8(options.outDirUtf8);
    fs::create_directories(out, ec);
    if (ec) return R::Failure(ErrorCode::IoError, "Cannot create " + options.outDirUtf8, ec.message());
    std::string error;

    // The packages and the media beside the catalog.
    if (options.copyPackages) {
        std::set<std::string> done;
        auto copyPackages = [&](const std::vector<Component>& list) -> bool {
            for (const Component& c : list) {
                for (const UltraStore::Release& r : c.releases) {
                    for (const UltraStore::Package& p : r.packages) {
                        if (done.count(p.sha256)) continue;
                        const auto file = db.GetPackageFile(p.sha256);
                        if (!file) continue;   // hosted elsewhere, or warned about already
                        const fs::path from = UltraCanvas::PathFromUtf8(file.Value().file);
                        if (!fs::is_regular_file(from, ec)) {
                            report.warnings.push_back(c.id + " " + r.version + ": the package file is missing: " + file.Value().file);
                            continue;
                        }
                        if (!CopyIfNeeded(from, out / UltraCanvas::PathFromUtf8(PublishedPackagePath(file.Value())), error)) return false;
                        done.insert(p.sha256);
                        ++report.packagesCopied;
                    }
                }
            }
            return true;
        };
        if (!copyPackages(catalog.apps) || !copyPackages(catalog.components)) return R::Failure(ErrorCode::IoError, error);
        if (!options.mediaRootUtf8.empty()) {
            const fs::path media = UltraCanvas::PathFromUtf8(options.mediaRootUtf8);
            auto copyMedia = [&](const Component& c, const std::string& relative) -> bool {
                if (relative.empty() || IsAbsoluteUrl(relative)) return true;
                const fs::path from = media / UltraCanvas::PathFromUtf8(relative);
                if (!fs::is_regular_file(from, ec)) {
                    report.warnings.push_back(c.id + ": the picture is missing: " + relative);
                    return true;
                }
                if (!CopyIfNeeded(from, out / UltraCanvas::PathFromUtf8(relative), error)) return false;
                ++report.mediaCopied;
                return true;
            };
            for (const Component& c : catalog.apps) {
                if (!copyMedia(c, c.icon)) return R::Failure(ErrorCode::IoError, error);
                for (const std::string& s : c.screenshots) if (!copyMedia(c, s)) return R::Failure(ErrorCode::IoError, error);
            }
        }
    }

    // The catalog and its signature over those exact bytes.
    const std::string bytes = UltraStore::SerializeCatalog(catalog);
    const auto signature = UltraStore::SignCatalog(bytes, seed.Value());
    UltraCrypt_SecureZero(seed.Value().data(), seed.Value().size());
    if (!signature) return R::Failure(signature.error.code, signature.error.message, signature.error.detail);
    const std::string signatureText = UltraStore::ToHex(signature.Value()) + "\n";

    // Verified the way the store verifies before it is called published.
    std::vector<uint8_t> publicKey;
    if (!UltraStore::FromHex(key.Value().publicKeyHex, publicKey)) return R::Failure(ErrorCode::Internal, "The key's public half is not hex");
    std::vector<uint8_t> parsedSignature;
    if (!UltraStore::ParseSignatureFile(signatureText, parsedSignature)) return R::Failure(ErrorCode::Internal, "The signature file would not read back");
    if (const auto verified = UltraStore::VerifyCatalogSignature(bytes, parsedSignature, {publicKey}); !verified) {
        return R::Failure(ErrorCode::SignatureInvalid, "The signature does not verify with the key's public half", verified.error.message);
    }
    if (const auto parsed = UltraStore::ParseCatalog(bytes); !parsed) return R::Failure(parsed.error.code, parsed.error.message, parsed.error.detail);

    const fs::path catalogPath = out / "catalog.json";
    const fs::path signaturePath = out / "catalog.json.sig";
    if (!WriteFileAtomically(catalogPath, bytes, error)) return R::Failure(ErrorCode::IoError, error);
    if (!WriteFileAtomically(signaturePath, signatureText, error)) return R::Failure(ErrorCode::IoError, error);
    report.catalogPath = UltraCanvas::PathToUtf8(catalogPath);
    report.signaturePath = UltraCanvas::PathToUtf8(signaturePath);

    if (const auto seq = db.SetSequence(catalog.sequence); !seq) return R::Failure(seq.error.code, seq.error.message, seq.error.detail);
    if (const auto at = db.SetSetting("catalog.published_at", catalog.generated); !at) return R::Failure(at.error.code, at.error.message, at.error.detail);
    return R::Succeed(std::move(report));
}

} // namespace UltraStoreAdmin
