// Apps/UltraStore/setup/main.cpp
// ultrastore-setup - the first installer: puts UltraStore onto a machine
// that has none. It fetches the store's own signed catalog (ultrastore.json,
// SelfUpdate.h) from the servers the build names, checks it against the
// keys the build trusts, installs the store with the runtime it needs into
// the per-user store folder - the same tree, registry and engine the store
// uses afterwards, so the store finds itself installed and updates itself
// from then on - writes the entry in the application menu and starts the
// store. Run again, it brings an installed store up to date or does
// nothing.
//
// A console program: no window, so it needs none of the UI library's
// resources and says what it does in plain lines.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "CatalogClient.h"
#include "PlatformInfo.h"
#include "SelfUpdate.h"
#include "StoreEngine.h"
#include "StoreQuery.h"
#include "StoreSettings.h"

#include "UltraCanvasDataFolder.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef ULTRASTORE_VERSION
#error "ULTRASTORE_VERSION must be passed by CMake (from Docs/UltraStore/CHANGELOG.md)"
#endif

using namespace UltraStore;

namespace {

int Usage(FILE* to) {
    std::fprintf(to,
        "ultrastore-setup %s - installs UltraStore, the ULTRA OS app store\n"
        "\n"
        "  ultrastore-setup [options]\n"
        "\n"
        "  --check              say what would be installed, install nothing\n"
        "  --channel <name>     stable (the default) or beta\n"
        "  --root <dir>         the store folder (default: the per-user one)\n"
        "  --server <url>       a server to ask instead of the built-in ones; may be repeated\n"
        "  --key <hex>          a public key to trust beside the built-in ones; may be repeated\n"
        "  --catalog <file>     a local ultrastore.json instead of the servers' (not signature-checked: for trying out)\n"
        "  --no-menu            do not write the application menu entry\n"
        "  --no-start           do not start UltraStore afterwards\n"
        "  --version | --help\n",
        ULTRASTORE_VERSION);
    return to == stdout ? 0 : 2;
}

int Fail(const Error& e) {
    std::fprintf(stderr, "ultrastore-setup: %s", e.message.c_str());
    if (!e.detail.empty()) std::fprintf(stderr, " (%s)", e.detail.c_str());
    std::fprintf(stderr, " [%s]\n", ErrorCodeName(e.code).c_str());
    return 1;
}

struct Options {
    bool check = false, menu = true, start = true;
    std::string channel = "stable", root, catalogFile;
    std::vector<std::string> servers, keys;
};

// 0: go on; otherwise the exit status + 1.
int ParseOptions(const std::vector<std::string>& args, Options& o) {
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--help" || a == "-h") return Usage(stdout) + 1;
        if (a == "--version") { std::printf("ultrastore-setup %s\n", ULTRASTORE_VERSION); return 1; }
        if (a == "--check") { o.check = true; continue; }
        if (a == "--no-menu") { o.menu = false; continue; }
        if (a == "--no-start") { o.start = false; continue; }
        std::string* one = a == "--channel" ? &o.channel : a == "--root" ? &o.root : a == "--catalog" ? &o.catalogFile : nullptr;
        std::vector<std::string>* many = a == "--server" ? &o.servers : a == "--key" ? &o.keys : nullptr;
        if (!one && !many) {
            std::fprintf(stderr, "ultrastore-setup: unknown option %s\n", a.c_str());
            return Usage(stderr) + 1;
        }
        if (i + 1 >= args.size()) {
            std::fprintf(stderr, "ultrastore-setup: %s wants a value\n", a.c_str());
            return Usage(stderr) + 1;
        }
        if (one) *one = args[++i];
        else many->push_back(args[++i]);
    }
    return 0;
}

EngineCallbacks ConsoleCallbacks() {
    EngineCallbacks cb;
    cb.onStep = [](const InstallStep& step) {
        std::fprintf(stderr, "\r%-78s\r", "");
        std::printf("%s %s %s (%s)\n", step.alreadyInstalled ? "have" : "get ", step.component->name.empty() ? step.component->id.c_str() : step.component->name.c_str(),
                    step.release->version.c_str(), FormatBytes(step.package->size).c_str());
        std::fflush(stdout);
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

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (const int stop = ParseOptions(std::vector<std::string>(argv + 1, argv + argc), o)) return stop - 1;

    const PlatformInfo platform = PlatformInfo::Detect();
    const std::string root = o.root.empty()
        ? UltraCanvas::PathToUtf8(UltraCanvas::UltraCanvasLocalDataFolderRoot() / "UltraStore") : o.root;
    StoreEngine engine(root);
    if (const auto opened = engine.Open(); !opened) return Fail(opened.error);
    std::printf("UltraStore setup %s\nstore folder: %s\n", ULTRASTORE_VERSION, engine.Root().c_str());

    // The store's own catalog: the file given, or the servers'.
    Catalog catalog;
    if (!o.catalogFile.empty()) {
        std::ifstream in(UltraCanvas::PathFromUtf8(o.catalogFile), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        if (!in) return Fail(Error{ErrorCode::IoError, "cannot read " + o.catalogFile, {}});
        const Result<Catalog> parsed = ParseCatalog(text.str());
        if (!parsed) return Fail(parsed.error);
        catalog = parsed.Value();
        std::printf("catalog: %s (a local file, not signature-checked)\n", o.catalogFile.c_str());
    } else {
        CatalogClientConfig config;
        config.baseUrls = o.servers.empty() ? BuiltinServers() : o.servers;
        for (const std::string& hex : BuiltinKeysHex()) {
            std::vector<uint8_t> key;
            if (FromHex(hex, key) && key.size() == 32) config.trustedKeys.push_back(std::move(key));
        }
        for (const std::string& hex : o.keys) {
            std::vector<uint8_t> key;
            if (!FromHex(hex, key) || key.size() != 32) {
                std::fprintf(stderr, "ultrastore-setup: --key wants 64 hex characters, not \"%s\"\n", hex.c_str());
                return 2;
            }
            config.trustedKeys.push_back(std::move(key));
        }
        config.cacheDir = engine.CacheDir();
        if (config.baseUrls.empty()) return Fail(Error{ErrorCode::InvalidArgument, "This build names no server: pass --server <url>", {}});
        if (config.trustedKeys.empty()) return Fail(Error{ErrorCode::InvalidArgument, "This build trusts no key: pass --key <hex>", {}});
        CatalogClient client(SelfCatalogConfig(config));
        const Result<Catalog> fetched = client.Refresh();
        if (!fetched) return Fail(fetched.error);
        catalog = fetched.Value();
        std::printf("catalog: sequence %llu from %s, signature good\n",
                    static_cast<unsigned long long>(catalog.sequence), client.State().fetchedFrom.c_str());
    }

    const Result<SelfUpdateCheck> check = CheckSelfUpdate(catalog, engine, platform, o.channel);
    if (!check) return Fail(check.error);
    if (check.Value().installedVersion.empty()) std::printf("UltraStore %s will be installed\n", check.Value().newestVersion.c_str());
    else if (check.Value().available) std::printf("UltraStore %s is installed; %s is available\n", check.Value().installedVersion.c_str(), check.Value().newestVersion.c_str());
    else std::printf("UltraStore %s is installed and up to date\n", check.Value().installedVersion.c_str());
    if (o.check) return 0;

    const Result<SelfUpdateOutcome> done = InstallOrUpdateStore(engine, catalog, platform, o.channel, ConsoleCallbacks());
    if (!done) { std::fprintf(stderr, "\n"); return Fail(done.error); }
    const SelfUpdateOutcome& outcome = done.Value();
    if (outcome.changed) std::fprintf(stderr, "\r%-78s\r", "");   // the last progress line
    if (outcome.changed) std::printf("installed UltraStore %s (%s downloaded)\n", outcome.version.c_str(), FormatBytes(outcome.install.bytesDownloaded).c_str());
    // A version that was installed before stays where it is: a store window
    // may be running from it. The store collects it at its next start.

    if (o.menu) {
        const Result<std::string> menu = WriteStoreLauncher(engine);
        if (menu) std::printf("application menu: %s\n", menu.Value().c_str());
        else std::printf("no application menu entry: %s\n", menu.error.message.c_str());
    }
    if (outcome.launchPath.empty()) {
        std::printf("the package names no program to start; the store is in %s\n", engine.Root().c_str());
        return 0;
    }
    std::printf("program: %s\n", outcome.launchPath.c_str());
    if (o.start) {
        std::string error;
        if (!UltraCanvas::LaunchDetachedProcess({outcome.launchPath}, {}, error)) {
            std::fprintf(stderr, "ultrastore-setup: UltraStore is installed but could not be started: %s\n", error.c_str());
            return 1;
        }
        std::printf("UltraStore started\n");
    }
    return 0;
}
