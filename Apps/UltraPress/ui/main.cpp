// Apps/UltraPress/ui/main.cpp
// ultrapress: the one UltraPress program - the commands and the window.
// A command word first (new, check, page, publish, import, hosts, media, help) runs that
// command without a window (cli/UltraPressCli.h); a site folder opens it in
// the editor - the pages and the current page's blocks on the left, the
// page rendered on real elements in the middle, the selected block's
// properties on the right (ui/UltraPressWindow.h). The renderer is the one
// that becomes the published page under Emscripten.
//
//   ultrapress                       the start window: open a site folder, or create a site
//   ultrapress <site-dir>            open a site folder in the editor
//   ultrapress <site-dir> --check    render every page without a window and
//                                    print the element counts and warnings
//   ultrapress <command> ...         new, check, page, publish, import, hosts, media, help
//   ultrapress --version
//
// There were two programs, ultrapress and UltraPress, until the macOS build
// showed that two names differing only in case are one name on a
// case-insensitive filesystem. One program, like ultrafibu. And like
// ultrafibu, started with nothing it opens a window rather than printing
// its usage: a double-click on Windows gave that usage a console that
// closed before it could be read, which looked like a crash.
// Version: 0.8.4 - the settings in one file; the editor window's size remembered
// Version: 0.8.2 - the sites opened last are remembered for the start window
// Version: 0.8.1 - no argument, or a folder without a site, opens the start window
// Version: 0.8.0 - the look read from the settings folder before the window opens
// Version: 0.6.0 - media
// Version: 0.5.0 - hosts
// Version: 0.4.1 - one program: the commands and the window
// Version: 0.3.0 - editing: blocks added, moved, removed and their properties changed; Save
// Version: 0.2.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressBlocks.h"
#include "UltraPressCli.h"
#include "UltraPressModel.h"
#include "UltraPressRender.h"
#include "UltraPressSettings.h"
#include "UltraPressStart.h"
#include "UltraPressTheme.h"
#include "UltraPressWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTheme.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef ULTRAPRESS_VERSION
#error "ULTRAPRESS_VERSION comes from the first line of Docs/UltraPress/CHANGELOG.md through CMake; never a literal here"
#endif

using namespace UltraCanvas;

namespace {

int Usage() {
    std::fprintf(stderr,
                 "UltraPress %s\n"
                 "  ultrapress                      the start window: open a site folder, or create a site\n"
                 "  ultrapress <site-dir>           open a site folder in the editor\n"
                 "  ultrapress <site-dir> --check   render every page without a window, print what was built\n"
                 "  ultrapress <command> ...        new, check, page, publish, import, hosts, media; `ultrapress help` lists them\n"
                 "  ultrapress --version\n",
                 ULTRAPRESS_VERSION);
    return 2;
}

// Whether the first argument is a command word rather than a site folder.
// A folder that happens to be called `check` is opened as `./check`.
bool IsCommand(const std::string& word) {
    static const char* const kCommands[] = {"new", "check", "page", "publish", "import", "hosts", "media",
                                            "help", "--help", "-h"};
    for (const char* command : kCommands) {
        if (word == command) return true;
    }
    return false;
}

int RunCheck(const UltraPress::Site& site, const std::string& siteDir) {
    UltraPress::RenderOptions options;
    options.mediaRoot = siteDir;
    options.onLink = [](const std::string&) {};
    UltraPress::PageRenderer renderer(site, options);
    int failures = 0;
    for (const UltraPress::Page& page : site.pages) {
        UltraPress::RenderedPage rendered = renderer.Render(page);
        std::printf("%s: %d elements\n", page.slug.c_str(), rendered.elementCount);
        for (const std::string& warning : rendered.warnings) {
            std::printf("  %s\n", warning.c_str());
            if (warning.rfind("unknown block", 0) == 0) ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}

// The site folder's errors as one line for the start window.
std::string FirstError(const std::vector<std::string>& errors, const std::string& siteDir) {
    for (const std::string& error : errors) std::fprintf(stderr, "%s\n", error.c_str());
    return errors.empty() ? "\"" + siteDir + "\" could not be opened as a site." : errors.front();
}

// Static lifetime: under Emscripten Run() never returns and unwinds
// main()'s frame while the loop keeps ticking.
std::unique_ptr<UltraPress::EditorWindow> g_editor;
UltraPress::StartWindow g_start;
std::string g_settingsDir;          // <settings folder>/UltraPress

// Opens the editor on a loaded site: the main window at the size it had
// last, the look, the site. Why not, as one line, or an empty string.
std::string OpenEditor(const std::string& siteDir, UltraPress::Site site, bool dark) {
    for (const UltraPress::ValidationIssue& issue : UltraPress::ValidateSite(site)) {
        std::fprintf(stderr, "%s %s: %s\n", issue.page.c_str(), issue.path.c_str(), issue.message.c_str());
    }
    const UltraPress::UltraPressSettings settings = UltraPress::UltraPressSettings::Load(g_settingsDir);
    WindowConfig cfg;
    cfg.title = site.name + " - UltraPress " ULTRAPRESS_VERSION;
    cfg.width = settings.windowWidth >= 900 ? settings.windowWidth : 1500;
    cfg.height = settings.windowHeight >= 600 ? settings.windowHeight : 920;
    cfg.backgroundColor = UltraCanvasTheme::Current().roles.ground;
    auto window = CreateWindow(cfg);
    if (!window) return "The editor window could not be created.";
    // The size is read off the window as it closes and written once then.
    // The window is captured raw: it owns the callback, so a shared_ptr here
    // would be a cycle, and the pointer is valid for as long as it can run.
    UltraCanvasWindow* raw = window.get();
    window->onWindowClosed = [raw]() {
        int width = 0, height = 0;
        raw->GetWindowSize(width, height);
        if (width <= 0 || height <= 0) return;
        UltraPress::UltraPressSettings latest = UltraPress::UltraPressSettings::Load(g_settingsDir);
        latest.windowWidth = width;
        latest.windowHeight = height;
        latest.Save(g_settingsDir);
    };
    g_editor = std::make_unique<UltraPress::EditorWindow>(std::move(site), siteDir);
    g_editor->SetDark(dark, false);
    g_editor->Build(window);
#if defined(__EMSCRIPTEN__)
    window->Maximize();
#endif
    window->Show();
    return std::string();
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && (args[0] == "--version" || args[0] == "-v")) {
        std::printf("UltraPress %s\n", ULTRAPRESS_VERSION);
        return 0;
    }
    if (!args.empty() && IsCommand(args[0])) return UltraPress::CommandLine(argc, argv);
    const std::string siteDir = args.empty() ? std::string() : args[0];
    bool check = false;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--check") check = true;
        else return Usage();
    }

    if (check) {
        // Without a window: the folder has to be a site, and the answer goes
        // to the prompt that asked.
        UltraPress::Site site;
        std::vector<std::string> errors;
        if (!UltraPress::LoadSite(siteDir, site, errors)) {
            FirstError(errors, siteDir);
            return 1;
        }
        for (const UltraPress::ValidationIssue& issue : UltraPress::ValidateSite(site)) {
            std::fprintf(stderr, "%s %s: %s\n", issue.page.c_str(), issue.path.c_str(), issue.message.c_str());
        }
        return RunCheck(site, siteDir);
    }

    static UltraCanvasApplication app;
    if (!app.Initialize(argv[0])) return 1;

    // The settings - the look the user chose last, before any window is
    // built, so that every element that follows the theme is born in it.
    g_settingsDir = UltraPress::UltraPressSettings::Directory();
    const UltraPress::UltraPressSettings settings = UltraPress::UltraPressSettings::Load(g_settingsDir);
    const bool dark = settings.dark;
    UltraCanvasTheme::SetCurrent(UltraPress::ThemeFor(dark));

    // Open a site folder in the editor; why not, or an empty string. A site
    // that opened is remembered for the start window's list.
    auto open = [dark](const std::string& dir) -> std::string {
        UltraPress::Site site;
        std::vector<std::string> errors;
        if (!UltraPress::LoadSite(dir, site, errors)) return FirstError(errors, dir);
        const std::string error = OpenEditor(dir, std::move(site), dark);
        if (error.empty()) {
            UltraPress::UltraPressSettings latest = UltraPress::UltraPressSettings::Load(g_settingsDir);
            latest.RememberSite(dir);
            latest.Save(g_settingsDir);
        }
        return error;
    };

    // With a site folder that opens, the editor and nothing else. Without
    // one, or with a folder that is not a site, the start window says so and
    // offers to open or create one.
    std::string hint;
    if (siteDir.empty()) {
        hint = "No site is open yet. Open a site folder, or create a new site.";
    } else {
        const std::string error = open(siteDir);
        if (error.empty()) {
            app.Run();
            return 0;
        }
        hint = error;
    }
    g_start.onSiteChosen = open;
    g_start.onSiteGone = [](const std::string& dir) {
        UltraPress::UltraPressSettings latest = UltraPress::UltraPressSettings::Load(g_settingsDir);
        latest.ForgetSite(dir);
        latest.Save(g_settingsDir);
    };
    auto start = g_start.Build(hint, siteDir, settings.recentSites);
    if (!start) return 1;
    start->Show();
    app.Run();
    return 0;
}
