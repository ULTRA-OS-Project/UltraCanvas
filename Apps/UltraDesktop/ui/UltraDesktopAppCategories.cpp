// Apps/UltraDesktop/ui/UltraDesktopAppCategories.cpp
// Where an application goes in the Apps window. See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopAppCategories.h"
#include "UltraDesktopAppCatalogue.h"

#include <algorithm>
#include <iterator>
#include <cctype>
#include <map>
#include <set>
#include <utility>

using namespace UltraCanvas;

namespace UltraDesktop {

const char* const kOtherSubcategory = "Other";
const char* const kFolderGroupPrefix = "folder:";

namespace {

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool Has(const std::set<std::string>& categories, const char* name) {
    return categories.count(name) > 0;
}

// ===== FREEDESKTOP CATEGORIES =====
// The additional categories (the second word of Categories=) that name a
// heading, most specific first: the first one an entry declares wins.
struct Heading {
    const char* category;
    const char* title;
};
const Heading kHeadings[] = {
    // Internet
    {"WebBrowser", "Web browsers"},
    {"Email", "Email"},
    {"InstantMessaging", "Chat and calls"},
    {"Chat", "Chat and calls"},
    {"IRCClient", "Chat and calls"},
    {"VideoConference", "Chat and calls"},
    {"Telephony", "Chat and calls"},
    {"FileTransfer", "File transfer"},
    {"P2P", "File transfer"},
    {"News", "News"},
    {"Feed", "News"},
    {"RemoteAccess", "Remote access"},
    {"VPN", "VPN"},
    {"WebDevelopment", "Web development"},
    // Graphics
    {"RasterGraphics", "Painting"},
    {"VectorGraphics", "Drawing"},
    {"3DGraphics", "3D"},
    {"Photography", "Photos"},
    {"Scanning", "Scanning"},
    {"OCR", "Scanning"},
    {"Publishing", "Publishing"},
    // Office
    {"WordProcessor", "Writing"},
    {"Spreadsheet", "Spreadsheets"},
    {"Presentation", "Presentations"},
    {"Calendar", "Calendar and contacts"},
    {"ContactManagement", "Calendar and contacts"},
    {"Database", "Databases"},
    {"Finance", "Finance"},
    {"Dictionary", "Dictionaries"},
    {"ProjectManagement", "Planning"},
    {"Chart", "Planning"},
    {"Flowchart", "Planning"},
    // Audio and video
    {"AudioVideoEditing", "Editing"},
    {"Recorder", "Recorders"},
    {"Mixer", "Mixers"},
    {"Sequencer", "Music making"},
    {"Midi", "Music making"},
    {"Music", "Music making"},
    {"TV", "TV"},
    {"DiscBurning", "Disc burning"},
    {"Player", "Players"},
    // Games
    {"Emulator", "Retro"},
    {"ArcadeGame", "Retro"},
    {"BoardGame", "Board games"},
    {"CardGame", "Board games"},
    {"LogicGame", "Board games"},
    {"BlocksGame", "Board games"},
    {"AdventureGame", "Adventure"},
    {"RolePlaying", "Adventure"},
    {"StrategyGame", "Strategy"},
    {"Simulation", "Simulation"},
    {"ActionGame", "3D"},
    {"Shooter", "3D"},
    // Tools and system
    {"TextEditor", "Text editors"},
    {"Archiving", "Archives"},
    {"Compression", "Archives"},
    {"Calculator", "Calculators"},
    {"Clock", "Clocks"},
    {"Accessibility", "Accessibility"},
    {"Security", "Security"},
    {"IDE", "Development"},
    {"Debugger", "Development"},
    {"Development", "Development"},
    {"TerminalEmulator", "Terminals"},
    {"FileManager", "Files"},
    {"FileTools", "Files"},
    {"Filesystem", "Files"},
    {"Monitor", "Monitors"},
    {"HardwareSettings", "Settings"},
    {"DesktopSettings", "Settings"},
    {"Settings", "Settings"},
    {"PackageManager", "Software"},
    {"HomeAutomation", "Home automation"},
    {"Notes", "Notes"},
    {"Screenshot", "Screenshots"},
    {"Weather", "Weather"},
    {"Clipboard", "Clipboard"},
    {"3DPrinting", "3D printing"},
    {"Electronics", "Electronics"},
    {"Virtualization", "Virtual machines"},
    {"SportsGame", "Simulation"},
    {"KidsGame", "Board games"},
    {"Viewer", "Viewers"},
    {"Maps", "Maps"},
    {"Science", "Science"},
    {"Education", "Education"},
};

// What the main and additional categories say, in the order that settles an
// application listed under several: the specific trades first (CAD, video,
// audio, games), then the broad ones.
AppCategory MainOf(const std::set<std::string>& c, const std::string& name) {
    const std::string lower = Lower(name);
    const bool cadName = name.find("CAD") != std::string::npos || lower.find("kicad") != std::string::npos ||
                         lower.find("sketchup") != std::string::npos ||
                         lower.find("solidworks") != std::string::npos;
    if (Has(c, "Engineering") || Has(c, "Construction") || Has(c, "Electricity") || cadName) return AppCategory::CAD;
    if (Has(c, "Game")) return AppCategory::Games;
    if (Has(c, "Video") || Has(c, "TV")) return AppCategory::Video;
    if (Has(c, "Audio") || Has(c, "Music")) return AppCategory::Audio;
    if (Has(c, "Graphics")) return AppCategory::Graphics;
    if (Has(c, "Office") || Has(c, "Education") || Has(c, "Science")) return AppCategory::Office;
    if (Has(c, "Network")) return AppCategory::Internet;
    if (Has(c, "Development")) return AppCategory::Tools;
    if (Has(c, "System") || Has(c, "Settings")) return AppCategory::System;
    if (Has(c, "AudioVideo")) return AppCategory::Video;
    return AppCategory::Tools;
}

std::string HeadingOf(const std::vector<std::string>& declared, AppCategory main) {
    for (const Heading& heading : kHeadings) {
        if (std::find(declared.begin(), declared.end(), heading.category) == declared.end()) continue;
        // "Development" names Tools' heading; under Office or Internet it is
        // a web designer's tool and says nothing more than the category.
        if (std::string(heading.category) == "Development" && main != AppCategory::Tools) continue;
        // Science and Education name a heading only under Office: FreeCAD's
        // Science says nothing more than CAD does.
        if ((std::string(heading.category) == "Science" || std::string(heading.category) == "Education") &&
            main != AppCategory::Office) {
            continue;
        }
        return heading.title;
    }
    if (main == AppCategory::Games) return "";
    for (const std::string& category : declared) {
        if (category == "Utility") return "Accessories";
    }
    return "";
}

// ===== WINDOWS AND MACOS: NAMES, PROGRAMS AND START-MENU FOLDERS =====
// An entry without Categories= (a Start-menu shortcut, a Store application,
// a macOS bundle without LSApplicationCategoryType) gets the ones its words
// suggest, from the catalogue of well-known applications
// (UltraDesktopAppCatalogue). A key is matched against whole words of the
// name, the program file's name and the shortcut's folder ("media player"
// against "Windows Media Player Legacy").

// German umlauts and ß folded, so "Datenträgerbereinigung" meets its key
// "datentragerbereinigung" (UTF-8 input; any other byte is a separator or
// kept as it is).
std::string FoldWord(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        // General punctuation (… – “ ”) separates words like ASCII's.
        if (c == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80) {
            out += ' ';
            i += 2;
            continue;
        }
        if (c == 0xC3 && i + 1 < text.size()) {
            const unsigned char d = static_cast<unsigned char>(text[i + 1]);
            const char* folded = nullptr;
            switch (d) {
                case 0xA4: case 0x84: folded = "a"; break;   // ä Ä
                case 0xB6: case 0x96: folded = "o"; break;   // ö Ö
                case 0xBC: case 0x9C: folded = "u"; break;   // ü Ü
                case 0x9F: folded = "ss"; break;             // ß
                default: break;
            }
            if (folded) {
                out += folded;
                ++i;
                continue;
            }
        }
        if (std::isalnum(c) || c == '+' || c >= 0x80) {
            out += static_cast<char>(std::tolower(c));
        } else {
            out += ' ';
        }
    }
    return out;
}

// " word word word " - whole words with a space on either side, so a key
// matches only whole words.
std::string Words(const std::string& text) {
    std::string out = " ";
    bool space = true;
    for (char c : FoldWord(text)) {
        if (c == ' ') {
            if (!space) out += ' ';
            space = true;
        } else {
            out += c;
            space = false;
        }
    }
    if (!space) out += ' ';
    return out;
}

std::string FileStem(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string FolderOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return "";
    const std::string parent = path.substr(0, slash);
    const size_t before = parent.find_last_of("/\\");
    return before == std::string::npos ? parent : parent.substr(before + 1);
}

std::vector<std::string> Split(const char* list) {
    std::vector<std::string> out;
    std::string word;
    for (const char* p = list; *p; ++p) {
        if (*p == ';') {
            if (!word.empty()) out.push_back(word);
            word.clear();
        } else {
            word += *p;
        }
    }
    if (!word.empty()) out.push_back(word);
    return out;
}

std::vector<std::string> GuessCategories(const UCDesktopEntry& entry) {
    // The name settles it before the program and the folder: "Excel" in a
    // folder "Microsoft Office Tools" is a spreadsheet.
    const std::string sources[] = {
        Words(entry.name),
        Words(FileStem(entry.program)) + Words(FileStem(entry.shortcutPath)),
        Words(FolderOf(entry.shortcutPath)),
    };
    for (size_t source = 0; source < std::size(sources); ++source) {
        const std::string& words = sources[source];
        if (words.size() <= 1) continue;
        for (const AppGuess& guess : KnownApplications()) {
            if (guess.words[0] == '=') {
                // The whole name, and only the name: "=preview" is macOS'
                // Preview, not "PowerToys (Preview)".
                if (source == 0 && words == " " + std::string(guess.words + 1) + " ") return Split(guess.categories);
                continue;
            }
            if (words.find(" " + std::string(guess.words) + " ") != std::string::npos) return Split(guess.categories);
        }
    }
    return {};
}

} // namespace

const std::vector<AppCategoryInfo>& AppCategories() {
    static const std::vector<AppCategoryInfo> categories = {
        {AppCategory::Recent, "recent", "Recent", "recent.svg"},
        {AppCategory::Office, "office", "Office", "office.svg"},
        {AppCategory::Internet, "internet", "Internet", "internet.svg"},
        {AppCategory::Graphics, "graphics", "Graphics", "graphics.svg"},
        {AppCategory::Video, "video", "Video", "video.svg"},
        {AppCategory::Audio, "audio", "Audio", "audio.svg"},
        {AppCategory::CAD, "cad", "CAD", "cad.svg"},
        {AppCategory::Games, "games", "Games", "games.svg"},
        {AppCategory::Tools, "tools", "Tools", "tools.svg"},
        {AppCategory::System, "system", "System", "system.svg"},
    };
    return categories;
}

const AppCategoryInfo& AppCategoryInfoOf(AppCategory category) {
    for (const AppCategoryInfo& info : AppCategories()) {
        if (info.category == category) return info;
    }
    return AppCategories().front();
}

AppPlace PlaceApplication(const UCDesktopEntry& entry) {
    // macOS' LSApplicationCategoryType, as the backend reports it, read as
    // the freedesktop categories it stands for.
    std::vector<std::string> declared;
    for (const std::string& category : entry.categories) {
        if (category.rfind("public.app-category.", 0) == 0) {
            for (const std::string& c : CategoriesOfMacCategoryType(category)) declared.push_back(c);
        } else {
            declared.push_back(category);
        }
    }
    if (declared.empty()) declared = GuessCategories(entry);
    const std::set<std::string> set(declared.begin(), declared.end());
    AppPlace place;
    place.category = MainOf(set, entry.name);
    if (std::find(declared.begin(), declared.end(), "AIAssistant") != declared.end()) {
        place.subcategory = "AI assistants";
    } else {
        place.subcategory = HeadingOf(declared, place.category);
    }
    return place;
}

bool MatchesAppFilter(const UCDesktopEntry& entry, const std::string& lowerFilter) {
    if (lowerFilter.empty()) return true;
    return Lower(entry.name).find(lowerFilter) != std::string::npos ||
           Lower(entry.genericName).find(lowerFilter) != std::string::npos ||
           Lower(entry.comment).find(lowerFilter) != std::string::npos;
}

std::string AppFolderOf(const UCDesktopEntry& entry) {
    if (entry.shortcutPath.empty()) return "";
    const std::string folder = FolderOf(entry.shortcutPath);
    // Windows' own folders: their applications are the system's, and System
    // and Tools hold them already.
    static const char* const kSystemFolders[] = {
        "programs", "start menu", "startmenu", "startup", "autostart", "accessories", "zubehor",
        "windows accessories", "windows zubehor", "system tools", "systemprogramme", "windows tools",
        "administrative tools", "windows administrative tools", "windows verwaltungsprogramme", "verwaltung",
        "accessibility", "erleichterte bedienung", "windows accessibility", "windows erleichterte bedienung",
        "maintenance", "wartung", "windows powershell", "windows system", "windows systemprogramme",
    };
    std::string words = Words(folder);
    if (words.size() > 2) words = words.substr(1, words.size() - 2);
    for (const char* system : kSystemFolders) {
        if (words == system) return "";
    }
    return folder;
}

std::vector<AppGroup> GroupApplications(const std::vector<UCDesktopEntry>& apps,
                                        const AppGroupingOptions& options) {
    // Where each application goes: a category's key ("office") or a
    // folder's ("folder:MAGIX"), and the heading inside it.
    std::map<std::string, size_t> perFolder;
    if (options.folderGroupAt > 0) {
        for (const UCDesktopEntry& entry : apps) {
            const std::string folder = AppFolderOf(entry);
            if (!folder.empty()) ++perFolder[folder];
        }
    }
    std::map<std::string, std::vector<std::pair<size_t, std::string>>> placed;   // group key -> app, heading
    std::map<std::string, std::string> folderTitles;                             // group key -> folder
    for (size_t i = 0; i < apps.size(); ++i) {
        if (!MatchesAppFilter(apps[i], options.filter)) continue;
        const AppPlace place = PlaceApplication(apps[i]);
        std::string group = AppCategoryInfoOf(place.category).key;
        std::string heading = place.subcategory;
        const std::string folder = AppFolderOf(apps[i]);
        if (!folder.empty() && perFolder[folder] >= options.folderGroupAt && options.folderGroupAt > 0) {
            group = kFolderGroupPrefix + folder;
            folderTitles[group] = folder;
        }
        // The person's own choice, made by dragging the application.
        if (options.keyOf && !options.moved.empty()) {
            const auto moved = options.moved.find(options.keyOf(apps[i]));
            if (moved != options.moved.end() && moved->second != group) {
                const std::string& to = moved->second;
                const bool isFolder = to.rfind(kFolderGroupPrefix, 0) == 0;
                bool known = isFolder ? perFolder[to.substr(std::string(kFolderGroupPrefix).size())] >=
                                            std::max<size_t>(1, options.folderGroupAt)
                                      : false;
                for (const AppCategoryInfo& info : AppCategories()) {
                    if (to == info.key && info.category != AppCategory::Recent) known = true;
                }
                if (known) {
                    group = to;
                    if (isFolder) folderTitles[group] = to.substr(std::string(kFolderGroupPrefix).size());
                    // Its own kind may mean nothing in the new category.
                    if (!isFolder) {
                        bool fits = false;
                        for (const AppCategoryInfo& info : AppCategories()) {
                            if (info.key == to && info.category == place.category) fits = true;
                        }
                        if (!fits) heading.clear();
                    }
                }
            }
        }
        placed[group].push_back({i, heading});
    }

    auto fill = [&options](AppGroup& group, const std::vector<std::pair<size_t, std::string>>& members) {
        group.count = members.size();
        if (options.splitAbove == 0 || members.size() <= options.splitAbove) {
            AppSection section;
            for (const auto& [index, heading] : members) section.apps.push_back(index);
            group.sections.push_back(std::move(section));
            return;
        }
        std::map<std::string, AppSection> byHeading;
        for (const auto& [index, heading] : members) {
            const std::string title = heading.empty() ? kOtherSubcategory : heading;
            AppSection& section = byHeading[title];
            section.title = title;
            section.apps.push_back(index);
        }
        AppSection other;
        for (auto& [title, section] : byHeading) {
            if (title == kOtherSubcategory) {
                other = std::move(section);
            } else {
                group.sections.push_back(std::move(section));
            }
        }
        if (!other.apps.empty()) group.sections.push_back(std::move(other));
    };

    std::vector<AppGroup> groups;
    for (const AppCategoryInfo& info : AppCategories()) {
        AppGroup group;
        group.category = info.category;
        group.key = info.key;
        group.title = info.title;
        group.icon = info.icon;
        if (info.category == AppCategory::Recent) {
            AppSection section;
            if (options.keyOf) {
                for (const std::string& key : options.recent) {
                    if (section.apps.size() >= options.recentShown) break;
                    for (size_t i = 0; i < apps.size(); ++i) {
                        if (options.keyOf(apps[i]) != key || !MatchesAppFilter(apps[i], options.filter)) continue;
                        if (std::find(section.apps.begin(), section.apps.end(), i) == section.apps.end()) {
                            section.apps.push_back(i);
                        }
                        break;
                    }
                }
            }
            group.count = section.apps.size();
            if (group.count > 0) {
                group.sections.push_back(std::move(section));
                groups.push_back(std::move(group));
            }
            continue;
        }
        const auto found = placed.find(info.key);
        if (found == placed.end()) continue;
        fill(group, found->second);
        groups.push_back(std::move(group));
    }
    // The folders big enough to be categories of their own, by name, after
    // the categories.
    for (const auto& [key, title] : folderTitles) {
        const auto found = placed.find(key);
        if (found == placed.end()) continue;
        AppGroup group;
        group.category = AppCategory::Folder;
        group.key = key;
        group.title = title;
        group.icon = "folder.svg";
        fill(group, found->second);
        groups.push_back(std::move(group));
    }
    return groups;
}

} // namespace UltraDesktop
