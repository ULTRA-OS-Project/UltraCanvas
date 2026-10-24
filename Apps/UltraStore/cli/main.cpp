// Apps/UltraStore/cli/main.cpp
// ultrastore - the store from the command line. The core is finished and
// proven here before any window exists; later the same program opens the
// window when it is started without a command (one program, as UltraFIBU and
// UltraPress are, because a case-insensitive file system cannot tell
// `ultrastore` from `UltraStore`).
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
//   ultrastore --version | --help
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Catalog.h"
#include "CatalogClient.h"
#include "PackageResolver.h"
#include "PlatformInfo.h"
#include "Verifier.h"

#include "UltraCanvasDataFolder.h"
#include "UltraCanvasPathUtf8.h"

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
        "ultrastore %s - the ULTRA OS app store, from the command line\n"
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
        "  ultrastore --version | --help\n",
        ULTRASTORE_VERSION);
    return to == stdout ? 0 : 2;
}

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

std::string HumanSize(uint64_t bytes) {
    char buffer[32];
    if (bytes >= (uint64_t(1) << 30)) std::snprintf(buffer, sizeof(buffer), "%.1f GB", bytes / double(uint64_t(1) << 30));
    else if (bytes >= (uint64_t(1) << 20)) std::snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / double(uint64_t(1) << 20));
    else if (bytes >= 1024) std::snprintf(buffer, sizeof(buffer), "%llu KB", static_cast<unsigned long long>(bytes / 1024));
    else std::snprintf(buffer, sizeof(buffer), "%llu bytes", static_cast<unsigned long long>(bytes));
    return buffer;
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
                        HumanSize(res.package->size).c_str(),
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
        else std::printf("  %s  %s  %s\n", HumanSize(step.package->size).c_str(),
                         MatchKindName(step.match).c_str(), step.package->url.c_str());
    }
    std::printf("to download: %s\n", HumanSize(plan.Value().bytesToDownload).c_str());
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

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
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
    std::fprintf(stderr, "ultrastore: unknown command \"%s\"\n", cmd.c_str());
    return Usage(stderr);
}
