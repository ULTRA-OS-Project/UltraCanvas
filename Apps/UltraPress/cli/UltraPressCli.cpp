// Apps/UltraPress/cli/UltraPressCli.cpp
// The commands: new, check, page, publish, help, --version.
// Version: 0.4.1 - help names the window, the same program since there is one
// Version: 0.4.0 - import: a WordPress export as a site
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressCli.h"

#include "UltraPressBlocks.h"
#include "UltraPressHtml.h"
#include "UltraPressImport.h"
#include "UltraPressModel.h"
#include "UltraPressPublish.h"

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
        "      load the site and report every problem; exit 1 when there is one\n"
        "  ultrapress page <site-dir> <slug>\n"
        "      print the page's HTML shadow\n"
        "  ultrapress publish <site-dir> <out-dir> [--renderer <script.js>] [--drafts]\n"
        "      the static tier: one HTML page per published page, sitemap.xml,\n"
        "      robots.txt, feed.xml, .htaccess and the media, into <out-dir>\n"
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
    const std::vector<ValidationIssue> issues = ValidateSite(site);
    PrintIssues(issues);
    std::printf("%s: %zu pages, %zu issues\n", site.name.c_str(), site.pages.size(), issues.size());
    return issues.empty() ? 0 : 1;
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
    return result.ok ? 0 : 1;
}

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
    std::fprintf(stderr, "unknown command '%s'\n", command.c_str());
    PrintHelp();
    return 2;
}

} // namespace UltraPress
