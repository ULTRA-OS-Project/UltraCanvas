// Apps/UltraPress/cli/UltraPressCli.cpp
// The commands: new, check, page, publish, import, hosts, help, --version.
// Version: 0.6.0 - media: the library listed, files added and removed; check finds missing media
// Version: 0.5.1 - publish --prune; hosts add --key
// Version: 0.5.0 - hosts: the UltraCloud accounts a site is published to; publish --to
// Version: 0.4.1 - help names the window, the same program since there is one
// Version: 0.4.0 - import: a WordPress export as a site
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressCli.h"

#include "UltraPressBlocks.h"
#include "UltraPressHtml.h"
#include "UltraPressImport.h"
#include "UltraPressMedia.h"
#include "UltraPressModel.h"
#include "UltraPressPublish.h"
#include "UltraPressDeploy.h"
#if ULTRAPRESS_HAS_ULTRACLOUD
#include "UltraPressHosting.h"
#endif

#include "UltraCanvasPathUtf8.h"
#if ULTRAPRESS_HAS_ULTRANET
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"
#endif

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifndef ULTRAPRESS_VERSION
#error "ULTRAPRESS_VERSION comes from the first line of Docs/UltraPress/CHANGELOG.md through CMake; never a literal here"
#endif

namespace UltraPress {

namespace {

void PrintHelp() {
    std::printf(
        "UltraPress %s - a website and shop builder that publishes WebAssembly\n"
        "\n"
        "  ultrapress new <site-dir> [--name <name>] [--url <url>] [--language <code>]\n"
        "      a starter site: site.json, theme.json, menus.json, pages/\n"
        "  ultrapress check <site-dir>\n"
        "      load the site and report every problem, a picture the site does not have included;\n"
        "      exit 1 when there is one\n"
        "  ultrapress media <site-dir> [add <file>... | remove <media-path>...]\n"
        "      the media library: every file under media/ with its kind, size and uses;\n"
        "      add copies files in under a clean name, remove deletes unused ones\n"
        "  ultrapress page <site-dir> <slug>\n"
        "      print the page's HTML shadow\n"
        "  ultrapress publish <site-dir> <out-dir> [--renderer <script.js>] [--drafts]\n"
        "                                          [--to <host>|--to site [--path <remote-folder>] [--force] [--prune]]\n"
        "      the static tier: one HTML page per published page, sitemap.xml,\n"
        "      robots.txt, feed.xml, .htaccess and the media, into <out-dir>;\n"
        "      --to uploads it onto a host (what changed since the last upload),\n"
        "      `--to site` onto the host site.json names; --force uploads everything;\n"
        "      --prune deletes on the host what an earlier publish put there and the site no longer has\n"
        "  ultrapress hosts\n"
        "      the hosts a site can be published to: FTP, FTPS, SFTP, WebDAV, Nextcloud\n"
        "  ultrapress hosts add <ftp|webdav|nextcloud> --server <url> --user <name> [--password <p>]\n"
        "                      [--key <private-key-file>] [--name <label>] [--folder <remote-folder>] [--no-verify]\n"
        "      a host; the password from --password or ULTRAPRESS_HOST_PASSWORD; the\n"
        "      server is ftp://, ftps://, ftpes:// or sftp:// for ftp, https:// otherwise;\n"
        "      --key signs an sftp:// host in with that key, the password as its passphrase\n"
        "  ultrapress hosts remove <host>\n"
        "  ultrapress hosts use <site-dir> <host> [--path <remote-folder>]\n"
        "      remember the host and the folder in site.json, for the window's Publish\n"
        "  ultrapress import <export.xml> <site-dir> [--front <slug>] [--media] [--no-drafts]\n"
        "      a WordPress export (Tools > Export) as a site: pages, posts, menus,\n"
        "      the bodies as blocks; --media fetches the attachments into media/\n"
        "  ultrapress <site-dir> [--check]\n"
        "      open the site in the editor window; --check renders every page without one\n"
        "  ultrapress --version\n",
        ULTRAPRESS_VERSION);
}

std::string Option(const std::vector<std::string>& args, const std::string& name, const std::string& fallback) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) return args[i + 1];
    }
    return fallback;
}

bool HasFlag(const std::vector<std::string>& args, const std::string& name) {
    for (const std::string& arg : args) {
        if (arg == name) return true;
    }
    return false;
}

bool Load(const std::string& dir, Site& site) {
    std::vector<std::string> errors;
    const bool ok = LoadSite(dir, site, errors);
    for (const std::string& error : errors) std::fprintf(stderr, "%s\n", error.c_str());
    return ok;
}

void PrintIssues(const std::vector<ValidationIssue>& issues) {
    for (const ValidationIssue& issue : issues) {
        std::fprintf(stderr, "%s%s%s: %s\n", issue.page.c_str(), issue.page.empty() ? "" : " ", issue.path.c_str(),
                     issue.message.c_str());
    }
}

int CommandNew(const std::vector<std::string>& args) {
    if (args.empty()) { PrintHelp(); return 2; }
    const std::string dir = args[0];
    Site site = CreateStarterSite(Option(args, "--name", "My site"),
                                  Option(args, "--url", "https://example.org"),
                                  Option(args, "--language", "en"));
    std::vector<std::string> errors;
    if (!SaveSite(dir, site, errors)) {
        for (const std::string& error : errors) std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("%s: %zu pages, %zu menus\n", dir.c_str(), site.pages.size(), site.menus.size());
    return 0;
}

int CommandCheck(const std::vector<std::string>& args) {
    if (args.empty()) { PrintHelp(); return 2; }
    Site site;
    if (!Load(args[0], site)) return 1;
    std::vector<ValidationIssue> issues = ValidateSite(site);
    const std::vector<ValidationIssue> media = ValidateMedia(site, args[0]);
    issues.insert(issues.end(), media.begin(), media.end());
    PrintIssues(issues);
    std::printf("%s: %zu pages, %zu issues\n", site.name.c_str(), site.pages.size(), issues.size());
    return issues.empty() ? 0 : 1;
}

int CommandMedia(const std::vector<std::string>& args) {
    if (args.empty()) { PrintHelp(); return 2; }
    const std::string dir = args[0];
    Site site;
    if (!Load(dir, site)) return 1;
    if (args.size() == 1) {
        const std::vector<MediaFile> files = ListMedia(dir, &site);
        for (const MediaFile& file : files) {
            std::printf("%-40s %-9s %8s  %s\n", file.path.c_str(), file.kind.c_str(), HumanSize(file.size).c_str(),
                        file.usedBy == 0 ? "unused" : ("used " + std::to_string(file.usedBy) + "x").c_str());
        }
        std::printf("%zu file(s) in %s/media\n", files.size(), dir.c_str());
        return 0;
    }
    const std::string verb = args[1];
    if (verb == "add") {
        if (args.size() < 3) { std::fprintf(stderr, "ultrapress media <site-dir> add <file>...\n"); return 2; }
        int failures = 0;
        for (size_t i = 2; i < args.size(); ++i) {
            std::string path, error;
            if (ImportMedia(dir, args[i], path, error)) std::printf("%s\n", path.c_str());
            else { std::fprintf(stderr, "%s\n", error.c_str()); ++failures; }
        }
        return failures == 0 ? 0 : 1;
    }
    if (verb == "remove") {
        if (args.size() < 3) { std::fprintf(stderr, "ultrapress media <site-dir> remove <media-path>...\n"); return 2; }
        int failures = 0;
        for (size_t i = 2; i < args.size(); ++i) {
            std::string error;
            if (RemoveMedia(dir, args[i], error, &site)) std::printf("removed %s\n", args[i].c_str());
            else { std::fprintf(stderr, "%s\n", error.c_str()); ++failures; }
        }
        return failures == 0 ? 0 : 1;
    }
    std::fprintf(stderr, "unknown media command '%s'\n", verb.c_str());
    return 2;
}

int CommandPage(const std::vector<std::string>& args) {
    if (args.size() < 2) { PrintHelp(); return 2; }
    Site site;
    if (!Load(args[0], site)) return 1;
    const Page* page = site.FindPage(args[1]);
    if (!page) {
        std::fprintf(stderr, "no page '%s'\n", args[1].c_str());
        return 1;
    }
    HtmlEmitter emitter(site);
    std::fputs(emitter.Page(*page).c_str(), stdout);
    return 0;
}

int CommandPublish(const std::vector<std::string>& args) {
    if (args.size() < 2) { PrintHelp(); return 2; }
    Site site;
    if (!Load(args[0], site)) return 1;
    PublishOptions options;
    options.html.rendererScript = Option(args, "--renderer", "");
    options.includeDrafts = HasFlag(args, "--drafts");
    const PublishResult result = PublishStatic(site, args[0], args[1], options);
    PrintIssues(result.issues);
    for (const std::string& error : result.errors) std::fprintf(stderr, "%s\n", error.c_str());
    std::printf("%s: %zu files written to %s\n", site.name.c_str(), result.files.size(), args[1].c_str());
    if (!result.ok) return 1;

    const std::string to = Option(args, "--to", "");
    if (to.empty()) return 0;
#if ULTRAPRESS_HAS_ULTRACLOUD
    Hosts hosts;
    std::string error;
    if (!hosts.Open(error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    std::string hostName = to;
    std::string remotePath = Option(args, "--path", "");
    if (to == "site") {
        if (site.hosting.account.empty()) {
            std::fprintf(stderr, "%s names no host: `ultrapress hosts use %s <host>` first\n", args[0].c_str(), args[0].c_str());
            return 1;
        }
        hostName = site.hosting.account;
        if (remotePath.empty()) remotePath = site.hosting.path;
    }
    UltraCloud::Account host;
    const UltraCloud::Result found = hosts.Find(hostName, host);
    if (!found) { std::fprintf(stderr, "%s (`ultrapress hosts` lists them)\n", found.message.c_str()); return 1; }
    if (remotePath.empty()) remotePath = host.remoteFolder.empty() ? "/" : host.remoteFolder;
    DeployOptions deploy;
    deploy.remoteRoot = remotePath;
    deploy.force = HasFlag(args, "--force");
    deploy.prune = HasFlag(args, "--prune");
    deploy.onProgress = [](const std::string& relative, size_t index, size_t total) {
        std::printf("  [%zu/%zu] %s\n", index, total, relative.c_str());
        std::fflush(stdout);
    };
    std::printf("uploading to %s%s\n", host.accountId.c_str(), JoinRemote(remotePath, "").c_str());
    const DeployResult sent = DeployFolder(args[1], hosts.UploaderFor(host.accountId),
                                           host.accountId + ":" + JoinRemote(remotePath, ""), deploy);
    for (const std::string& line : sent.errors) std::fprintf(stderr, "%s\n", line.c_str());
    std::printf("%s\n", sent.Summary().c_str());
    return sent.ok ? 0 : 1;
#else
    std::fprintf(stderr, "this build has no UltraCloud: the site was written to %s, upload it yourself\n", args[1].c_str());
    return 1;
#endif
}

#if ULTRAPRESS_HAS_ULTRACLOUD
int CommandHosts(const std::vector<std::string>& args) {
    Hosts hosts;
    std::string error;
    if (!hosts.Open(error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    if (args.empty()) {
        const std::vector<UltraCloud::Account> list = hosts.List();
        if (list.empty()) {
            std::printf("no hosts yet: `ultrapress hosts add ftp --server ftp://ftp.example.org --user me`\n");
            return 0;
        }
        for (const UltraCloud::Account& account : list) std::printf("%s\n", DescribeHost(account).c_str());
        return 0;
    }
    const std::string verb = args[0];
    if (verb == "add") {
        if (args.size() < 2 || !Hosts::IsHostProvider(args[1])) {
            std::fprintf(stderr, "ultrapress hosts add <ftp|webdav|nextcloud> --server <url> --user <name>\n");
            return 2;
        }
        UltraCloud::Account account;
        account.providerId = args[1];
        account.serverUrl = Option(args, "--server", "");
        account.username = Option(args, "--user", "");
        account.displayName = Option(args, "--name", "");
        account.remoteFolder = Option(args, "--folder", "");
        if (account.serverUrl.empty() || account.username.empty()) {
            std::fprintf(stderr, "a host needs --server and --user\n");
            return 2;
        }
        if (account.displayName.empty()) account.displayName = account.username + " at " + account.serverUrl;
        UltraCloud::Credentials credentials;
        credentials.username = account.username;
        credentials.password = Option(args, "--password", UltraCanvas::GetEnvUtf8("ULTRAPRESS_HOST_PASSWORD"));
        credentials.privateKeyFile = Option(args, "--key", "");
        if (credentials.password.empty() && credentials.privateKeyFile.empty()) {
            std::fprintf(stderr, "a password: --password <p>, or ULTRAPRESS_HOST_PASSWORD in the environment; or --key <file> for sftp://\n");
            return 2;
        }
        if (!credentials.privateKeyFile.empty() && account.serverUrl.rfind("sftp://", 0) != 0) {
            std::fprintf(stderr, "--key is for an sftp:// server\n");
            return 2;
        }
        const UltraCloud::Result added = hosts.Add(account, credentials, !HasFlag(args, "--no-verify"));
        if (!added) {
            std::fprintf(stderr, "not added: %s\n", added.message.c_str());
            if (!added.diagnostics.empty()) std::fprintf(stderr, "%s\n", added.diagnostics.c_str());
            return 1;
        }
        std::printf("%s\n", DescribeHost(account).c_str());
        return 0;
    }
    if (verb == "remove") {
        if (args.size() < 2) { std::fprintf(stderr, "ultrapress hosts remove <host>\n"); return 2; }
        UltraCloud::Account account;
        const UltraCloud::Result found = hosts.Find(args[1], account);
        if (!found) { std::fprintf(stderr, "%s\n", found.message.c_str()); return 1; }
        const UltraCloud::Result removed = hosts.Remove(account.accountId);
        if (!removed) { std::fprintf(stderr, "not removed: %s\n", removed.message.c_str()); return 1; }
        std::printf("removed %s\n", account.accountId.c_str());
        return 0;
    }
    if (verb == "use") {
        if (args.size() < 3) { std::fprintf(stderr, "ultrapress hosts use <site-dir> <host> [--path <remote-folder>]\n"); return 2; }
        UltraCloud::Account account;
        const UltraCloud::Result found = hosts.Find(args[2], account);
        if (!found) { std::fprintf(stderr, "%s\n", found.message.c_str()); return 1; }
        Site site;
        if (!Load(args[1], site)) return 1;
        site.hosting.account = account.accountId;
        site.hosting.path = Option(args, "--path", account.remoteFolder.empty() ? "/" : account.remoteFolder);
        std::vector<std::string> errors;
        if (!SaveSite(args[1], site, errors)) {
            for (const std::string& line : errors) std::fprintf(stderr, "%s\n", line.c_str());
            return 1;
        }
        std::printf("%s publishes to %s%s\n", site.name.c_str(), account.accountId.c_str(),
                    JoinRemote(site.hosting.path, "").c_str());
        return 0;
    }
    std::fprintf(stderr, "unknown hosts command '%s'\n", verb.c_str());
    return 2;
}
#else
int CommandHosts(const std::vector<std::string>&) {
    std::fprintf(stderr, "this build has no UltraCloud, so no hosts; `ultrapress publish` writes the folder to upload\n");
    return 1;
}
#endif

int CommandImport(const std::vector<std::string>& args) {
    if (args.size() < 2) { PrintHelp(); return 2; }
    const std::string exportPath = args[0];
    const std::string dir = args[1];
    ImportOptions options;
    options.frontPage = Option(args, "--front", "");
    options.includeDrafts = !HasFlag(args, "--no-drafts");
    const bool media = HasFlag(args, "--media");
#if ULTRAPRESS_HAS_ULTRANET
    if (media) {
        const UltraNetResult net = UltraNet_Initialize();
        if (!net.success) std::fprintf(stderr, "UltraNet: %s - the attachments are listed, not fetched\n", net.message.c_str());
        else {
            options.fetchMedia = [&dir](const std::string& url, const std::string& local) {
                const std::filesystem::path target = UltraCanvas::PathFromUtf8(dir) / UltraCanvas::PathFromUtf8(local);
                std::error_code ec;
                std::filesystem::create_directories(target.parent_path(), ec);
                const UltraNetResult result = UltraNet_HttpDownloadFile(url, UltraCanvas::PathToUtf8(target));
                if (!result.success) std::fprintf(stderr, "%s: %s\n", url.c_str(), result.message.c_str());
                return result.success;
            };
        }
    }
#else
    if (media) std::fprintf(stderr, "this build has no UltraNet: the attachments are listed, not fetched\n");
#endif
    Site site;
    ImportReport report;
    const bool ok = ImportWordPressFile(exportPath, options, site, report);
    for (const std::string& warning : report.warnings) std::fprintf(stderr, "%s\n", warning.c_str());
    if (!ok) return 1;
    std::vector<std::string> errors;
    if (!SaveSite(dir, site, errors)) {
        for (const std::string& error : errors) std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    if (!options.fetchMedia && !report.media.empty()) {
        std::printf("%zu attachments to fetch into %s (or run with --media):\n", report.media.size(), dir.c_str());
        for (const auto& [url, local] : report.media) std::printf("  %s -> %s\n", url.c_str(), local.c_str());
    }
    for (const auto& [from, to] : report.redirects) std::printf("redirect %s -> %s\n", from.c_str(), to.c_str());
    std::printf("%s: %s\n", site.name.c_str(), report.Summary().c_str());
#if ULTRAPRESS_HAS_ULTRANET
    if (options.fetchMedia) UltraNet_Shutdown();
#endif
    return 0;
}

} // namespace

int CommandLine(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        PrintHelp();
        return args.empty() ? 2 : 0;
    }
    if (args[0] == "--version" || args[0] == "-v") {
        std::printf("UltraPress %s\n", ULTRAPRESS_VERSION);
        return 0;
    }
    const std::string command = args[0];
    args.erase(args.begin());
    if (command == "new") return CommandNew(args);
    if (command == "check") return CommandCheck(args);
    if (command == "page") return CommandPage(args);
    if (command == "publish") return CommandPublish(args);
    if (command == "import") return CommandImport(args);
    if (command == "hosts") return CommandHosts(args);
    if (command == "media") return CommandMedia(args);
    std::fprintf(stderr, "unknown command '%s'\n", command.c_str());
    PrintHelp();
    return 2;
}

} // namespace UltraPress
