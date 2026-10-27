// Apps/UltraStore/cli/Commands.cpp
// ultrastore - the store from the command line. The same program opens the
// window when it is started without a command (ui/main.cpp; one program, as
// UltraFIBU and UltraPress are, because a case-insensitive file system cannot
// tell `ultrastore` from `UltraStore`). The window's options are in the usage
// below so `ultrastore --help` describes the whole program.
//
//   ultrastore platform                       what this machine is
//   ultrastore list <catalog.json> [--channel beta] [--lang de]
//                                             the apps, with the package this machine gets
//   ultrastore resolve <catalog.json> <app-id> [--installed id@version ...]
//                                             the install plan for an app
//   ultrastore verify <catalog.json> <catalog.json.sig> --key <hex> [--key <hex>]
//                                             checks the signature and parses
//   ultrastore fetch <base-url> --key <hex> [--cache <dir>]
//                                             refreshes the cache from a server
//   ultrastore keygen                         a new signing seed and its public key
//   ultrastore pubkey <seed-hex>              the public key of a seed
//   ultrastore sign <catalog.json> <seed-hex> [-o catalog.json.sig]
//                                             signs a catalog (the admin side)
//   ultrastore sha256 <file>                  a package's hash for the catalog
//   ultrastore install <catalog.json> <app-id> [--channel beta] [--root <dir>]
//                                             downloads, verifies and unpacks the app
//                                             with the components it needs
//   ultrastore update <catalog.json> [<app-id>] [--root <dir>]
//                                             moves an app (or every app) to its newest release
//   ultrastore remove <app-id> [--root <dir>] removes an app and what only it needed
//   ultrastore installed [--root <dir>]       what is installed
//   ultrastore gc [--root <dir>]              removes components nothing depends on
//   ultrastore --version | --help
//
//   ultrastore [--catalog <catalog.json>] [--root <dir>] [--channel beta]
//                                             the window (no command)
//
// --root is the store's folder; without it UltraCanvasLocalDataFolderRoot()/UltraStore.
//
// Version: 0.4.0 - buy, licences
// Version: 0.3.0 - code
// Version: 0.3.0 - Run() and Usage() for ui/main.cpp; the window's options in the usage;
//                  FormatBytes shared with the window (StoreQuery)
// Version: 0.2.0 - install, update, remove, installed, gc (UltraInstaller)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Commands.h"

#include "Catalog.h"
#include "CatalogClient.h"
#include "PackageResolver.h"
#include "PlatformInfo.h"
#include "Purchase.h"
#include "StoreEngine.h"
#include "StoreQuery.h"
#include "StoreSettings.h"
#include "Verifier.h"

#include "UltraCanvasDataFolder.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"

#include <cstdio>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef ULTRASTORE_VERSION
#error "ULTRASTORE_VERSION must be passed by CMake (from Docs/UltraStore/CHANGELOG.md)"
#endif

using namespace UltraStore;

int UltraStore::Cli::Usage(FILE* to) {
    std::fprintf(to,
        "ultrastore %s - the ULTRA OS app store\n"
        "\n"
        "  ultrastore [--catalog <catalog.json>] [--root <dir>] [--channel stable|beta]\n"
        "                                     the window; --catalog shows a local catalog instead of the servers'\n"
        "\n"
        "  ultrastore platform\n"
        "  ultrastore list <catalog.json> [--channel stable|beta] [--lang en|de]\n"
        "  ultrastore resolve <catalog.json> <app-id> [--channel beta] [--installed <id>@<version> ...]\n"
        "  ultrastore verify <catalog.json> <catalog.json.sig> --key <hex> [--key <hex>]\n"
        "  ultrastore fetch <base-url> --key <hex> [--key <hex>] [--cache <dir>]\n"
        "  ultrastore keygen\n"
        "  ultrastore pubkey <seed-hex>\n"
        "  ultrastore sign <catalog.json> <seed-hex> [-o <catalog.json.sig>]\n"
        "  ultrastore sha256 <file>\n"
        "  ultrastore install <catalog.json> <app-id> [--channel stable|beta] [--root <dir>]\n"
        "  ultrastore update <catalog.json> [<app-id>] [--root <dir>]\n"
        "  ultrastore remove <app-id> [--root <dir>]\n"
        "  ultrastore installed [--root <dir>]\n"
        "  ultrastore gc [--root <dir>]\n"
        "  ultrastore buy <catalog.json> <app-id> [--code <CODE>] [--root <dir>]\n"
        "                                     through the catalog's payment service: the browser pays, the licence is kept\n"
        "  ultrastore licences [--root <dir>]  the apps bought here\n"
        "  ultrastore code [<CODE> | clear] [--cache <dir>]\n"
        "                                     the discount or partner code in force (30 days), checked against the cached catalog\n"
        "  ultrastore ultrastore://code/<CODE>  the same, from a QR code or a page: opens the window with it\n"
        "  ultrastore --version | --help\n",
        ULTRASTORE_VERSION);
    return to == stdout ? 0 : 2;
}

namespace {

int Usage(FILE* to) { return UltraStore::Cli::Usage(to); }

bool ReadFile(const std::string& path, std::string& out) {
    std::ifstream in(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

bool WriteFile(const std::string& path, const std::string& bytes) {
    std::ofstream out(UltraCanvas::PathFromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

int Fail(const Error& e) {
    std::fprintf(stderr, "ultrastore: %s", e.message.c_str());
    if (!e.detail.empty()) std::fprintf(stderr, " (%s)", e.detail.c_str());
    std::fprintf(stderr, " [%s]\n", ErrorCodeName(e.code).c_str());
    return 1;
}

int Fail(const std::string& message) {
    std::fprintf(stderr, "ultrastore: %s\n", message.c_str());
    return 1;
}

std::string Option(const std::vector<std::string>& args, const std::string& name, const std::string& fallback = {}) {
    for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == name) return args[i + 1];
    return fallback;
}

std::vector<std::string> Options(const std::vector<std::string>& args, const std::string& name) {
    std::vector<std::string> out;
    for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == name) out.push_back(args[i + 1]);
    return out;
}

// The positional arguments: everything that is not an option or its value.
std::vector<std::string> Positional(const std::vector<std::string>& args) {
    std::vector<std::string> out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i].size() > 1 && args[i][0] == '-') { ++i; continue; }
        out.push_back(args[i]);
    }
    return out;
}

bool KeysFrom(const std::vector<std::string>& args, PublicKeys& keys) {
    for (const std::string& hex : Options(args, "--key")) {
        std::vector<uint8_t> key;
        if (!FromHex(hex, key) || key.size() != 32) {
            std::fprintf(stderr, "ultrastore: --key wants 64 hex characters, not \"%s\"\n", hex.c_str());
            return false;
        }
        keys.push_back(std::move(key));
    }
    return true;
}

int CmdPlatform() {
    const PlatformInfo info = PlatformInfo::Detect();
    std::printf("%s\n", info.Describe().c_str());
    std::printf("os:        %s\narch:      %s\nversion:   %s\n", OSName(info.target.os).c_str(),
                ArchName(info.target.arch).c_str(), info.osVersion.c_str());
    if (!info.distroId.empty()) std::printf("distro:    %s\n", info.distroId.c_str());
    for (const std::string& v : info.target.variants) std::printf("variant:   %s\n", v.c_str());
    std::printf("x86_64:    %s\n", info.canRunX86_64 ? "runs" : "does not run");
    return 0;
}

int CmdList(const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    std::string text;
    if (!ReadFile(pos[1], text)) return Fail("cannot read " + pos[1]);
    const Result<Catalog> parsed = ParseCatalog(text);
    if (!parsed) return Fail(parsed.error);
    const Catalog& catalog = parsed.Value();
    const std::string channel = Option(args, "--channel", "stable");
    const std::string lang = Option(args, "--lang", "en");
    const PlatformInfo platform = PlatformInfo::Detect();
    std::printf("catalog schema %d, sequence %llu, %zu app(s), %zu component(s)\n", catalog.schema,
                static_cast<unsigned long long>(catalog.sequence), catalog.apps.size(), catalog.components.size());
    for (const Component& app : catalog.apps) {
        const Resolution res = PackageResolver::Resolve(app, platform, channel);
        std::printf("\n%s  (%s)\n", app.name.c_str(), app.id.c_str());
        if (!app.summary.Empty()) std::printf("  %s\n", app.summary.Text(lang).c_str());
        if (res.Available()) {
            std::printf("  %s for this machine: %s %s, %s%s\n", res.release->version.c_str(),
                        OSName(res.package->target.os).c_str(), ArchName(res.package->target.arch).c_str(),
                        FormatBytes(res.package->size).c_str(),
                        res.kind == MatchKind::Emulated ? " (runs under Rosetta)" : "");
            for (const Dependency& d : res.release->depends) std::printf("  needs %s %s\n", d.id.c_str(), d.version.c_str());
        } else if (res.release) {
            std::printf("  not available for your system; available for %s\n",
                        PackageResolver::SupportedSystems(*res.release).c_str());
        } else {
            std::printf("  no release on the %s channel\n", channel.c_str());
        }
    }
    return 0;
}

int CmdResolve(const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 3) return Usage(stderr);
    std::string text;
    if (!ReadFile(pos[1], text)) return Fail("cannot read " + pos[1]);
    const Result<Catalog> parsed = ParseCatalog(text);
    if (!parsed) return Fail(parsed.error);
    const Catalog& catalog = parsed.Value();
    const Component* app = catalog.FindApp(pos[2]);
    if (!app) return Fail("no app " + pos[2] + " in the catalog");
    const PlatformInfo platform = PlatformInfo::Detect();
    const Resolution res = PackageResolver::Resolve(*app, platform, Option(args, "--channel", "stable"));
    if (!res.Available()) {
        return Fail(app->name + " is not available for your system" +
                    (res.release ? "; available for " + PackageResolver::SupportedSystems(*res.release) : ""));
    }
    InstalledVersions installed;
    for (const std::string& entry : Options(args, "--installed")) {
        const size_t at = entry.find('@');
        if (at == std::string::npos) return Fail("--installed wants <id>@<version>, not " + entry);
        installed.entries.emplace_back(entry.substr(0, at), entry.substr(at + 1));
    }
    const Result<InstallPlan> plan = PackageResolver::Plan(catalog, *app, *res.release, platform, installed);
    if (!plan) return Fail(plan.error);
    for (const InstallStep& step : plan.Value().steps) {
        std::printf("%-9s %s %s", ComponentKindName(step.component->kind).c_str(), step.component->id.c_str(),
                    step.release->version.c_str());
        if (step.alreadyInstalled) std::printf("  (installed)\n");
        else std::printf("  %s  %s  %s\n", FormatBytes(step.package->size).c_str(),
                         MatchKindName(step.match).c_str(), step.package->url.c_str());
    }
    std::printf("to download: %s\n", FormatBytes(plan.Value().bytesToDownload).c_str());
    return 0;
}

int CmdVerify(const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 3) return Usage(stderr);
    PublicKeys keys;
    if (!KeysFrom(args, keys)) return 2;
    if (keys.empty()) return Fail("verify needs at least one --key");
    std::string catalogBytes, sigBytes;
    if (!ReadFile(pos[1], catalogBytes)) return Fail("cannot read " + pos[1]);
    if (!ReadFile(pos[2], sigBytes)) return Fail("cannot read " + pos[2]);
    std::vector<uint8_t> signature;
    if (!ParseSignatureFile(sigBytes, signature)) return Fail(pos[2] + " is not a signature");
    const Result<void> verified = VerifyCatalogSignature(catalogBytes, signature, keys);
    if (!verified) return Fail(verified.error);
    const Result<Catalog> parsed = ParseCatalog(catalogBytes);
    if (!parsed) return Fail(parsed.error);
    std::printf("signature ok; schema %d, sequence %llu, %zu app(s), %zu component(s)\n", parsed.Value().schema,
                static_cast<unsigned long long>(parsed.Value().sequence), parsed.Value().apps.size(),
                parsed.Value().components.size());
    return 0;
}

int CmdFetch(const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    CatalogClientConfig config;
    config.baseUrls.push_back(pos[1]);
    if (!KeysFrom(args, config.trustedKeys)) return 2;
    if (config.trustedKeys.empty()) return Fail("fetch needs at least one --key");
    config.cacheDir = Option(args, "--cache");
    if (config.cacheDir.empty()) {
        config.cacheDir = UltraCanvas::PathToUtf8(UltraCanvas::UltraCanvasLocalDataFolderRoot() / "UltraStore" / "cache");
    }
    CatalogClient client(config);
    const Result<Catalog> fetched = client.Refresh();
    if (!fetched) return Fail(fetched.error);
    const CatalogState state = client.State();
    std::printf("fetched sequence %llu from %s at %s into %s\n", static_cast<unsigned long long>(fetched.Value().sequence),
                state.fetchedFrom.c_str(), state.fetchedAt.c_str(), config.cacheDir.c_str());
    return 0;
}

int CmdKeygen() {
    std::vector<uint8_t> seed, pk;
    const Result<void> r = GenerateSigningKey(seed, pk);
    if (!r) return Fail(r.error);
    std::printf("seed:   %s\npublic: %s\n", ToHex(seed).c_str(), ToHex(pk).c_str());
    std::printf("Keep the seed where only the signer reads it (a vault); the public key goes into the store.\n");
    return 0;
}

int CmdPubkey(const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    std::vector<uint8_t> seed;
    if (!FromHex(pos[1], seed)) return Fail("the seed is not hex");
    const Result<std::vector<uint8_t>> pk = PublicKeyOfSeed(seed);
    if (!pk) return Fail(pk.error);
    std::printf("%s\n", ToHex(pk.Value()).c_str());
    return 0;
}

int CmdSign(const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 3) return Usage(stderr);
    std::string catalogBytes;
    if (!ReadFile(pos[1], catalogBytes)) return Fail("cannot read " + pos[1]);
    const Result<Catalog> parsed = ParseCatalog(catalogBytes);
    if (!parsed) return Fail(parsed.error);   // never sign what the store would refuse
    std::vector<uint8_t> seed;
    if (!FromHex(pos[2], seed)) return Fail("the seed is not hex");
    const Result<std::vector<uint8_t>> signature = SignCatalog(catalogBytes, seed);
    if (!signature) return Fail(signature.error);
    const std::string out = Option(args, "-o", pos[1] + ".sig");
    if (!WriteFile(out, ToHex(signature.Value()) + "\n")) return Fail("cannot write " + out);
    std::printf("signed %s (sequence %llu) -> %s\n", pos[1].c_str(),
                static_cast<unsigned long long>(parsed.Value().sequence), out.c_str());
    return 0;
}

int CmdSha256(const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    const std::string hex = Sha256HexOfFile(pos[1]);
    if (hex.empty()) return Fail("cannot hash " + pos[1]);
    std::printf("%s  %s\n", hex.c_str(), pos[1].c_str());
    return 0;
}

// ===== INSTALLING =====

std::string StoreRoot(const std::vector<std::string>& args) {
    const std::string root = Option(args, "--root");
    if (!root.empty()) return root;
    return UltraCanvas::PathToUtf8(UltraCanvas::UltraCanvasLocalDataFolderRoot() / "UltraStore");
}

bool LoadCatalog(const std::string& path, Catalog& out) {
    std::string text;
    if (!ReadFile(path, text)) { Fail("cannot read " + path); return false; }
    const Result<Catalog> parsed = ParseCatalog(text);
    if (!parsed) { Fail(parsed.error); return false; }
    out = parsed.Value();
    return true;
}

// Progress on stderr: one line per step, rewritten as the bytes come.
EngineCallbacks ConsoleCallbacks() {
    EngineCallbacks cb;
    cb.onStep = [](const InstallStep& step) {
        std::fprintf(stderr, "%s %s %s (%s)\n", step.alreadyInstalled ? "have" : "get",
                     step.component->id.c_str(), step.release->version.c_str(), FormatBytes(step.package->size).c_str());
    };
    cb.onDownload = [](const InstallStep&, int64_t done, int64_t total) {
        if (total > 0) std::fprintf(stderr, "\r  downloading %s of %s   ", FormatBytes(uint64_t(done)).c_str(), FormatBytes(uint64_t(total)).c_str());
        else std::fprintf(stderr, "\r  downloading %s   ", FormatBytes(uint64_t(done)).c_str());
        return true;
    };
    cb.onUnpack = [](const InstallStep&, uint64_t, uint64_t, const std::string& file) {
        if (!file.empty()) std::fprintf(stderr, "\r  unpacking %-60.60s", file.c_str());
        return true;
    };
    return cb;
}

void PrintOutcome(const InstallOutcome& outcome) {
    std::fprintf(stderr, "\r%-70s\r", "");
    for (const InstallStep& step : outcome.plan.steps) {
        std::printf("%-9s %s %s%s\n", ComponentKindName(step.component->kind).c_str(), step.component->id.c_str(),
                    step.release->version.c_str(), step.alreadyInstalled ? "  (was installed)" : "  installed");
    }
    if (outcome.installed.empty()) std::printf("nothing to do\n");
    else std::printf("downloaded %s\n", FormatBytes(outcome.bytesDownloaded).c_str());
}

int CmdInstall(const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 3) return Usage(stderr);
    Catalog catalog;
    if (!LoadCatalog(pos[1], catalog)) return 1;
    StoreEngine engine(StoreRoot(args));
    if (const auto r = engine.Open(); !r) return Fail(r.error);
    engine.SetUrlSigner(LicensedUrlSigner(engine.Registry(), catalog.paymentUrl));
    const auto outcome = engine.Install(catalog, pos[2], PlatformInfo::Detect(), Option(args, "--channel", "stable"), ConsoleCallbacks());
    if (!outcome) { std::fprintf(stderr, "\n"); return Fail(outcome.error); }
    PrintOutcome(outcome.Value());
    return 0;
}

int CmdUpdate(const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    Catalog catalog;
    if (!LoadCatalog(pos[1], catalog)) return 1;
    StoreEngine engine(StoreRoot(args));
    if (const auto r = engine.Open(); !r) return Fail(r.error);
    engine.SetUrlSigner(LicensedUrlSigner(engine.Registry(), catalog.paymentUrl));
    const PlatformInfo platform = PlatformInfo::Detect();
    if (pos.size() >= 3) {
        const auto outcome = engine.Update(catalog, pos[2], platform, Option(args, "--channel"), ConsoleCallbacks());
        if (!outcome) { std::fprintf(stderr, "\n"); return Fail(outcome.error); }
        PrintOutcome(outcome.Value());
        return 0;
    }
    const auto all = engine.UpdateAll(catalog, platform, ConsoleCallbacks());
    if (!all) return Fail(all.error);
    int rc = 0;
    for (const auto& [id, outcome] : all.Value()) {
        std::printf("%s:\n", id.c_str());
        if (!outcome) { Fail(outcome.error); rc = 1; continue; }
        PrintOutcome(outcome.Value());
    }
    if (all.Value().empty()) std::printf("no app is installed\n");
    return rc;
}

int CmdRemove(const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    StoreEngine engine(StoreRoot(args));
    if (const auto r = engine.Open(); !r) return Fail(r.error);
    const auto removed = engine.Remove(pos[1]);
    if (!removed) return Fail(removed.error);
    for (const InstalledRecord& r : removed.Value()) {
        std::printf("removed %-9s %s %s\n", ComponentKindName(r.kind).c_str(), r.id.c_str(), r.version.c_str());
    }
    return 0;
}

int CmdInstalled(const std::vector<std::string>& args) {
    StoreEngine engine(StoreRoot(args));
    if (const auto r = engine.Open(); !r) return Fail(r.error);
    const auto all = engine.Installed();
    if (!all) return Fail(all.error);
    std::printf("store at %s\n", engine.Root().c_str());
    if (all.Value().empty()) { std::printf("nothing is installed\n"); return 0; }
    for (const InstalledRecord& r : all.Value()) {
        std::printf("%-9s %s %s%s  %s\n", ComponentKindName(r.kind).c_str(), r.id.c_str(), r.version.c_str(),
                    r.current ? "  (current)" : "", r.installedAt.c_str());
    }
    return 0;
}

int CmdGc(const std::vector<std::string>& args) {
    StoreEngine engine(StoreRoot(args));
    if (const auto r = engine.Open(); !r) return Fail(r.error);
    const auto removed = engine.CollectGarbage();
    if (!removed) return Fail(removed.error);
    for (const InstalledRecord& r : removed.Value()) {
        std::printf("removed %-9s %s %s\n", ComponentKindName(r.kind).c_str(), r.id.c_str(), r.version.c_str());
    }
    if (removed.Value().empty()) std::printf("nothing to remove\n");
    return 0;
}

// buy <catalog.json> <app-id> [--code <CODE>]: the checkout asked for, the
// browser opened on it, the order polled until paid, the licence verified
// with the catalog's key and kept in the registry.
int CmdBuy(const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 3) return Usage(stderr);
    Catalog catalog;
    if (!LoadCatalog(pos[1], catalog)) return 1;
    const Component* app = catalog.FindApp(pos[2]);
    if (!app) return Fail("The catalog does not list " + pos[2]);
    if (catalog.paymentUrl.empty()) return Fail("The catalog names no payment service" + (app->homepage.empty() ? std::string() : "; the publisher's page is " + app->homepage));
    StoreEngine engine(StoreRoot(args));
    if (const auto r = engine.Open(); !r) return Fail(r.error);
    std::string code = Option(args, "--code");
    if (code.empty()) code = StoreSettings::Load().CodeInForce(NowIso8601Utc().substr(0, 10));
    const HttpCall http = UltraNetHttpCall();
    const auto started = StartCheckout(catalog.paymentUrl, pos[2], code, http);
    if (!started) return Fail(started.error);
    Order order = started.Value();
    std::printf("order %s: %s for %s %s%s\npay at %s\n", order.orderId.c_str(), app->name.c_str(), order.amount.c_str(), order.currency.c_str(),
                order.codeApplied ? (" with the code " + order.code).c_str() : "", order.checkoutUrl.c_str());
    std::fflush(stdout);
    UltraCanvas::OpenURL(order.checkoutUrl);
    std::fprintf(stderr, "waiting for the payment (Ctrl-C leaves the order open)");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);
    while (!order.Over() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::fprintf(stderr, ".");
        std::fflush(stderr);
        const auto now = OrderStatus(catalog.paymentUrl, order.orderId, order.secret, http);
        if (now) order = now.Value();
    }
    std::fprintf(stderr, "\n");
    if (!order.Paid()) return Fail("The purchase was " + order.status);
    Licence licence;
    if (catalog.licenceKey.empty()) {
        licence.app = pos[2];
        licence.licenceId = order.licenceId;
        licence.orderId = order.orderId;
        licence.issued = order.paidAt.substr(0, 10);
        std::fprintf(stderr, "warning: the catalog names no licence key; the licence is kept unverified\n");
    } else {
        const auto verified = VerifyLicence(order.licence, catalog.licenceKey);
        if (!verified) return Fail(verified.error);
        licence = verified.Value();
    }
    if (const auto kept = engine.Registry().PutLicence(LicenceRecordOf(order, licence)); !kept) return Fail(kept.error);
    std::printf("bought %s: receipt no. %d, licence %s\n", app->name.c_str(), order.receiptNo, licence.licenceId.c_str());
    return 0;
}

int CmdLicences(const std::vector<std::string>& args) {
    StoreEngine engine(StoreRoot(args));
    if (const auto r = engine.Open(); !r) return Fail(r.error);
    const auto all = engine.Registry().Licences();
    if (!all) return Fail(all.error);
    if (all.Value().empty()) { std::printf("nothing was bought here\n"); return 0; }
    for (const LicenceRecord& l : all.Value()) {
        std::printf("%-32s receipt %d  %s %s  %s  order %s  licence %s\n", l.app.c_str(), l.receiptNo, l.amount.c_str(), l.currency.c_str(),
                    l.issuedAt.c_str(), l.orderId.c_str(), l.licenceId.c_str());
    }
    return 0;
}

// code [<CODE> | clear]: the code in force, checked against the cached
// catalog with the settings' keys, kept in the settings file for 30 days.
int CmdCode(const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    StoreSettings settings = StoreSettings::Load();
    const std::string today = NowIso8601Utc().substr(0, 10);
    if (pos.size() < 2) {
        const std::string inForce = settings.CodeInForce(today);
        if (inForce.empty()) std::printf("no code in force\n");
        else std::printf("code %s, entered %s, in force until %d days after that\n", inForce.c_str(), settings.codeEnteredAt.c_str(), StoreSettings::kCodeDays);
        return 0;
    }
    if (pos[1] == "clear") {
        settings.code.clear();
        settings.codeEnteredAt.clear();
        if (!settings.Save()) return Fail("cannot write the settings file");
        std::printf("code cleared\n");
        return 0;
    }
    CatalogClientConfig config;
    config.baseUrls = settings.Servers();
    config.trustedKeys = settings.TrustedKeys();
    config.cacheDir = Option(args, "--cache");
    if (config.cacheDir.empty()) {
        config.cacheDir = UltraCanvas::PathToUtf8(UltraCanvas::UltraCanvasLocalDataFolderRoot() / "UltraStore" / "cache");
    }
    CatalogClient client(config);
    const Result<Catalog> cached = client.LoadCached();
    if (!cached) return Fail(Error{cached.error.code, "No catalog to check the code against: run `ultrastore fetch` first", cached.error.message});
    const CodeCheck check = CheckCode(cached.Value(), pos[1], today);
    if (!check.Ok()) return Fail(check.message);
    settings.code = check.code->code;
    settings.codeEnteredAt = today;
    if (!settings.Save()) return Fail("cannot write the settings file");
    std::printf("code %s: %s - in force for %d days\n", check.code->code.c_str(), check.message.c_str(), StoreSettings::kCodeDays);
    return 0;
}

} // namespace

int UltraStore::Cli::Run(const std::vector<std::string>& args) {
    if (args.empty()) return Usage(stderr);
    const std::string& cmd = args[0];
    if (cmd == "--help" || cmd == "-h" || cmd == "help") return Usage(stdout);
    if (cmd == "--version" || cmd == "-V") { std::printf("ultrastore %s\n", ULTRASTORE_VERSION); return 0; }
    if (cmd == "platform") return CmdPlatform();
    if (cmd == "list")     return CmdList(args);
    if (cmd == "resolve")  return CmdResolve(args);
    if (cmd == "verify")   return CmdVerify(args);
    if (cmd == "fetch")    return CmdFetch(args);
    if (cmd == "keygen")   return CmdKeygen();
    if (cmd == "pubkey")   return CmdPubkey(args);
    if (cmd == "sign")     return CmdSign(args);
    if (cmd == "sha256")   return CmdSha256(args);
    if (cmd == "install")  return CmdInstall(args);
    if (cmd == "update")   return CmdUpdate(args);
    if (cmd == "remove")   return CmdRemove(args);
    if (cmd == "installed") return CmdInstalled(args);
    if (cmd == "gc")       return CmdGc(args);
    if (cmd == "code")     return CmdCode(args);
    if (cmd == "buy")      return CmdBuy(args);
    if (cmd == "licences") return CmdLicences(args);
    std::fprintf(stderr, "ultrastore: unknown command \"%s\"\n", cmd.c_str());
    return Usage(stderr);
}
