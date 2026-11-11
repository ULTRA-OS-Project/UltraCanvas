// Apps/UltraStoreAdmin/core/CatalogPublisher.cpp
// Version: 0.7.0 - ultrastore.json: the store's own catalog, signed beside the catalog
// Version: 0.6.0 - the mark for the pages under go/
// Version: 0.5.0 - a restricted package on an app with a trial
// Version: 0.4.0 - payment_url and licence_key from the settings
// Version: 0.3.0 - the codes, their landing pages
// Version: 0.2.0 - trusted_keys from the active keys
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "CatalogPublisher.h"

#include "Affiliates.h"
#include "PageFrame.h"

#include "KeyVault.h"
#include "SelfUpdate.h"
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
    // The payment service the stores buy through, and its licence key.
    catalog.paymentUrl = TrimSlash(db.Setting("payment.url"));
    catalog.licenceKey = db.Setting("payment.licence_key");
    if (!catalog.paymentUrl.empty() && catalog.licenceKey.empty()) warn.push_back("payment.url is set but payment.licence_key is not: the store cannot verify the licences");
    // Every active key may sign the next catalog (schema 2's trusted_keys):
    // a new key is announced by the first publish after it is made and
    // signs from the next one on; a retired key drops out of the list.
    const auto keys = db.Keys();
    if (!keys) return R::Failure(keys.error.code, keys.error.message, keys.error.detail);
    for (const KeyRecord& k : keys.Value()) {
        if (k.Active()) catalog.trustedKeys.push_back(k.publicKeyHex);
    }
    std::sort(catalog.trustedKeys.begin(), catalog.trustedKeys.end());
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
                if (file.Value().access == "restricted" && c.pricing.trialDays > 0) {
                    warn.push_back(c.id + " " + r.version + " " + p.target.Describe() + ": restricted package on an app with a " +
                                   std::to_string(c.pricing.trialDays) + "-day trial: Try installs the same package and will fail without a licence; make it public");
                }
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
    // The codes in use: every enabled code whose affiliate, when it has one,
    // is enabled too. What a partner earns stays here; the catalog carries
    // only what the store needs to show the discount and name the partner.
    const auto codes = db.Codes();
    if (!codes) return R::Failure(codes.error.code, codes.error.message, codes.error.detail);
    for (const CodeRecord& record : codes.Value()) {
        if (!record.enabled) continue;
        if (!record.code.affiliate.empty()) {
            const auto affiliate = db.GetAffiliate(record.code.affiliate);
            if (!affiliate || !affiliate.Value().enabled) {
                warn.push_back("code " + record.code.code + ": its affiliate " + record.code.affiliate + (affiliate ? " is switched off" : " does not exist") + ", left out");
                continue;
            }
        }
        for (const std::string& appId : record.code.apps) {
            bool known = false;
            for (const Component& c : catalog.apps) if (c.id == appId) known = true;
            if (!known) warn.push_back("code " + record.code.code + ": names " + appId + ", which the catalog does not list");
        }
        catalog.codes.push_back(record.code);
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
        // The mark the pages under go/ show (PageFrame.h), beside them in media/.
        if (const auto marks = CopyPageMarks(options.outDirUtf8); !marks) return R::Failure(marks.error.code, marks.error.message, marks.error.detail);
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
    // The pages beside the catalog, each a folder with an index.html so the
    // extension-less link (<base>/go/<CODE>, <base>/join/<affiliate>) is
    // served by every static web server without a rewrite rule:
    //   go/<CODE>         a code's landing page - what it gives, the apps, the
    //                     ultrastore:// link that opens the store with it
    //   join/<affiliate>  a partner's join page - who invites, what a partner
    //                     earns, the id to name when signing up, the store's
    //                     contact (store.contact)
    // The uploader never deletes on the server (a store half-way through a
    // download needs its file), so a page once published stays reachable.
    // The database remembers every page ever published (RememberPage), and
    // each publish writes a page for every one of them: the current page
    // for a code in the catalog or a partner in the programme, and for the
    // rest - a code switched off or whose partner was, a partner switched
    // off, a code or a partner removed outright - a page that says so (no
    // longer valid, this invitation is closed), which the next upload puts
    // in place of the stale one.
    const std::string pagesBase = TrimSlash(options.catalogBaseUrl.empty() ? db.Setting("catalog.base_url") : options.catalogBaseUrl);
    const std::string storeContact = db.Setting("store.contact");
    std::vector<PublishedPage> written;
    auto writePage = [&](const std::string& relative, const std::string& kind, const std::string& html) -> bool {
        const fs::path folder = out / UltraCanvas::PathFromUtf8(relative);
        fs::create_directories(folder, ec);
        if (ec) { error = "cannot create " + UltraCanvas::PathToUtf8(folder) + ": " + ec.message(); return false; }
        if (!WriteFileAtomically(folder / "index.html", html, error)) return false;
        written.push_back({relative, kind, ""});
        return true;
    };
    auto alreadyWritten = [&](const std::string& relative) {
        for (const PublishedPage& w : written) if (w.path == relative) return true;
        return false;
    };
    for (const UltraStore::PromoCode& code : catalog.codes) {
        if (!writePage("go/" + UltraStore::NormalizeCode(code.code), "code", CodeLandingPage(code, catalog.apps, pagesBase))) return R::Failure(ErrorCode::IoError, error);
        ++report.codePages;
    }
    if (const auto codes = db.Codes(); codes) {
        for (const CodeRecord& record : codes.Value()) {
            if (catalog.FindCode(record.code.code) != nullptr) continue;
            if (!writePage("go/" + UltraStore::NormalizeCode(record.code.code), "code", RetiredCodePage(record.code.code, pagesBase))) return R::Failure(ErrorCode::IoError, error);
            ++report.retiredPages;
        }
    }
    if (const auto affiliates = db.Affiliates(); affiliates) {
        for (const Affiliate& partner : affiliates.Value()) {
            if (!writePage("join/" + partner.id, "join", JoinPage(partner, storeContact, pagesBase))) return R::Failure(ErrorCode::IoError, error);
            if (partner.enabled) ++report.joinPages; else ++report.retiredPages;
        }
    }
    // What was published once and has no record any more: a retired page.
    const auto remembered = db.PublishedPages();
    if (!remembered) return R::Failure(remembered.error.code, remembered.error.message, remembered.error.detail);
    for (const PublishedPage& page : remembered.Value()) {
        if (alreadyWritten(page.path)) continue;
        const std::string name = page.path.substr(page.path.rfind('/') + 1);
        std::string html;
        if (page.kind == "code") html = RetiredCodePage(name, pagesBase);
        else if (page.kind == "join") {
            Affiliate gone;
            gone.id = name;
            gone.enabled = false;
            html = JoinPage(gone, storeContact, pagesBase);
        } else continue;
        if (!writePage(page.path, page.kind, html)) return R::Failure(ErrorCode::IoError, error);
        ++report.retiredPages;
    }
    for (const PublishedPage& page : written) {
        if (const auto r = db.RememberPage(page.path, page.kind); !r) return R::Failure(r.error.code, r.error.message, r.error.detail);
    }

    // The catalog and its signature over those exact bytes.
    const std::string bytes = UltraStore::SerializeCatalog(catalog);
    const auto signature = UltraStore::SignCatalog(bytes, seed.Value());
    // The store's own catalog, signed with the same seed while it is open:
    // UltraStore's entry and what it runs on, at the schema every store
    // reads (UltraStore::kSelfCatalogSchema).
    std::string selfBytes, selfSignatureText;
    bool selfSigned = true;
    UltraStore::Error selfError;
    if (const auto self = UltraStore::ExtractSelfCatalog(catalog)) {
        selfBytes = UltraStore::SerializeCatalog(self.Value());
        const auto selfSignature = UltraStore::SignCatalog(selfBytes, seed.Value());
        if (selfSignature) selfSignatureText = UltraStore::ToHex(selfSignature.Value()) + "\n";
        else { selfSigned = false; selfError = selfSignature.error; }
    } else {
        report.warnings.push_back(std::string("the catalog does not list UltraStore (") + UltraStore::kStoreAppId +
                                  "): no ultrastore.json is written, so the setup program and the store's own update have nothing to fetch");
    }
    UltraCrypt_SecureZero(seed.Value().data(), seed.Value().size());
    if (!signature) return R::Failure(signature.error.code, signature.error.message, signature.error.detail);
    if (!selfSigned) return R::Failure(selfError.code, selfError.message, selfError.detail);
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

    // The store's own catalog, checked the same way and written first: a
    // store never finds a catalog that names a newer store than
    // ultrastore.json offers.
    if (!selfBytes.empty()) {
        std::vector<uint8_t> selfSignature;
        if (!UltraStore::ParseSignatureFile(selfSignatureText, selfSignature)) return R::Failure(ErrorCode::Internal, "The store's own signature file would not read back");
        if (const auto verified = UltraStore::VerifyCatalogSignature(selfBytes, selfSignature, {publicKey}); !verified) {
            return R::Failure(ErrorCode::SignatureInvalid, "The store's own catalog does not verify with the key's public half", verified.error.message);
        }
        const auto selfParsed = UltraStore::ParseCatalog(selfBytes);
        if (!selfParsed) return R::Failure(selfParsed.error.code, selfParsed.error.message, selfParsed.error.detail);
        if (selfParsed.Value().schema > UltraStore::kSelfCatalogSchema) {
            return R::Failure(ErrorCode::Internal, "The store's own catalog was written with a schema older stores cannot read",
                              "schema " + std::to_string(selfParsed.Value().schema));
        }
        const fs::path selfPath = out / UltraStore::kSelfCatalogFile;
        const fs::path selfSignaturePath = out / UltraCanvas::PathFromUtf8(std::string(UltraStore::kSelfCatalogFile) + ".sig");
        if (!WriteFileAtomically(selfPath, selfBytes, error)) return R::Failure(ErrorCode::IoError, error);
        if (!WriteFileAtomically(selfSignaturePath, selfSignatureText, error)) return R::Failure(ErrorCode::IoError, error);
        report.selfCatalogPath = UltraCanvas::PathToUtf8(selfPath);
        report.selfSignaturePath = UltraCanvas::PathToUtf8(selfSignaturePath);
    }

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
