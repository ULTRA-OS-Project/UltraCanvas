// Apps/UltraDesktop/ui/UltraDesktopAppCategories.h
// Where an application goes in the Apps window: one of ten main categories
// (Recent, Office, Internet, Graphics, Video, Audio, CAD, Games, Tools,
// System) and, when a category holds more applications than the desktop's
// threshold, a sub-category inside it ("Web browsers", "Painting", "Text
// editors"; for Games 3D, Retro, Board games, Adventure, Strategy and
// Simulation).
//
// The category comes from the freedesktop Categories= the application's
// desktop entry declares (Network;WebBrowser -> Internet, Web browsers).
// Windows' Start-menu shortcuts declare none: there the name, the program and
// the Start-menu folder the shortcut lies in are matched against a table of
// well-known applications and words (Outlook -> Internet, Email; a shortcut
// under "Windows Tools" -> System). What matches nothing goes to Tools.
//
// No UI in here: the window (UltraDesktopAppStarter) asks GroupApplications
// for the groups and draws them as tiles or as a tree and a list.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasDesktopEntry.h"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace UltraDesktop {

// Folder: a Start-menu folder big enough to be a category of its own (see
// AppGroupingOptions::folderGroupAt); not one of AppCategories().
enum class AppCategory { Recent, Office, Internet, Graphics, Video, Audio, CAD, Games, Tools, System, Folder };

struct AppCategoryInfo {
    AppCategory category;
    const char* key;      // "office": the id the views and the icon file use
    const char* title;    // "Office"
    const char* icon;     // "office.svg", in media/icons/appstarter/
};

// The ten, in the order the window shows them.
const std::vector<AppCategoryInfo>& AppCategories();
const AppCategoryInfo& AppCategoryInfoOf(AppCategory category);

// Where `entry` belongs. `subcategory` is a heading for people ("Web
// browsers"), "" when nothing more specific is known.
struct AppPlace {
    AppCategory category = AppCategory::Tools;
    std::string subcategory;
};
AppPlace PlaceApplication(const UltraCanvas::UCDesktopEntry& entry);

// The heading the applications without a sub-category get once a category
// is split.
extern const char* const kOtherSubcategory;   // "Other"
// The key of a folder's own category: "folder:MAGIX".
extern const char* const kFolderGroupPrefix;  // "folder:"

// The Start-menu folder `entry`'s shortcut lies in ("MAGIX"), "" for none, for
// a shortcut at the top of the Start menu, and for Windows' own folders
// (Windows Tools, Accessories, System Tools ...; English and German), whose
// applications System and Tools hold. On macOS, where the shortcut is the
// application bundle, the folder below /Applications ("Adobe Photoshop
// 2025"); /Applications and Utilities themselves are no folder of their own.
std::string AppFolderOf(const UltraCanvas::UCDesktopEntry& entry);

// One heading's applications: indices into the list GroupApplications got.
struct AppSection {
    std::string title;              // "" in a category that is not split
    std::vector<size_t> apps;
};

struct AppGroup {
    AppCategory category;
    std::string key;                // AppCategoryInfo::key
    std::string title;
    std::string icon;
    std::vector<AppSection> sections;
    size_t count = 0;               // applications in all sections
};

struct AppGroupingOptions {
    // The application's history key (the desktop's PinKey), for Recent.
    std::function<std::string(const UltraCanvas::UCDesktopEntry&)> keyOf;
    // History keys, the most recently started first.
    std::vector<std::string> recent;
    size_t recentShown = 8;
    // A category with more applications than this is split into its
    // sub-categories; 0 never splits.
    size_t splitAbove = 8;
    // Lower-case words an application's name, generic name or comment must
    // contain to be listed; "" lists every application.
    std::string filter;
    // A Start-menu folder holding at least this many applications (on the
    // host system: a maker's folder - MAGIX, Lenovo, LAV Filters) becomes a
    // category of its own, after the others; 0 never.
    size_t folderGroupAt = 5;
    // Applications the person moved to another category by dragging them:
    // history key -> the group's key ("office", "folder:MAGIX"). A move to a
    // folder category that is no longer there is ignored.
    std::map<std::string, std::string> moved;
};

// The groups to show, in AppCategories' order, empty ones left out, then the
// folders' own categories by name. Recent
// holds the applications of the history that are still installed, up to
// recentShown, in the order they were last started, and is never split.
// Inside a split category the sections are sorted by title, "Other" last;
// the applications keep the order of `apps` (by name).
std::vector<AppGroup> GroupApplications(const std::vector<UltraCanvas::UCDesktopEntry>& apps,
                                        const AppGroupingOptions& options);

// What GroupApplications matches `filter` against, lower-cased.
bool MatchesAppFilter(const UltraCanvas::UCDesktopEntry& entry, const std::string& lowerFilter);

} // namespace UltraDesktop
