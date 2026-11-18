// Apps/UltraPress/cli/UltraPressCli.cpp
// The commands: new, check, page, publish, import, hosts, help, --version.
// Version: 0.7.2 - hosts add --port; an ftp host's scheme checked before it is stored
// Version: 0.7.1 - media prune; a picture's pixels in the list
// Version: 0.6.0 - media: the library listed, files added and removed; check finds missing media
// Version: 0.5.1 - publish --prune; hosts add --key
// Version: 0.5.0 - hosts: the UltraCloud accounts a site is published to; publish --to
// Version: 0.4.1 - help names the window, the same program since there is one
// Version: 0.4.0 - import: a WordPress export as a site
// Version: 0.1.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressCli.h"

#include "UltraPressBlocks.h"
#include "UltraPressHtml.h"
#include "UltraPressImport.h"
#include "UltraPressMedia.h"
#include "UltraPressModel.h"
#include "UltraPressPublish.h"
#include "UltraPressSite.h"

#if ULTRAPRESS_HAS_ULTRASITE
#include "UltraSite/UltraSite.h"
#include "UltraSite/UltraSiteServer.h"
#endif
#include "UltraPressDeploy.h"
#if ULTRAPRESS_HAS_ULTRACLOUD
#include "UltraPressHosting.h"
#include <UltraCloud/UltraCloudFtp.h>   // FtpSchemeKnown, FtpServerOf / FtpServerUrl for --port
#endif

#include "UltraCanvasPathUtf8.h"
#if ULTRAPRESS_HAS_ULTRANET
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <functional>
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
        "UltraPress %s - a website and shop builder that publishes HTML + JavaScript or WebAssembly\n"
        "\n"
        "  UltraPress new <site-dir> [--name <name>] [--url <url>] [--language <code>]\n"
        "      a starter site: site.json, theme.json, menus.json, pages/\n"
        "  UltraPress check <site-dir>\n"
        "      load the site and report every problem, a picture the site does not have included;\n"
        "      exit 1 when there is one\n"
        "  UltraPress media <site-dir> [add <file>... | remove <media-path>... | prune]\n"
        "      the media library: every file under media/ with its kind, size, pixels and uses;\n"
        "      add copies files in under a clean name, remove deletes unused ones, prune every unused one\n"
        "  UltraPress page <site-dir> <slug>\n"
        "      print the page's HTML shadow\n"
        "  UltraPress publish <site-dir> <out-dir> [--output html|wasm] [--renderer <script.js>] [--drafts] [--sizes] [--precompress]\n"
        "                                          [--to <host>|--to site [--path <remote-folder>] [--force] [--prune]]\n"
        "      the site's output (\"output\" in site.json; --output for this run) into <out-dir>:\n"
        "      html - every page styled, with small script modules, a hashed stylesheet,\n"
        "      sitemap.xml, robots.txt, feed.xml, 404.html, .htaccess, _headers and the media;\n"
        "      a broken link or a page over its size budget fails it; --sizes lists the sizes;\n"
        "      wasm - the HTML shadow of every page for the WebAssembly renderer (--renderer);\n"
        "      --to uploads it onto a host (what changed since the last upload),\n"
        "      `--to site` onto the host site.json names; --force uploads everything;\n"
        "      --prune deletes on the host what an earlier publish put there and the site no longer has\n"
        "  UltraPress docs <markdown-dir> <out-dir> [--name <name>] [--url <url>] [--language <code>]\n"
        "                  [--edit-url <base>] [--source-root <dir> --source-url <base>] [--exclude <name>]...\n"
        "                  [--versions <dir>] [--sizes] [--no-budgets] [--broken-links warn] [--precompress] [--quiet]\n"
        "      a folder of Markdown as a documentation site, the way Docusaurus publishes one: a page\n"
        "      per file, the sidebar after the folders, a table of contents, search, links between the\n"
        "      files kept; a link out of the folder goes to --source-url + its path under --source-root\n"
        "  UltraPress serve <site-dir|built-dir> [--port <n>] [--base <path>]\n"
        "      the site on this computer only (127.0.0.1): built as HTML + JavaScript into\n"
        "      <site>/.ultrapress/preview and built again when a file of it changed; a built\n"
        "      folder (index.html) is served as it is; .br / .gz copies go to browsers that take them\n"
        "  UltraPress hosts\n"
        "      the hosts a site can be published to: FTP, FTPS, SFTP, WebDAV, Nextcloud\n"
        "  UltraPress hosts add <ftp|webdav|nextcloud> --server <url> --user <name> [--password <p>]\n"
        "                      [--port <n>] [--key <private-key-file>] [--name <label>] [--folder <remote-folder>] [--no-verify]\n"
        "      a host; the password from --password or ULTRAPRESS_HOST_PASSWORD; the\n"
        "      server is ftp://, ftps://, ftpes:// or sftp:// for ftp, https:// otherwise;\n"
        "      --port when an ftp host's port is not its protocol's own (21; 990 for ftps://, 22 for sftp://);\n"
        "      --key signs an sftp:// host in with that key, the password as its passphrase\n"
        "  UltraPress hosts remove <host>\n"
        "  UltraPress hosts use <site-dir> <host> [--path <remote-folder>]\n"
        "      remember the host and the folder in site.json, for the window's Publish\n"
        "  UltraPress import <export.xml> <site-dir> [--front <slug>] [--media] [--no-drafts]\n"
        "      a WordPress export (Tools > Export) as a site: pages, posts, menus,\n"
        "      the bodies as blocks; --media fetches the attachments into media/\n"
        "  UltraPress <site-dir> [--check]\n"
        "      open the site in the editor window; --check renders every page without one\n"
        "  UltraPress --version\n",
        ULTRAPRESS_VERSION);
}

std::string Option(const std::vector<std::string>& args, const std::string& name, const std::string& fallback) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) return args[i + 1];
    }
    return fallback;
}

// --port: a number from 1 to 65535, or 0 for anything else.
int ParsePort(const std::string& text) {
    if (text.empty() || text.size() > 5) return 0;
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return 0;
        value = value * 10 + (c - '0');
    }
    return value >= 1 && value <= 65535 ? value : 0;
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
            const std::string pixels = file.width > 0 ? std::to_string(file.width) + "\xC3\x97" + std::to_string(file.height) : "";
            std::printf("%-40s %-9s %8s %-11s %s\n", file.path.c_str(), file.kind.c_str(), HumanSize(file.size).c_str(), pixels.c_str(),
                        file.usedBy == 0 ? "unused" : ("used " + std::to_string(file.usedBy) + "x").c_str());
        }
        std::printf("%zu file(s) in %s/media\n", files.size(), dir.c_str());
        return 0;
    }
    const std::string verb = args[1];
    if (verb == "add") {
        if (args.size() < 3) { std::fprintf(stderr, "UltraPress media <site-dir> add <file>...\n"); return 2; }
        int failures = 0;
        for (size_t i = 2; i < args.size(); ++i) {
            std::string path, error;
            if (ImportMedia(dir, args[i], path, error)) std::printf("%s\n", path.c_str());
            else { std::fprintf(stderr, "%s\n", error.c_str()); ++failures; }
        }
        return failures == 0 ? 0 : 1;
    }
    if (verb == "remove") {
        if (args.size() < 3) { std::fprintf(stderr, "UltraPress media <site-dir> remove <media-path>...\n"); return 2; }
        int failures = 0;
        for (size_t i = 2; i < args.size(); ++i) {
            std::string error;
            if (RemoveMedia(dir, args[i], error, &site)) std::printf("removed %s\n", args[i].c_str());
            else { std::fprintf(stderr, "%s\n", error.c_str()); ++failures; }
        }
        return failures == 0 ? 0 : 1;
    }
    if (verb == "prune") {
        std::vector<std::string> removed, errors;
        const bool ok = PruneMedia(dir, site, removed, errors);
        for (const std::string& path : removed) std::printf("removed %s\n", path.c_str());
        for (const std::string& line : errors) std::fprintf(stderr, "%s\n", line.c_str());
        std::printf("%zu unused file(s) removed\n", removed.size());
        return ok ? 0 : 1;
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
    HtmlOptions html;
    html.siteDirectory = args[0];
    HtmlEmitter emitter(site, html);
    std::fputs(emitter.Page(*page).c_str(), stdout);
    return 0;
}

// The site on this computer: built into <site>/.ultrapress/preview and
// served on 127.0.0.1, built again when a file of the site changed since
// (checked before each page is answered). A folder that holds no site but
// an index.html - a built site - is served as it is.
int CommandServe(const std::vector<std::string>& args) {
    if (args.empty()) { PrintHelp(); return 2; }
#if ULTRAPRESS_HAS_ULTRASITE
    namespace fs = std::filesystem;
    const fs::path folder = UltraCanvas::PathFromUtf8(args[0]);
    std::error_code ec;
    const bool isSite = fs::is_regular_file(folder / "site.json", ec);
    std::string served = args[0];
    std::string basePath = "/";
    std::function<void(const std::string&)> refresh;
    // The newest change under the site, the preview folder aside.
    auto newest = [folder]() {
        // min(), not zero: the file clock's epoch may lie in the future
        // (libstdc++'s is 2174), so today's times are negative.
        fs::file_time_type latest = fs::file_time_type::min();
        std::error_code e;
        for (fs::recursive_directory_iterator it(folder, e), end; it != end; it.increment(e)) {
            if (it->path().filename() == ".ultrapress") { it.disable_recursion_pending(); continue; }
            if (it->is_regular_file(e)) latest = std::max(latest, it->last_write_time(e));
        }
        return latest;
    };
    fs::file_time_type built = fs::file_time_type::min();
    if (isSite) {
        served = UltraCanvas::PathToUtf8(folder / ".ultrapress" / "preview");
        auto build = [&, args]() {
            Site site;
            std::vector<std::string> errors;
            if (!LoadSite(args[0], site, errors)) {
                for (const std::string& e : errors) std::fprintf(stderr, "%s\n", e.c_str());
                return false;
            }
            PublishOptions options;
            options.output = kOutputHtml;
            options.enforceBudgets = false;
            const PublishResult result = Publish(site, args[0], served, options);
            for (const std::string& e : result.errors) std::fprintf(stderr, "%s\n", e.c_str());
            basePath = UltraSite::BasePathOf(site.url);
            built = newest();
            return true;
        };
        if (!build()) return 1;
        refresh = [&, build](const std::string& target) {
            const std::string path = target.substr(0, target.find('?'));
            if (!path.empty() && path.back() != '/' && path.find(".html") == std::string::npos) return;   // pages only
            if (newest() != built) {
                std::printf("the site changed: building it again\n");
                build();
            }
        };
    } else if (!fs::is_regular_file(folder / "index.html", ec)) {
        std::fprintf(stderr, "%s holds neither a site (site.json) nor a built one (index.html)\n", args[0].c_str());
        return 1;
    }
    UltraSite::ServeOptions serve;
    serve.port = ParsePort(Option(args, "--port", "3000"));
    if (serve.port == 0) {
        std::fprintf(stderr, "--port takes a number from 1 to 65535\n");
        return 2;
    }
    serve.basePath = Option(args, "--base", basePath);
    serve.beforeRequest = refresh;
    serve.onReady = [&](int port) {
        std::printf("serving %s at http://127.0.0.1:%d%s - Ctrl+C stops\n", served.c_str(), port, serve.basePath.c_str());
        std::fflush(stdout);
    };
    serve.onRequest = [](const std::string& line) {
        std::printf("  %s\n", line.c_str());
        std::fflush(stdout);
    };
    const std::string error = UltraSite::Serve(served, serve);
    if (!error.empty()) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    return 0;
#else
    std::fprintf(stderr, "this build has no UltraSite, so no preview server\n");
    return 1;
#endif
}

// A folder of Markdown as a documentation site: `UltraPress docs`.
int CommandDocs(const std::vector<std::string>& args) {
    if (args.size() < 2) { PrintHelp(); return 2; }
    DocsSiteOptions options;
    options.folder = args[0];
    options.name = Option(args, "--name", options.name);
    options.url = Option(args, "--url", options.url);
    options.language = Option(args, "--language", options.language);
    options.editUrl = Option(args, "--edit-url", "");
    options.sourceRoot = Option(args, "--source-root", "");
    options.sourceUrl = Option(args, "--source-url", "");
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--exclude") options.exclude.push_back(args[i + 1]);
    }
    options.enforceBudgets = !HasFlag(args, "--no-budgets");
    options.brokenLinksWarn = Option(args, "--broken-links", "error") == "warn";
    options.precompress = HasFlag(args, "--precompress");
    options.versionsFolder = Option(args, "--versions", "");
    const PublishResult result = PublishDocsFolder(options, args[1]);
    const bool quiet = HasFlag(args, "--quiet");
    if (!quiet) {
        for (const std::string& warning : result.warnings) std::fprintf(stderr, "warning: %s\n", warning.c_str());
    } else if (!result.warnings.empty()) {
        std::fprintf(stderr, "%zu warnings (without --quiet they are listed)\n", result.warnings.size());
    }
    for (const std::string& error : result.errors) std::fprintf(stderr, "%s\n", error.c_str());
    if (HasFlag(args, "--sizes") && !result.sizeReport.empty()) std::fputs(result.sizeReport.c_str(), stdout);
    std::printf("%s: %zu files written to %s\n", options.name.c_str(), result.files.size(), args[1].c_str());
    return result.ok ? 0 : 1;
}

int CommandPublish(const std::vector<std::string>& args) {
    if (args.size() < 2) { PrintHelp(); return 2; }
    Site site;
    if (!Load(args[0], site)) return 1;
    PublishOptions options;
    options.html.rendererScript = Option(args, "--renderer", "");
    options.includeDrafts = HasFlag(args, "--drafts");
    options.output = Option(args, "--output", "");
    options.precompress = HasFlag(args, "--precompress");
    const PublishResult result = Publish(site, args[0], args[1], options);
    PrintIssues(result.issues);
    for (const std::string& warning : result.warnings) std::fprintf(stderr, "warning: %s\n", warning.c_str());
    for (const std::string& error : result.errors) std::fprintf(stderr, "%s\n", error.c_str());
    if (HasFlag(args, "--sizes") && !result.sizeReport.empty()) std::fputs(result.sizeReport.c_str(), stdout);
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
            std::fprintf(stderr, "%s names no host: `UltraPress hosts use %s <host>` first\n", args[0].c_str(), args[0].c_str());
            return 1;
        }
        hostName = site.hosting.account;
        if (remotePath.empty()) remotePath = site.hosting.path;
    }
    UltraCloud::Account host;
    const UltraCloud::Result found = hosts.Find(hostName, host);
    if (!found) { std::fprintf(stderr, "%s (`UltraPress hosts` lists them)\n", found.message.c_str()); return 1; }
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
            std::printf("no hosts yet: `UltraPress hosts add ftp --server ftp://ftp.example.org --user me`\n");
            return 0;
        }
        for (const UltraCloud::Account& account : list) std::printf("%s\n", DescribeHost(account).c_str());
        return 0;
    }
    const std::string verb = args[0];
    if (verb == "add") {
        if (args.size() < 2 || !Hosts::IsHostProvider(args[1])) {
            std::fprintf(stderr, "UltraPress hosts add <ftp|webdav|nextcloud> --server <url> --user <name>\n");
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
        const std::string portText = Option(args, "--port", "");
        if (account.providerId == "ftp") {
            // Said here as well as by the provider's Verify, so that
            // --no-verify stores no https:// host either.
            if (!UltraCloud::FtpSchemeKnown(account.serverUrl)) {
                std::fprintf(stderr, "--server for an ftp host starts with ftp://, ftps://, ftpes:// or sftp://; %s is not FTP\n",
                             account.serverUrl.c_str());
                return 2;
            }
            // --port goes into the URL, where the window's dialog puts the
            // port it asks for: `--server sftp://my.example --port 2222` and
            // `--server sftp://my.example:2222` store the same host, and a
            // port that is the protocol's own is left out.
            if (!portText.empty()) {
                const int port = ParsePort(portText);
                if (port <= 0) {
                    std::fprintf(stderr, "--port is a number from 1 to 65535\n");
                    return 2;
                }
                UltraCloud::FtpServer server = UltraCloud::FtpServerOf(account.serverUrl);
                server.port = port;
                account.serverUrl = UltraCloud::FtpServerUrl(server);
            }
        } else if (!portText.empty()) {
            std::fprintf(stderr, "--port is for an ftp host; a WebDAV or Nextcloud server's port is part of its URL\n");
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
        if (args.size() < 2) { std::fprintf(stderr, "UltraPress hosts remove <host>\n"); return 2; }
        UltraCloud::Account account;
        const UltraCloud::Result found = hosts.Find(args[1], account);
        if (!found) { std::fprintf(stderr, "%s\n", found.message.c_str()); return 1; }
        const UltraCloud::Result removed = hosts.Remove(account.accountId);
        if (!removed) { std::fprintf(stderr, "not removed: %s\n", removed.message.c_str()); return 1; }
        std::printf("removed %s\n", account.accountId.c_str());
        return 0;
    }
    if (verb == "use") {
        if (args.size() < 3) { std::fprintf(stderr, "UltraPress hosts use <site-dir> <host> [--path <remote-folder>]\n"); return 2; }
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
    std::fprintf(stderr, "this build has no UltraCloud, so no hosts; `UltraPress publish` writes the folder to upload\n");
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
    if (command == "docs") return CommandDocs(args);
    if (command == "serve") return CommandServe(args);
    if (command == "import") return CommandImport(args);
    if (command == "hosts") return CommandHosts(args);
    if (command == "media") return CommandMedia(args);
    std::fprintf(stderr, "unknown command '%s'\n", command.c_str());
    PrintHelp();
    return 2;
}

} // namespace UltraPress
