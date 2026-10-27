// Apps/UltraStoreAdmin/cli/Commands.cpp
// ultrastoreadmin - the store's admin backend from the command line; the
// same program opens the window when started without a command
// (ui/main.cpp). Every command works on the store folder (--store <dir>;
// default UltraCanvasDataFolderRoot()/UltraStoreAdmin): admin.sqlite, the
// package store packages/, the media store media/.
//
//   ultrastoreadmin init                                    the database and the folders
//   ultrastoreadmin settings [set <key> <value>]            catalog.base_url, packages.base_url, texts.languages, ai.provider, ai.base_url, ai.model
//   ultrastoreadmin key new <name> [--passphrase <p>]       a signing key, its seed sealed with the passphrase
//   ultrastoreadmin key list | export <id> <file.pub> | retire <id>
//   ultrastoreadmin publisher add <id> <name> [<url>] | list | remove <id>
//   ultrastoreadmin app import <component.json>             an app or component, the catalog's object
//   ultrastoreadmin app new <id> <name> [--kind app|runtime|library] [--publisher <id>]
//   ultrastoreadmin app set <id> <field> <value>            one setting (app fields)
//   ultrastoreadmin app text <id> summary|description <lang> <text or @file>
//   ultrastoreadmin app media <id> icon|screenshot <file>   copied into the media store
//   ultrastoreadmin app show <id> | list | export <id> <file> | remove <id>
//   ultrastoreadmin release add <id> <version> [--channel beta] [--date <d>] [--depends <id>@<version> ...] [--notes <lang> <text>]
//   ultrastoreadmin release remove <id> <version>
//   ultrastoreadmin package add <id> <version> <file> --os <os> --arch <arch> [--variant <v> ...] [--min-os <v>] [--entry <path>] [--access public|restricted]
//   ultrastoreadmin package entries <file>                  the archive's listing and the suggested entry
//   ultrastoreadmin translate [<id>] [--languages de fr ...] [--redo]
//   ultrastoreadmin build [-o <catalog.json>]              the catalog, unsigned, for a look
//   ultrastoreadmin publish <outdir> --key <id> [--passphrase <p>] [--no-copy]
//   ultrastoreadmin --version | --help
//
// A passphrase comes from --passphrase, else ULTRASTOREADMIN_PASSPHRASE, else
// a line on standard input.
//
// Version: 0.4.0 - affiliate, code, sale
// Version: 0.3.0 - upload
// Version: 0.2.0 - Run() and Usage() for ui/main.cpp; the window in the usage
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Commands.h"

#include "AdminDatabase.h"
#include "CatalogPublisher.h"
#include "KeyVault.h"
#include "PackageStore.h"
#include "Translation.h"
#include "Affiliates.h"
#include "StoreQuery.h"
#include "Uploader.h"

#include "Catalog.h"
#include "UltraStoreTypes.h"

#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef ULTRASTOREADMIN_VERSION
#error "ULTRASTOREADMIN_VERSION must be passed by CMake (from Docs/UltraStoreAdmin/CHANGELOG.md)"
#endif

using namespace UltraStoreAdmin;
using UltraStore::Component;
using UltraStore::ComponentKind;
using UltraStore::Error;
using UltraStore::Package;
using UltraStore::Release;

namespace {

namespace fs = std::filesystem;

} // namespace

int UltraStoreAdmin::Cli::Usage(FILE* to) {
    std::fprintf(to,
        "ultrastoreadmin %s - the UltraStore admin backend\n"
        "\n"
        "  ultrastoreadmin [--store <dir>]                the window\n"
        "  ultrastoreadmin [--store <dir>] <command>\n"
        "\n"
        "  init\n"
        "  settings [set <key> <value>]\n"
        "  key new <name> [--passphrase <p>] | list | export <id> <file.pub> | retire <id>\n"
        "  publisher add <id> <name> [<url>] | list | remove <id>\n"
        "  app import <component.json> | new <id> <name> [--kind app|runtime|library] [--publisher <id>]\n"
        "  app set <id> <field> <value> | text <id> summary|description <lang> <text|@file>\n"
        "  app media <id> icon|screenshot <file> | show <id> | list | export <id> <file> | remove <id>\n"
        "  release add <id> <version> [--channel beta] [--date <d>] [--depends <id>@<version> ...] [--notes <lang> <text>]\n"
        "  release remove <id> <version>\n"
        "  package add <id> <version> <file> --os <os> --arch <arch> [--variant <v> ...] [--min-os <v>] [--entry <path>] [--access public|restricted]\n"
        "  package entries <file>\n"
        "  translate [<id>] [--languages <l> ...] [--redo]\n"
        "  build [-o <catalog.json>]\n"
        "  publish <outdir> --key <id> [--passphrase <p>] [--no-copy]\n"
        "  upload <outdir> [--to <url>] [--user <name>] [--password <p>] [--key-file <pem>] [--all]\n"
        "  affiliate add <id> <name> [--contact <c>] [--share <percent>] [--recruit-share <percent>] [--recruited-by <id>]\n"
        "  affiliate list | show <id> | enable <id> | disable <id> | remove <id> | report | qr <id> <file.svg>\n"
        "  code new <CODE> [--affiliate <id>] [--percent <n> | --amount <x> --currency <ccy>] [--apps <id> ...] [--from <d>] [--until <d>]\n"
        "  code list | enable <CODE> | disable <CODE> | remove <CODE> | link <CODE> | qr <CODE> <file.svg>\n"
        "  sale add <CODE|-> <app-id> <amount> <currency> [--at <iso8601>] | list\n"
        "  --version | --help\n"
        "\n"
        "Fields for `app set`: name publisher license homepage support_url privacy_url source_url icon\n"
        "  source_language categories tags screenshots visibility lifecycle successor age_rating\n"
        "  requires_account open_source pricing.model price currency trial_days updates.period updates.until\n",
        ULTRASTOREADMIN_VERSION);
    return to == stdout ? 0 : 2;
}

namespace {

int Usage(FILE* to) { return UltraStoreAdmin::Cli::Usage(to); }

int Fail(const Error& e) {
    std::fprintf(stderr, "ultrastoreadmin: %s", e.message.c_str());
    if (!e.detail.empty()) std::fprintf(stderr, " (%s)", e.detail.c_str());
    std::fprintf(stderr, " [%s]\n", UltraStore::ErrorCodeName(e.code).c_str());
    return 1;
}

int Fail(const std::string& message) {
    std::fprintf(stderr, "ultrastoreadmin: %s\n", message.c_str());
    return 1;
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

std::string Option(const std::vector<std::string>& args, const std::string& name, const std::string& fallback = {}) {
    for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == name) return args[i + 1];
    return fallback;
}

std::vector<std::string> Options(const std::vector<std::string>& args, const std::string& name) {
    std::vector<std::string> out;
    for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == name) out.push_back(args[i + 1]);
    return out;
}

bool Flag(const std::vector<std::string>& args, const std::string& name) {
    for (const std::string& a : args) if (a == name) return true;
    return false;
}

// --languages takes every following word until the next option.
std::vector<std::string> ListOption(const std::vector<std::string>& args, const std::string& name) {
    std::vector<std::string> out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] != name) continue;
        for (size_t j = i + 1; j < args.size() && args[j].rfind("--", 0) != 0; ++j) out.push_back(args[j]);
    }
    return out;
}

std::vector<std::string> Positional(const std::vector<std::string>& args, const std::vector<std::string>& flags = {}) {
    std::vector<std::string> out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i].size() > 1 && args[i][0] == '-') {
            bool isFlag = false;
            for (const std::string& f : flags) if (f == args[i]) isFlag = true;
            if (!isFlag) ++i;
            continue;
        }
        out.push_back(args[i]);
    }
    return out;
}

std::string Passphrase(const std::vector<std::string>& args) {
    std::string p = Option(args, "--passphrase");
    if (!p.empty()) return p;
    p = UltraCanvas::GetEnvUtf8("ULTRASTOREADMIN_PASSPHRASE");   // path-string-ok: GetEnvUtf8 is the UTF-8 read
    if (!p.empty()) return p;
    std::fprintf(stderr, "Passphrase: ");
    std::fflush(stderr);
    std::getline(std::cin, p);
    return p;
}

std::vector<std::string> SplitWords(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

std::string JoinWords(const std::vector<std::string>& words) {
    std::string out;
    for (const std::string& w : words) out += (out.empty() ? "" : " ") + w;
    return out;
}

// ===== the store folder =====
struct Store {
    AdminStore paths;
    AdminDatabase db;
    std::string& dir = paths.dir;
    std::string DatabasePath() const { return paths.DatabasePath(); }
    std::string PackagesDir() const { return paths.PackagesDir(); }
    std::string MediaDir() const { return paths.MediaDir(); }

    bool Open(bool create) {
        if (create) {
            if (const auto made = paths.CreateFolders(); !made) { Fail(made.error); return false; }
        } else if (!paths.Exists()) {
            Fail("No admin database at " + DatabasePath() + ": run `ultrastoreadmin init` first");
            return false;
        }
        const auto opened = db.Open(DatabasePath());
        if (!opened) { Fail(opened.error); return false; }
        return true;
    }
};

// ===== commands =====
int CmdInit(Store& store) {
    if (!store.Open(true)) return 1;
    std::printf("admin store at %s\n  database  %s\n  packages  %s\n  media     %s\n", store.dir.c_str(),
                store.DatabasePath().c_str(), store.PackagesDir().c_str(), store.MediaDir().c_str());
    if (store.db.Setting("texts.languages").empty()) store.db.SetSetting("texts.languages", JoinWords(OfficialLanguageCodes()));
    std::printf("next: `settings set catalog.base_url https://...`, `key new <name>`, `publisher add`, `app import`\n");
    return 0;
}

int CmdSettings(Store& store, const std::vector<std::string>& args) {
    if (!store.Open(false)) return 1;
    const auto pos = Positional(args);
    if (pos.size() >= 2 && pos[1] == "set") {
        if (pos.size() < 4) return Usage(stderr);
        if (const auto r = store.db.SetSetting(pos[2], pos[3]); !r) return Fail(r.error);
        std::printf("%s = %s\n", pos[2].c_str(), pos[3].c_str());
        return 0;
    }
    const auto all = store.db.Settings();
    if (!all) return Fail(all.error);
    if (all.Value().empty()) std::printf("no settings yet\n");
    for (const auto& [k, v] : all.Value()) std::printf("%-24s %s\n", k.c_str(), v.c_str());
    std::printf("\nkeys the store reads: catalog.base_url packages.base_url texts.languages ai.provider ai.base_url ai.model upload.url upload.user upload.key_file\n");
    return 0;
}

int CmdKey(Store& store, const std::vector<std::string>& args) {
    if (!store.Open(false)) return 1;
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    const std::string& sub = pos[1];
    if (sub == "new") {
        if (pos.size() < 3) return Usage(stderr);
        const std::string passphrase = Passphrase(args);
        if (passphrase.empty()) return Fail("a key needs a passphrase");
        std::fprintf(stderr, "deriving the key's seal (Argon2id, a moment)...\n");
        const auto key = NewSigningKey(passphrase);
        if (!key) return Fail(key.error);
        const auto added = store.db.AddKey(pos[2], key.Value().publicKeyHex, key.Value().sealed.Bytes());
        if (!added) return Fail(added.error);
        std::printf("key %lld \"%s\"\npublic: %s\n", static_cast<long long>(added.Value().id), added.Value().name.c_str(), added.Value().publicKeyHex.c_str());
        std::printf("Export it with `key export %lld Apps/UltraStore/keys/<name>.pub` so the store trusts it.\n", static_cast<long long>(added.Value().id));
        return 0;
    }
    if (sub == "list") {
        const auto keys = store.db.Keys();
        if (!keys) return Fail(keys.error);
        if (keys.Value().empty()) std::printf("no key yet: `key new <name>`\n");
        for (const KeyRecord& k : keys.Value()) {
            std::printf("%lld  %-20s %s  created %s%s\n", static_cast<long long>(k.id), k.name.c_str(), k.publicKeyHex.c_str(),
                        k.createdAt.c_str(), k.Active() ? "" : ("  retired " + k.retiredAt).c_str());
        }
        return 0;
    }
    if (sub == "export") {
        if (pos.size() < 4) return Usage(stderr);
        const auto key = store.db.GetKey(std::atoll(pos[2].c_str()));   // locale-ok: digits only
        if (!key) return Fail(key.error);
        if (!WriteFile(pos[3], key.Value().publicKeyHex + "\n")) return Fail("cannot write " + pos[3]);
        std::printf("wrote %s\n", pos[3].c_str());
        return 0;
    }
    if (sub == "retire") {
        if (pos.size() < 3) return Usage(stderr);
        if (const auto r = store.db.RetireKey(std::atoll(pos[2].c_str())); !r) return Fail(r.error);   // locale-ok: digits only
        std::printf("key %s retired; release the store without its .pub to stop trusting it\n", pos[2].c_str());
        return 0;
    }
    return Usage(stderr);
}

int CmdPublisher(Store& store, const std::vector<std::string>& args) {
    if (!store.Open(false)) return 1;
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    if (pos[1] == "add") {
        if (pos.size() < 4) return Usage(stderr);
        UltraStore::Publisher p{pos[2], pos[3], pos.size() > 4 ? pos[4] : std::string()};
        if (const auto r = store.db.PutPublisher(p); !r) return Fail(r.error);
        std::printf("publisher %s: %s\n", p.id.c_str(), p.name.c_str());
        return 0;
    }
    if (pos[1] == "list") {
        const auto all = store.db.Publishers();
        if (!all) return Fail(all.error);
        for (const auto& p : all.Value()) std::printf("%-24s %s  %s\n", p.id.c_str(), p.name.c_str(), p.url.c_str());
        if (all.Value().empty()) std::printf("no publisher yet\n");
        return 0;
    }
    if (pos[1] == "remove") {
        if (pos.size() < 3) return Usage(stderr);
        if (const auto r = store.db.RemovePublisher(pos[2]); !r) return Fail(r.error);
        return 0;
    }
    return Usage(stderr);
}

bool SetField(Component& c, const std::string& field, const std::string& value, std::string& error) {
    auto onOff = [&](bool& target) {
        if (value == "true" || value == "yes" || value == "1") { target = true; return true; }
        if (value == "false" || value == "no" || value == "0") { target = false; return true; }
        error = field + " is true or false";
        return false;
    };
    if (field == "name") c.name = value;
    else if (field == "publisher") c.publisher = value;
    else if (field == "license") c.license = value;
    else if (field == "homepage") c.homepage = value;
    else if (field == "support_url") c.supportUrl = value;
    else if (field == "privacy_url") c.privacyUrl = value;
    else if (field == "source_url") c.sourceUrl = value;
    else if (field == "icon") c.icon = value;
    else if (field == "source_language") c.sourceLanguage = value;
    else if (field == "categories") c.categories = SplitWords(value);
    else if (field == "tags") c.tags = SplitWords(value);
    else if (field == "screenshots") c.screenshots = SplitWords(value);
    else if (field == "successor") c.successor = value;
    else if (field == "age_rating") c.ageRating = value;
    else if (field == "price") c.pricing.price = value;
    else if (field == "currency") c.pricing.currency = value;
    else if (field == "trial_days") c.pricing.trialDays = std::atoi(value.c_str());   // locale-ok: digits only
    else if (field == "updates.until") c.updates.until = value;
    else if (field == "requires_account") return onOff(c.requiresAccount);
    else if (field == "open_source") return onOff(c.openSource);
    else if (field == "visibility") {
        if (value != "listed" && value != "unlisted" && value != "hidden") { error = "visibility is listed, unlisted or hidden"; return false; }
        c.visibility = UltraStore::ParseVisibility(value);
    } else if (field == "lifecycle") {
        if (value != "active" && value != "deprecated" && value != "withdrawn") { error = "lifecycle is active, deprecated or withdrawn"; return false; }
        c.lifecycle = UltraStore::ParseLifecycle(value);
    } else if (field == "pricing.model") {
        if (value != "free" && value != "one-time" && value != "monthly" && value != "yearly") { error = "pricing.model is free, one-time, monthly or yearly"; return false; }
        c.pricing.model = UltraStore::ParsePricingModel(value);
    } else if (field == "updates.period") {
        if (value != "always" && value != "never" && value != "until") { error = "updates.period is always, never or until"; return false; }
        c.updates.period = UltraStore::ParseUpdatePeriod(value);
    } else {
        error = "unknown field " + field;
        return false;
    }
    return true;
}

int CmdApp(Store& store, const std::vector<std::string>& args) {
    if (!store.Open(false)) return 1;
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    const std::string& sub = pos[1];
    if (sub == "import") {
        if (pos.size() < 3) return Usage(stderr);
        std::string text;
        if (!ReadFile(pos[2], text)) return Fail("cannot read " + pos[2]);
        const auto parsed = UltraStore::ParseComponent(text, UltraStore::ComponentKind::App, false);   // the admin keeps a package before it has a URL: its own export reads back
        if (!parsed) return Fail(parsed.error);
        if (const auto r = store.db.PutComponent(parsed.Value()); !r) return Fail(r.error);
        std::printf("%s %s (%s) stored\n", UltraStore::ComponentKindName(parsed.Value().kind).c_str(), parsed.Value().id.c_str(), parsed.Value().name.c_str());
        return 0;
    }
    if (sub == "new") {
        if (pos.size() < 4) return Usage(stderr);
        Component c;
        c.id = pos[2];
        c.name = pos[3];
        c.kind = UltraStore::ParseComponentKind(Option(args, "--kind", "app"));
        c.publisher = Option(args, "--publisher");
        if (const auto r = store.db.PutComponent(c); !r) return Fail(r.error);
        std::printf("%s %s created; add texts, releases and packages\n", UltraStore::ComponentKindName(c.kind).c_str(), c.id.c_str());
        return 0;
    }
    if (sub == "list") {
        const auto all = store.db.Components();
        if (!all) return Fail(all.error);
        if (all.Value().empty()) std::printf("no app or component yet: `app import` or `app new`\n");
        for (const Component& c : all.Value()) {
            std::printf("%-8s %-32s %-24s %zu release(s)%s\n", UltraStore::ComponentKindName(c.kind).c_str(), c.id.c_str(), c.name.c_str(),
                        c.releases.size(), c.releases.empty() ? "  (not published until one is added)" : "");
        }
        return 0;
    }
    if (pos.size() < 3) return Usage(stderr);
    const std::string& id = pos[2];
    auto component = store.db.GetComponent(id);
    if (!component) return Fail(component.error);
    Component& c = component.Value();
    if (sub == "show") {
        std::printf("%s\n", UltraStore::SerializeComponent(c).c_str());
        return 0;
    }
    if (sub == "export") {
        if (pos.size() < 4) return Usage(stderr);
        if (!WriteFile(pos[3], UltraStore::SerializeComponent(c) + "\n")) return Fail("cannot write " + pos[3]);
        std::printf("wrote %s\n", pos[3].c_str());
        return 0;
    }
    if (sub == "remove") {
        if (const auto r = store.db.RemoveComponent(id); !r) return Fail(r.error);
        std::printf("%s removed from the database (its package files stay in the package store)\n", id.c_str());
        return 0;
    }
    if (sub == "set") {
        if (pos.size() < 5) return Usage(stderr);
        std::string error;
        if (!SetField(c, pos[3], pos[4], error)) return Fail(error);
    } else if (sub == "text") {
        if (pos.size() < 6) return Usage(stderr);
        std::string text = pos[5];
        if (!text.empty() && text[0] == '@' && !ReadFile(text.substr(1), text)) return Fail("cannot read " + pos[5].substr(1));
        UltraStore::LocalizedText* target = pos[3] == "summary" ? &c.summary : pos[3] == "description" ? &c.description : nullptr;
        if (!target) return Fail("the text is summary or description");
        target->byLanguage[pos[4]] = text;
        if (c.sourceLanguage.empty()) c.sourceLanguage = pos[4];
        // A text a person wrote is not a machine one any more.
        c.machineTranslated.erase(std::remove(c.machineTranslated.begin(), c.machineTranslated.end(), pos[4]), c.machineTranslated.end());
    } else if (sub == "media") {
        if (pos.size() < 5) return Usage(stderr);
        const std::string what = pos[3];
        if (what != "icon" && what != "screenshot") return Fail("media is icon or screenshot");
        const auto imported = ImportMedia(pos[4], store.dir, id);
        if (!imported) return Fail(imported.error);
        const std::string& relative = imported.Value();
        if (what == "icon") c.icon = relative;
        else if (std::find(c.screenshots.begin(), c.screenshots.end(), relative) == c.screenshots.end()) c.screenshots.push_back(relative);
        std::printf("%s -> %s\n", pos[4].c_str(), relative.c_str());
    } else {
        return Usage(stderr);
    }
    if (const auto r = store.db.PutComponent(c); !r) return Fail(r.error);
    std::printf("%s updated\n", id.c_str());
    return 0;
}

int CmdRelease(Store& store, const std::vector<std::string>& args) {
    if (!store.Open(false)) return 1;
    const auto pos = Positional(args);
    if (pos.size() < 4) return Usage(stderr);
    auto component = store.db.GetComponent(pos[2]);
    if (!component) return Fail(component.error);
    Component& c = component.Value();
    const std::string& version = pos[3];
    if (pos[1] == "add") {
        Release* existing = nullptr;
        for (Release& r : c.releases) if (r.version == version) existing = &r;
        Release fresh;
        Release& r = existing ? *existing : fresh;
        r.version = version;
        r.channel = Option(args, "--channel", existing ? r.channel : std::string("stable"));
        r.date = Option(args, "--date", r.date);
        for (const std::string& dep : Options(args, "--depends")) {
            const size_t at = dep.find('@');
            if (at == std::string::npos) return Fail("--depends wants <id>@<version>, not " + dep);
            bool known = false;
            for (auto& d : r.depends) if (d.id == dep.substr(0, at)) { d.version = dep.substr(at + 1); known = true; }
            if (!known) r.depends.push_back({dep.substr(0, at), dep.substr(at + 1)});
        }
        const auto notes = Options(args, "--notes");
        if (notes.size() == 1) {
            // --notes <lang> <text>: the text is the word after the language
            for (size_t i = 0; i + 2 < args.size(); ++i) if (args[i] == "--notes") r.notes.byLanguage[args[i + 1]] = args[i + 2];
        }
        if (!existing) {
            c.releases.push_back(r);
            std::sort(c.releases.begin(), c.releases.end(), [](const Release& a, const Release& b) { return UltraStore::VersionLess(b.version, a.version); });
        }
        if (const auto w = store.db.PutComponent(c); !w) return Fail(w.error);
        std::printf("%s %s on %s%s\n", c.id.c_str(), version.c_str(), r.channel.c_str(), existing ? " updated" : " added");
        return 0;
    }
    if (pos[1] == "remove") {
        const size_t before = c.releases.size();
        c.releases.erase(std::remove_if(c.releases.begin(), c.releases.end(), [&](const Release& r) { return r.version == version; }), c.releases.end());
        if (c.releases.size() == before) return Fail("no release " + version + " of " + c.id);
        if (const auto w = store.db.PutComponent(c); !w) return Fail(w.error);
        std::printf("%s %s removed\n", c.id.c_str(), version.c_str());
        return 0;
    }
    return Usage(stderr);
}

int CmdPackage(Store& store, const std::vector<std::string>& args) {
    const auto pos = Positional(args);
    if (pos.size() < 3) return Usage(stderr);
    if (pos[1] == "entries") {
        const auto entries = ListPackageEntries(pos[2]);
        if (!entries) return Fail(entries.error);
        for (const std::string& e : entries.Value()) std::printf("%s\n", e.c_str());
        const UltraStore::OS os = UltraStore::ParseOS(Option(args, "--os", "linux"));
        std::printf("suggested entry (%s): %s\n", UltraStore::OSName(os).c_str(), SuggestEntry(entries.Value(), os).c_str());
        return 0;
    }
    if (pos[1] != "add" || pos.size() < 5) return Usage(stderr);
    if (!store.Open(false)) return 1;
    auto component = store.db.GetComponent(pos[2]);
    if (!component) return Fail(component.error);
    Component& c = component.Value();
    Release* release = nullptr;
    for (Release& r : c.releases) if (r.version == pos[3]) release = &r;
    if (!release) return Fail("no release " + pos[3] + " of " + c.id + ": `release add` first");
    const std::string osText = Option(args, "--os"), archText = Option(args, "--arch");
    if (osText.empty() || archText.empty()) return Fail("package add needs --os and --arch");
    Package p;
    p.target.os = UltraStore::ParseOS(osText);
    p.target.arch = UltraStore::ParseArch(archText);
    if (p.target.os == UltraStore::OS::Unknown) return Fail("unknown os " + osText);
    if (p.target.arch == UltraStore::Arch::Unknown) return Fail("unknown arch " + archText);
    p.target.variants = Options(args, "--variant");
    p.minOs = Option(args, "--min-os");
    const auto imported = ImportPackage(pos[4], store.PackagesDir());
    if (!imported) return Fail(imported.error);
    p.format = imported.Value().format;
    p.sha256 = imported.Value().sha256;
    p.size = imported.Value().size;
    p.entry = Option(args, "--entry");
    if (p.entry.empty() && c.kind == ComponentKind::App) {
        if (const auto entries = ListPackageEntries(imported.Value().file)) p.entry = SuggestEntry(entries.Value(), p.target.os);
        if (p.entry.empty()) std::fprintf(stderr, "note: no entry suggested; set one with --entry for the store to start the app\n");
        else std::fprintf(stderr, "entry: %s (suggested from the archive; --entry overrides)\n", p.entry.c_str());
    }
    PackageFile file;
    file.sha256 = p.sha256;
    file.file = imported.Value().file;
    file.size = p.size;
    file.format = p.format;
    file.access = Option(args, "--access", c.pricing.model == UltraStore::PricingModel::Free ? "public" : "restricted");
    if (const auto r = store.db.PutPackageFile(file); !r) return Fail(r.error);
    // The same target once: replace.
    release->packages.erase(std::remove_if(release->packages.begin(), release->packages.end(), [&](const Package& q) {
        return q.target.os == p.target.os && q.target.arch == p.target.arch && q.target.variants == p.target.variants;
    }), release->packages.end());
    release->packages.push_back(p);
    if (const auto w = store.db.PutComponent(c); !w) return Fail(w.error);
    std::printf("%s %s %s: %s, %llu bytes, sha256 %s, %s\n", c.id.c_str(), release->version.c_str(), p.target.Describe().c_str(),
                p.format.c_str(), static_cast<unsigned long long>(p.size), p.sha256.c_str(), file.access.c_str());
    return 0;
}

int CmdTranslate(Store& store, const std::vector<std::string>& args) {
    if (!store.Open(false)) return 1;
    std::vector<std::string> languages = ListOption(args, "--languages");
    if (languages.empty()) languages = SplitWords(store.db.Setting("texts.languages"));
    if (languages.empty()) languages = OfficialLanguageCodes();
    std::string error;
    auto translator = CreateUltraAITranslator(store.db.Setting("ai.provider"), store.db.Setting("ai.base_url"), store.db.Setting("ai.model"), &error);
    if (!translator) return Fail("no translator: " + error);
    const auto pos = Positional(args, {"--redo"});
    std::vector<Component> components;
    if (pos.size() >= 2) {
        const auto one = store.db.GetComponent(pos[1]);
        if (!one) return Fail(one.error);
        components.push_back(one.Value());
    } else {
        const auto all = store.db.Components();
        if (!all) return Fail(all.error);
        components = all.Value();
    }
    int rc = 0;
    for (Component& c : components) {
        const auto report = TranslateComponent(c, *translator, languages, Flag(args, "--redo"));
        if (!report) { Fail(report.error); rc = 1; continue; }
        if (const auto w = store.db.PutComponent(c); !w) { Fail(w.error); rc = 1; continue; }
        std::printf("%s: %zu texts into %zu language(s)%s\n", c.id.c_str(), report.Value().texts, report.Value().languages.size(),
                    report.Value().skipped.empty() ? "" : (", " + std::to_string(report.Value().skipped.size()) + " already complete").c_str());
        for (const std::string& e : report.Value().errors) { std::fprintf(stderr, "  %s\n", e.c_str()); rc = 1; }
    }
    return rc;
}

int CmdBuild(Store& store, const std::vector<std::string>& args) {
    if (!store.Open(false)) return 1;
    PublishOptions options;
    std::vector<std::string> warnings;
    const auto catalog = BuildCatalog(store.db, options, &warnings);
    if (!catalog) return Fail(catalog.error);
    for (const std::string& w : warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
    const std::string text = UltraStore::SerializeCatalog(catalog.Value());
    const std::string out = Option(args, "-o");
    if (out.empty()) std::printf("%s\n", text.c_str());
    else if (!WriteFile(out, text)) return Fail("cannot write " + out);
    else std::fprintf(stderr, "wrote %s (sequence %llu, unsigned)\n", out.c_str(), static_cast<unsigned long long>(catalog.Value().sequence));
    return 0;
}

int CmdPublish(Store& store, const std::vector<std::string>& args) {
    if (!store.Open(false)) return 1;
    const auto pos = Positional(args, {"--no-copy"});
    if (pos.size() < 2) return Usage(stderr);
    const std::string keyText = Option(args, "--key");
    if (keyText.empty()) return Fail("publish needs --key <id> (`key list`)");
    PublishOptions options;
    options.outDirUtf8 = pos[1];
    options.mediaRootUtf8 = store.dir;
    options.copyPackages = !Flag(args, "--no-copy");
    const std::string passphrase = Passphrase(args);
    const auto report = Publish(store.db, std::atoll(keyText.c_str()), passphrase, options);   // locale-ok: digits only
    if (!report) return Fail(report.error);
    const PublishReport& r = report.Value();
    for (const std::string& w : r.warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
    std::printf("published sequence %llu: %zu app(s), %zu component(s), %zu package file(s), %zu picture(s)\n  %s\n  %s\n  key %s\n",
                static_cast<unsigned long long>(r.sequence), r.apps, r.components, r.packagesCopied, r.mediaCopied,
                r.catalogPath.c_str(), r.signaturePath.c_str(), r.publicKeyHex.c_str());
    return 0;
}

// upload <outdir> [--to <url>] [--user <name>] [--password <p>] [--key-file <pem>] [--all]
// The target and the user come from the settings (upload.url, upload.user,
// upload.key_file) unless given; the password from --password, else
// ULTRASTOREADMIN_UPLOAD_PASSWORD, else a line on standard input when the
// target has a user and no key.
int CmdUpload(Store& store, const std::vector<std::string>& args) {
    if (!store.Open(false)) return 1;
    const auto pos = Positional(args, {"--all"});
    if (pos.size() < 2) return Usage(stderr);
    UploadTarget target;
    target.url = Option(args, "--to", store.db.Setting("upload.url"));
    if (target.url.empty()) return Fail("upload needs --to <url> or the setting upload.url (sftp://user@host/path, ftps://, https://)");
    target.username = Option(args, "--user", store.db.Setting("upload.user"));
    target.privateKeyFileUtf8 = Option(args, "--key-file", store.db.Setting("upload.key_file"));
    target.keyPassphrase = Option(args, "--key-passphrase");
    target.password = Option(args, "--password");
    if (target.password.empty()) target.password = UltraCanvas::GetEnvUtf8("ULTRASTOREADMIN_UPLOAD_PASSWORD");   // path-string-ok: GetEnvUtf8 is the UTF-8 read
    const bool urlHasUser = target.url.find('@') != std::string::npos && target.url.find('@') < target.url.find('/', 8);
    if (target.password.empty() && target.privateKeyFileUtf8.empty() && (urlHasUser || !target.username.empty())) {
        std::fprintf(stderr, "Password for %s: ", target.url.c_str());
        std::fflush(stderr);
        std::getline(std::cin, target.password);
    }
    auto transport = CreateUltraNetTransport(target);
    if (!transport) return Fail(transport.error);
    UploadOptions options;
    options.publishDirUtf8 = pos[1];
    options.stateFileUtf8 = UploadStatePath(store.dir);
    options.everything = Flag(args, "--all");
    options.onProgress = [](const UploadProgress& p) {
        if (p.currentFile.empty()) return;
        std::fprintf(stderr, "[%zu/%zu] %s\n", p.filesDone + 1, p.filesTotal, p.currentFile.c_str());
    };
    const auto report = UploadPublishFolder(*transport.Value(), target.url, options);
    if (!report) return Fail(report.error);
    for (const std::string& w : report.Value().warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
    std::printf("uploaded %zu file(s), %llu bytes, to %s (%s); %zu unchanged\n", report.Value().filesSent,
                static_cast<unsigned long long>(report.Value().bytesSent), target.url.c_str(),
                SchemeName(SchemeOf(target.url)).c_str(), report.Value().filesSkipped);
    return 0;
}

// ===== affiliates, codes, sales =====
std::string Base(Store& store) { return store.db.Setting("catalog.base_url"); }

int CmdAffiliate(Store& store, const std::vector<std::string>& args) {
    if (!store.Open(false)) return 1;
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    const std::string& sub = pos[1];
    if (sub == "add") {
        if (pos.size() < 4) return Usage(stderr);
        Affiliate a;
        if (const auto existing = store.db.GetAffiliate(pos[2])) a = existing.Value();
        a.id = pos[2];
        a.name = pos[3];
        a.contact = Option(args, "--contact", a.contact);
        a.sharePercent = std::atoi(Option(args, "--share", std::to_string(a.sharePercent)).c_str());                  // locale-ok: digits only
        a.recruitSharePercent = std::atoi(Option(args, "--recruit-share", std::to_string(a.recruitSharePercent)).c_str());   // locale-ok: digits only
        a.recruitedBy = Option(args, "--recruited-by", a.recruitedBy);
        if (const auto r = store.db.PutAffiliate(a); !r) return Fail(r.error);
        std::printf("affiliate %s: %s, %d%% of a sale with their code, %d%% of a recruit's sale%s\n  join link %s\n", a.id.c_str(), a.name.c_str(),
                    a.sharePercent, a.recruitSharePercent, a.recruitedBy.empty() ? "" : (", recruited by " + a.recruitedBy).c_str(),
                    JoinLink(Base(store), a.id).c_str());
        return 0;
    }
    if (sub == "list" || sub == "report") {
        const auto report = AffiliateReport(store.db);
        if (!report) return Fail(report.error);
        if (report.Value().empty()) { std::printf("no affiliate yet\n"); return 0; }
        for (const AffiliateTotals& t : report.Value()) {
            const Affiliate& a = t.affiliate;
            std::printf("%-16s %-24s %3d%% / %3d%% recruits  %s%s\n", a.id.c_str(), a.name.c_str(), a.sharePercent, a.recruitSharePercent,
                        a.recruitedBy.empty() ? "" : ("by " + a.recruitedBy).c_str(), a.enabled ? "" : "  (off)");
            if (sub == "report") {
                std::string own, recruits;
                for (const auto& [cur, sum] : t.ownCredit) own += (own.empty() ? "" : ", ") + sum + " " + cur;
                for (const auto& [cur, sum] : t.recruitCredit) recruits += (recruits.empty() ? "" : ", ") + sum + " " + cur;
                std::printf("                 %zu sale(s) with their codes: %s; %zu recruit sale(s): %s\n", t.ownSales, own.empty() ? "0.00" : own.c_str(),
                            t.recruitSales, recruits.empty() ? "0.00" : recruits.c_str());
            }
        }
        return 0;
    }
    if (pos.size() < 3) return Usage(stderr);
    if (sub == "show") {
        const auto a = store.db.GetAffiliate(pos[2]);
        if (!a) return Fail(a.error);
        std::printf("%s  %s\ncontact %s\nshare %d%%, recruit share %d%%\nrecruited by %s\n%s\njoin link %s\n", a.Value().id.c_str(), a.Value().name.c_str(),
                    a.Value().contact.c_str(), a.Value().sharePercent, a.Value().recruitSharePercent,
                    a.Value().recruitedBy.empty() ? "-" : a.Value().recruitedBy.c_str(), a.Value().enabled ? "enabled" : "disabled", JoinLink(Base(store), a.Value().id).c_str());
        return 0;
    }
    if (sub == "enable" || sub == "disable") {
        auto a = store.db.GetAffiliate(pos[2]);
        if (!a) return Fail(a.error);
        a.Value().enabled = sub == "enable";
        if (const auto r = store.db.PutAffiliate(a.Value()); !r) return Fail(r.error);
        return 0;
    }
    if (sub == "remove") {
        if (const auto r = store.db.RemoveAffiliate(pos[2]); !r) return Fail(r.error);
        return 0;
    }
    if (sub == "qr") {
        if (pos.size() < 4) return Usage(stderr);
        if (const auto a = store.db.GetAffiliate(pos[2]); !a) return Fail(a.error);
        if (Base(store).empty()) return Fail("set catalog.base_url first: the join link needs it");
        if (const auto r = WriteQrCodeSvg(JoinLink(Base(store), pos[2]), pos[3]); !r) return Fail(r.error);
        std::printf("wrote %s: %s\n", pos[3].c_str(), JoinLink(Base(store), pos[2]).c_str());
        return 0;
    }
    return Usage(stderr);
}

int CmdCode(Store& store, const std::vector<std::string>& args) {
    if (!store.Open(false)) return 1;
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    const std::string& sub = pos[1];
    if (sub == "list") {
        const auto all = store.db.Codes();
        if (!all) return Fail(all.error);
        if (all.Value().empty()) { std::printf("no code yet\n"); return 0; }
        for (const CodeRecord& c : all.Value()) {
            std::string what = UltraStore::DiscountLabel(c.code);
            std::printf("%-16s %-9s %-16s %-14s %s%s%s\n", c.code.code.c_str(), UltraStore::CodeKindName(c.code.kind).c_str(), c.code.affiliate.c_str(),
                        what.empty() ? "-" : what.c_str(), c.code.apps.empty() ? "every app" : JoinWords(c.code.apps).c_str(),
                        c.code.validUntil.empty() ? "" : ("  until " + c.code.validUntil).c_str(), c.enabled ? "" : "  (off)");
        }
        return 0;
    }
    if (pos.size() < 3) return Usage(stderr);
    const std::string code = UltraStore::NormalizeCode(pos[2]);
    if (sub == "new") {
        CodeRecord r;
        r.code.code = code;
        r.code.affiliate = Option(args, "--affiliate");
        r.code.kind = r.code.affiliate.empty() ? UltraStore::CodeKind::Discount : UltraStore::CodeKind::Affiliate;
        r.code.discountPercent = std::atoi(Option(args, "--percent", "0").c_str());   // locale-ok: digits only
        r.code.discountAmount = Option(args, "--amount");
        r.code.currency = Option(args, "--currency");
        r.code.apps = ListOption(args, "--apps");
        r.code.validFrom = Option(args, "--from");
        r.code.validUntil = Option(args, "--until");
        if (const auto put = store.db.PutCode(r); !put) return Fail(put.error);
        std::printf("code %s: %s\n  link %s\n  store link %s\n", code.c_str(), UltraStore::DiscountLabel(r.code).empty() ? "partner code" : UltraStore::DiscountLabel(r.code).c_str(),
                    CodeLink(Base(store), code).c_str(), StoreLink(code).c_str());
        return 0;
    }
    if (sub == "enable" || sub == "disable") {
        auto r = store.db.GetCode(code);
        if (!r) return Fail(r.error);
        r.Value().enabled = sub == "enable";
        if (const auto put = store.db.PutCode(r.Value()); !put) return Fail(put.error);
        return 0;
    }
    if (sub == "remove") {
        if (const auto r = store.db.RemoveCode(code); !r) return Fail(r.error);
        return 0;
    }
    if (sub == "link") {
        if (const auto r = store.db.GetCode(code); !r) return Fail(r.error);
        std::printf("%s\n%s\n", CodeLink(Base(store), code).c_str(), StoreLink(code).c_str());
        return 0;
    }
    if (sub == "qr") {
        if (pos.size() < 4) return Usage(stderr);
        if (const auto r = store.db.GetCode(code); !r) return Fail(r.error);
        if (Base(store).empty()) return Fail("set catalog.base_url first: the link needs it");
        if (const auto r = WriteQrCodeSvg(CodeLink(Base(store), code), pos[3]); !r) return Fail(r.error);
        std::printf("wrote %s: %s\n", pos[3].c_str(), CodeLink(Base(store), code).c_str());
        return 0;
    }
    return Usage(stderr);
}

int CmdSale(Store& store, const std::vector<std::string>& args) {
    if (!store.Open(false)) return 1;
    const auto pos = Positional(args);
    if (pos.size() < 2) return Usage(stderr);
    if (pos[1] == "add") {
        if (pos.size() < 6) return Usage(stderr);
        const auto sale = RecordSale(store.db, pos[2] == "-" ? std::string() : pos[2], pos[3], pos[4], pos[5], Option(args, "--at"));
        if (!sale) return Fail(sale.error);
        const SaleRecord& s = sale.Value();
        std::printf("sale %lld: %s %s %s", static_cast<long long>(s.id), s.app.c_str(), s.amount.c_str(), s.currency.c_str());
        if (!s.affiliate.empty()) std::printf("; %s earns %s", s.affiliate.c_str(), s.affiliateCredit.c_str());
        if (!s.recruiter.empty()) std::printf("; %s earns %s for recruiting them", s.recruiter.c_str(), s.recruiterCredit.c_str());
        std::printf("\n");
        return 0;
    }
    if (pos[1] == "list") {
        const auto all = store.db.Sales();
        if (!all) return Fail(all.error);
        if (all.Value().empty()) { std::printf("no sale yet\n"); return 0; }
        for (const SaleRecord& s : all.Value()) {
            std::printf("%4lld  %s  %-12s %-24s %10s %s  %s %s  %s %s\n", static_cast<long long>(s.id), s.at.c_str(), s.code.c_str(), s.app.c_str(), s.amount.c_str(), s.currency.c_str(),
                        s.affiliate.c_str(), s.affiliate.empty() ? "" : s.affiliateCredit.c_str(), s.recruiter.c_str(), s.recruiter.empty() ? "" : s.recruiterCredit.c_str());
        }
        return 0;
    }
    return Usage(stderr);
}

} // namespace

int UltraStoreAdmin::Cli::Run(const std::vector<std::string>& argsIn) {
    std::vector<std::string> args = argsIn;
    Store store;
    store.dir = DefaultAdminStoreDir();
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--store") {
            store.dir = args[i + 1];
            args.erase(args.begin() + static_cast<long>(i), args.begin() + static_cast<long>(i) + 2);
            break;
        }
    }
    if (args.empty()) return Usage(stderr);
    const std::string& cmd = args[0];
    if (cmd == "--help" || cmd == "-h" || cmd == "help") return Usage(stdout);
    if (cmd == "--version" || cmd == "-V") { std::printf("ultrastoreadmin %s\n", ULTRASTOREADMIN_VERSION); return 0; }
    if (cmd == "init")      return CmdInit(store);
    if (cmd == "settings")  return CmdSettings(store, args);
    if (cmd == "key")       return CmdKey(store, args);
    if (cmd == "publisher") return CmdPublisher(store, args);
    if (cmd == "app")       return CmdApp(store, args);
    if (cmd == "release")   return CmdRelease(store, args);
    if (cmd == "package")   return CmdPackage(store, args);
    if (cmd == "translate") return CmdTranslate(store, args);
    if (cmd == "build")     return CmdBuild(store, args);
    if (cmd == "publish")   return CmdPublish(store, args);
    if (cmd == "upload")    return CmdUpload(store, args);
    if (cmd == "affiliate") return CmdAffiliate(store, args);
    if (cmd == "code")      return CmdCode(store, args);
    if (cmd == "sale")      return CmdSale(store, args);
    std::fprintf(stderr, "ultrastoreadmin: unknown command \"%s\"\n", cmd.c_str());
    return Usage(stderr);
}
