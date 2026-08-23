// Apps/UltraCleaner/main.cpp
// UltraCleaner — finds and removes the files macOS, Windows and Linux leave
// behind: temporary files, application and browser caches, logs, crash
// reports, thumbnail databases, package-manager downloads, developer build
// leftovers and the trash.
//
// Two ways in. Without arguments it opens the UltraCanvas window. With
// --scan / --clean it runs headless, which is what makes it usable over ssh
// and testable in CI. Removal never happens by accident: --clean simulates
// unless it is given --trash or --delete, and --delete additionally needs
// --yes.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraCleanerWindow.h"

#include "UltraCleanerAlbumScanner.h"
#include "UltraCleanerPaths.h"
#include "UltraCleanerRemover.h"
#include "UltraCleanerRules.h"
#include "UltraCleanerScanner.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasModalDialog.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

#ifdef __linux__
#include <X11/Xlib.h>
#include <csignal>
#endif

using namespace UltraCanvas;

namespace {

UltraCanvasApplication* g_app = nullptr;

#ifdef __linux__
void SignalHandler(int) {
    if (g_app) g_app->RequestExit();
    std::exit(EXIT_SUCCESS);
}
#endif

void PrintUsage(const char* programName) {
    std::printf(
        "UltraCleaner - removes the files an operating system leaves behind\n"
        "Powered by the UltraCanvas framework\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "  (no options)      Open the UltraCleaner window\n"
        "  <folder>          Open the window on that photo album\n"
        "  --scan            List what could be removed and exit\n"
        "  --clean           Act on what a scan finds (simulates by default)\n"
        "      --trash       …by moving it to the trash / recycle bin\n"
        "      --delete      …by deleting it permanently (also needs --yes)\n"
        "      --yes         Confirm a permanent delete without a prompt\n"
        "  --all             Include the categories that are normally left\n"
        "                    unticked (trash, installers, large caches)\n"
        "  --rules           Print the rule table for this platform and exit\n"
        "\n"
        "  --album <folder>  Find duplicate and near-duplicate photographs\n"
        "      --level <n>   duplicates | moments (default) | scenes\n"
        "      --within <s>  only group shots taken within <s> seconds of each\n"
        "                    other (ignored where a photo has no capture time)\n"
        "  -v, --version     Show version information\n"
        "  -h, --help        Show this message\n"
        "\n"
        "Examples:\n"
        "  %s --scan\n"
        "  %s --clean --trash\n"
        "  %s --clean --delete --yes\n",
        programName, programName, programName, programName);
}

void PrintRules() {
    const auto rules = UltraCleaner::RulesForCurrentPlatform();
    std::printf("UltraCleaner rules for %s (%zu):\n\n",
                UltraCleaner::PlatformName(UltraCleaner::CurrentPlatform()).c_str(),
                rules.size());
    for (const auto& rule : rules) {
        std::printf("  %-26s %-24s %s\n", rule.id.c_str(),
                    UltraCleaner::CategoryTitle(rule.category).c_str(),
                    rule.safeByDefault ? "" : "(not ticked by default)");
        for (const auto& root : rule.roots) {
            const std::string expanded = UltraCleaner::ExpandTokens(root);
            std::printf("      %-46s -> %s\n", root.c_str(),
                        expanded.empty() ? "(not on this system)" : expanded.c_str());
        }
    }
}

// Prints a scan report the way the GUI shows it: totals, then categories,
// then the largest items.
void PrintReport(const UltraCleaner::ScanReport& report, size_t itemLimit) {
    std::printf("\n%zu removable items, %s in total\n", report.totalItems,
                UltraCleaner::FormatByteSize(report.totalBytes).c_str());
    if (report.skippedProtected > 0) {
        std::printf("%zu candidates were refused by the safety check\n",
                    report.skippedProtected);
    }
    if (report.unreadablePaths > 0) {
        std::printf("%zu locations could not be read\n", report.unreadablePaths);
    }
    std::printf("\n");
    for (const auto& summary : report.categories) {
        std::printf("  %-26s %6zu items  %10s  %s\n",
                    UltraCleaner::CategoryTitle(summary.category).c_str(),
                    summary.itemCount,
                    UltraCleaner::FormatByteSize(summary.totalBytes).c_str(),
                    summary.safeByDefault ? "" : "(not ticked by default)");
    }

    // Largest first — that is the only order worth reading in a terminal.
    std::vector<const UltraCleaner::CleanItem*> sorted;
    sorted.reserve(report.items.size());
    for (const auto& item : report.items) sorted.push_back(&item);
    std::sort(sorted.begin(), sorted.end(),
              [](const UltraCleaner::CleanItem* a, const UltraCleaner::CleanItem* b) {
                  return a->sizeBytes > b->sizeBytes;
              });

    if (!sorted.empty()) std::printf("\nLargest items:\n");
    size_t shown = 0;
    for (const auto* item : sorted) {
        if (shown++ >= itemLimit) break;
        std::printf("  [%s] %10s  %s\n", item->selected ? "x" : " ",
                    UltraCleaner::FormatByteSize(item->sizeBytes).c_str(),
                    item->path.c_str());
    }
    if (sorted.size() > shown) {
        std::printf("  …and %zu more\n", sorted.size() - shown);
    }
}

// Prints an album report: one block per group, keeper marked.
int RunAlbum(const std::string& folder, UltraCleaner::SimilarityLevel level,
             int64_t withinSeconds) {
    using namespace UltraCleaner;

    // Headless: no UltraCanvasApplication has started the image subsystem, and
    // decoding is the whole job here, so start it explicitly.
    if (!UltraCanvas::UCImage::InitializeImageSubsysterm("UltraCleaner")) {
        std::printf("Could not start the image subsystem — is libvips present?\n");
        return EXIT_FAILURE;
    }

    AlbumScanOptions options;
    options.level = level;
    options.maxSecondsApart = withinSeconds;

    AlbumScanner scanner;
    const AlbumScanReport report = scanner.Scan(folder, options);

    std::printf("\n%zu pictures scanned in %s\n", report.picturesScanned,
                folder.c_str());
    std::printf("  %zu photographs, %zu screenshots "
                "(screenshots are matched only when byte-identical)\n",
                report.photographs, report.screenshots);
    std::printf("  level: %s — %s\n", SimilarityLevelName(level),
                SimilarityLevelDescription(level));
    std::printf("\n%zu groups, %zu pictures grouped, %s recoverable\n\n",
                report.groups.size(), report.GroupedPictures(),
                FormatByteSize(report.RecoverableBytes()).c_str());

    int index = 0;
    for (const auto& group : report.groups) {
        std::printf("group %d — %s (%zu pictures, %s recoverable)\n", ++index,
                    GroupReasonName(group.reason), group.members.size(),
                    FormatByteSize(group.RecoverableBytes()).c_str());
        for (size_t i = 0; i < group.members.size(); ++i) {
            const auto& picture = group.members[i];
            std::printf("   %-4s %-42s %5dx%-5d %9s  %s\n",
                        i == group.keeperIndex ? "KEEP" : "",
                        picture.path.c_str(), picture.width, picture.height,
                        FormatByteSize(picture.fileSize).c_str(),
                        picture.capturedAtText.c_str());
        }
        std::printf("\n");
    }
    if (report.groups.empty()) {
        std::printf("Nothing to review — no duplicates or near-duplicates found.\n");
    }
    UltraCanvas::UCImage::ShutdownImageSubsysterm();
    return EXIT_SUCCESS;
}

int RunHeadless(bool clean, UltraCleaner::RemovalMode mode, bool includeAll) {
    using namespace UltraCleaner;

    Scanner scanner;
    ScanReport report = scanner.Scan(RulesForCurrentPlatform());
    if (includeAll) {
        for (auto& item : report.items) item.selected = true;
    }
    PrintReport(report, clean ? 10 : 25);

    if (!clean) return EXIT_SUCCESS;

    Remover remover;
    RemovalOptions options;
    options.mode = mode;
    const RemovalReport result = remover.Remove(report, options);

    std::printf("\n%s %zu items, %s\n",
                result.simulated ? "Would remove"
                : mode == RemovalMode::MoveToTrash ? "Moved to the trash:"
                                                   : "Removed",
                result.removedItems,
                FormatByteSize(result.freedBytes).c_str());
    if (result.skippedMissing > 0) {
        std::printf("%zu items had already gone\n", result.skippedMissing);
    }
    if (result.refusedByGuard > 0) {
        std::printf("%zu items were refused by the safety check\n",
                    result.refusedByGuard);
    }
    for (const auto& failure : result.failures) {
        std::printf("  failed: %s\n    %s\n", failure.path.c_str(),
                    failure.reason.c_str());
    }
    return result.failures.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char* argv[]) {
    bool headlessScan = false;
    bool headlessClean = false;
    bool toTrash = false;
    bool toDelete = false;
    bool confirmed = false;
    bool includeAll = false;
    std::string albumFolder;
    bool openAlbumInWindow = false;
    UltraCleaner::SimilarityLevel level = UltraCleaner::SimilarityLevel::Moments;
    int64_t withinSeconds = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            std::printf("UltraCleaner version %s\nUltraCanvas Framework\n",
                        ULTRACLEANER_VERSION);
            return EXIT_SUCCESS;
        } else if (arg == "--rules") {
            PrintRules();
            return EXIT_SUCCESS;
        } else if (arg == "--scan") {
            headlessScan = true;
        } else if (arg == "--clean") {
            headlessClean = true;
        } else if (arg == "--trash") {
            toTrash = true;
        } else if (arg == "--delete") {
            toDelete = true;
        } else if (arg == "--yes") {
            confirmed = true;
        } else if (arg == "--all") {
            includeAll = true;
        } else if (arg == "--album") {
            if (i + 1 >= argc) {
                std::printf("--album needs a folder\n");
                return EXIT_FAILURE;
            }
            albumFolder = argv[++i];
        } else if (arg == "--level") {
            if (i + 1 >= argc) {
                std::printf("--level needs duplicates, moments or scenes\n");
                return EXIT_FAILURE;
            }
            const std::string value = argv[++i];
            if (value == "duplicates") {
                level = UltraCleaner::SimilarityLevel::Duplicates;
            } else if (value == "scenes") {
                level = UltraCleaner::SimilarityLevel::Scenes;
            } else if (value == "moments") {
                level = UltraCleaner::SimilarityLevel::Moments;
            } else {
                std::printf("unknown level: %s\n", value.c_str());
                return EXIT_FAILURE;
            }
        } else if (arg == "--within") {
            if (i + 1 >= argc) {
                std::printf("--within needs a number of seconds\n");
                return EXIT_FAILURE;
            }
            withinSeconds = std::atoll(argv[++i]);
        } else if (!arg.empty() && arg[0] != '-') {
            // A bare path opens the window on that album, so the app can be
            // started from a file manager or with a folder on the command line.
            albumFolder = arg;
            openAlbumInWindow = true;
        } else {
            std::printf("Unknown argument: %s\nUse --help for usage.\n",
                        arg.c_str());
            return EXIT_FAILURE;
        }
    }

    if (!albumFolder.empty() && !openAlbumInWindow) {
        return RunAlbum(albumFolder, level, withinSeconds);
    }

    if (headlessScan || headlessClean) {
        UltraCleaner::RemovalMode mode = UltraCleaner::RemovalMode::Simulate;
        if (toDelete) {
            if (!confirmed) {
                std::printf("--delete removes files permanently; add --yes to "
                            "confirm, or use --trash instead.\n");
                return EXIT_FAILURE;
            }
            mode = UltraCleaner::RemovalMode::DeletePermanently;
        } else if (toTrash) {
            mode = UltraCleaner::RemovalMode::MoveToTrash;
        }
        return RunHeadless(headlessClean, mode, includeAll);
    }

    UltraCanvasApplication app;
    g_app = &app;

#ifdef __linux__
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    if (!XInitThreads()) {
        debugOutput << "Warning: X11 threading initialization failed" << std::endl;
    }
#endif

    try {
        if (!app.Initialize("UltraCleaner")) {
            debugOutput << "Failed to initialize the UltraCanvas application"
                        << std::endl;
            return EXIT_FAILURE;
        }
        UltraCanvasDialogManager::SetUseNativeDialogs(true);

        UltraCleaner::UltraCleanerWindow window;
        if (!window.Initialize(openAlbumInWindow ? albumFolder : std::string())) {
            debugOutput << "Failed to create the UltraCleaner window" << std::endl;
            return EXIT_FAILURE;
        }
        window.Show();
        app.Run();
    } catch (const std::exception& e) {
        debugOutput << "Unhandled exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
