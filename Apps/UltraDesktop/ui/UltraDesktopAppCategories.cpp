// Apps/UltraDesktop/ui/UltraDesktopAppCategories.cpp
// Where an application goes in the Apps window. See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopAppCategories.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <utility>

using namespace UltraCanvas;

namespace UltraDesktop {

const char* const kOtherSubcategory = "Other";

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

// ===== WINDOWS: NAMES, PROGRAMS AND START-MENU FOLDERS =====
// An entry without Categories= (a Start-menu shortcut, a Store application)
// gets the ones its words suggest. A key is matched against whole words of
// the name, the program file's name and the shortcut's folder ("media
// player" against "Windows Media Player Legacy"); English and German, the
// languages ULTRA OS is used in.
struct Guess {
    const char* words;
    const char* categories;   // as Categories= would have them
};
const Guess kGuesses[] = {
    // The table is read top to bottom and the first key found wins, so the
    // keys that must beat a broader one come first: Windows Defender's
    // firewall console is a system tool, not the antivirus "defender" below;
    // Windows Media Player is a player before anything "windows" says.
    {"firewall", "System;Security"},
    {"media player", "AudioVideo;Video;Player"},
    {"uninstall", "System;PackageManager"},
    {"deinstallieren", "System;PackageManager"},
    {"command line", "System;TerminalEmulator"},
    {"registrierungs", "System"},
    {"windows security", "System;Security"},
    {"lav audio", "AudioVideo;Audio"},
    {"music maker", "AudioVideo;Audio;Sequencer"},
    {"smart microphone", "AudioVideo;Audio;HardwareSettings"},
    {"microphone", "AudioVideo;Audio;HardwareSettings"},
    {"mikrofon", "AudioVideo;Audio;HardwareSettings"},
    {"fax und scan", "Office;Scanning"},
    {"fax and scan", "Office;Scanning"},
    {"downloader+", "Network;FileTransfer"},
    {"windows media", "AudioVideo;Video;Player"},
    {"clipchamp", "AudioVideo;Video;AudioVideoEditing"},
    // Office
    {"word", "Office;WordProcessor"},
    {"wordpad", "Office;WordProcessor"},
    {"writer", "Office;WordProcessor"},
    {"texter", "Office;WordProcessor"},
    {"excel", "Office;Spreadsheet"},
    {"calc", "Office;Spreadsheet"},
    {"powerpoint", "Office;Presentation"},
    {"impress", "Office;Presentation"},
    {"onenote", "Office;WordProcessor"},
    {"access", "Office;Database"},
    {"base", "Office;Database"},
    {"publisher", "Office;Publishing"},
    {"visio", "Office;Flowchart"},
    {"project", "Office;ProjectManagement"},
    {"libreoffice", "Office"},
    {"openoffice", "Office"},
    {"office", "Office"},
    {"acrobat", "Office;Viewer"},
    {"pdf", "Office;Viewer"},
    {"reader", "Office;Viewer"},
    {"calendar", "Office;Calendar"},
    {"kalender", "Office;Calendar"},
    {"contacts", "Office;ContactManagement"},
    {"kontakte", "Office;ContactManagement"},
    {"people", "Office;ContactManagement"},
    {"exodus", "Office;Finance"},
    {"wallet", "Office;Finance"},
    {"finance", "Office;Finance"},
    {"ultradbadmin", "Office;Database"},
    {"ultracanvastexter", "Office;WordProcessor"},
    {"pdf24", "Office;Viewer"},
    {"to do", "Office;ProjectManagement"},
    {"sticky notes", "Office;ProjectManagement"},
    {"warenwirtschaft", "Office;Finance"},
    {"selectline", "Office;Finance"},
    {"moon atlas", "Office;Science"},
    {"planetarium", "Office;Science"},
    {"stellarium", "Office;Science"},
    // Internet
    {"outlook", "Network;Email"},
    {"thunderbird", "Network;Email"},
    {"mail", "Network;Email"},
    {"ultramail", "Network;Email"},
    {"chrome", "Network;WebBrowser"},
    {"chromium", "Network;WebBrowser"},
    {"firefox", "Network;WebBrowser"},
    {"edge", "Network;WebBrowser"},
    {"opera", "Network;WebBrowser"},
    {"brave", "Network;WebBrowser"},
    {"vivaldi", "Network;WebBrowser"},
    {"browser", "Network;WebBrowser"},
    {"duckduckgo", "Network;WebBrowser"},
    {"tor", "Network;WebBrowser"},
    {"teams", "Network;Chat"},
    {"zoom", "Network;VideoConference"},
    {"skype", "Network;Chat"},
    {"discord", "Network;Chat"},
    {"telegram", "Network;Chat"},
    {"whatsapp", "Network;Chat"},
    {"signal", "Network;Chat"},
    {"slack", "Network;Chat"},
    {"chatcube", "Network;Chat"},
    {"dropbox", "Network;FileTransfer"},
    {"onedrive", "Network;FileTransfer"},
    {"filezilla", "Network;FileTransfer"},
    {"winscp", "Network;FileTransfer"},
    {"downloader", "Network;FileTransfer"},
    {"stogram", "Network;FileTransfer"},
    {"putty", "Network;RemoteAccess"},
    {"remote desktop", "Network;RemoteAccess"},
    {"remotedesktop", "Network;RemoteAccess"},
    {"remotedesktopverbindung", "Network;RemoteAccess"},
    {"teamviewer", "Network;RemoteAccess"},
    {"anydesk", "Network;RemoteAccess"},
    {"quick assist", "Network;RemoteAccess"},
    {"line", "Network;Chat"},
    {"phone link", "Network;Telephony"},
    {"xing", "Network"},
    {"linkedin", "Network"},
    {"news", "Network;News"},
    {"tunnelbear", "Network;VPN"},
    {"wireguard", "Network;VPN"},
    {"openvpn", "Network;VPN"},
    {"nordvpn", "Network;VPN"},
    {"vpn", "Network;VPN"},
    // Graphics
    {"paint", "Graphics;RasterGraphics"},
    {"photoshop", "Graphics;RasterGraphics"},
    {"gimp", "Graphics;RasterGraphics"},
    {"krita", "Graphics;RasterGraphics"},
    {"inkscape", "Graphics;VectorGraphics"},
    {"illustrator", "Graphics;VectorGraphics"},
    {"artcreator", "Graphics;VectorGraphics"},
    {"animation", "Graphics;RasterGraphics"},
    {"photos", "Graphics;Photography"},
    {"fotos", "Graphics;Photography"},
    {"lightroom", "Graphics;Photography"},
    {"irfanview", "Graphics;Viewer"},
    {"xnview", "Graphics;Viewer"},
    {"snipping", "Graphics;RasterGraphics"},
    {"snip", "Graphics;RasterGraphics"},
    {"blender", "Graphics;3DGraphics"},
    {"3d", "Graphics;3DGraphics"},
    {"scan", "Graphics;Scanning"},
    {"scanner", "Graphics;Scanning"},
    {"jasc", "Graphics;RasterGraphics"},
    {"xara", "Graphics;VectorGraphics"},
    {"designer", "Graphics;VectorGraphics"},
    // Video
    {"vlc", "AudioVideo;Video;Player"},
    {"movies", "AudioVideo;Video;Player"},
    {"filme", "AudioVideo;Video;Player"},
    {"video", "AudioVideo;Video"},
    {"ultravideo", "AudioVideo;Video;Player"},
    {"camera", "AudioVideo;Video;Recorder"},
    {"kamera", "AudioVideo;Video;Recorder"},
    {"obs", "AudioVideo;Video;Recorder"},
    {"premiere", "AudioVideo;Video;AudioVideoEditing"},
    {"davinci", "AudioVideo;Video;AudioVideoEditing"},
    {"kdenlive", "AudioVideo;Video;AudioVideoEditing"},
    {"handbrake", "AudioVideo;Video;AudioVideoEditing"},
    {"codec", "AudioVideo;Video"},
    {"directvobsub", "AudioVideo;Video"},
    {"netflix", "AudioVideo;Video;Player"},
    {"lav", "AudioVideo;Video"},
    {"madvr", "AudioVideo;Video"},
    {"mediainfo", "AudioVideo;Video"},
    {"movie edit", "AudioVideo;Video;AudioVideoEditing"},
    {"movie studio", "AudioVideo;Video;AudioVideoEditing"},
    {"capture", "AudioVideo;Video;Recorder"},
    {"tiktok", "AudioVideo;Video;Recorder"},
    {"teleprompter", "AudioVideo;Video;Recorder"},
    // Audio
    {"spotify", "AudioVideo;Audio;Player"},
    {"itunes", "AudioVideo;Audio;Player"},
    {"music", "AudioVideo;Audio;Player"},
    {"musik", "AudioVideo;Audio;Player"},
    {"winamp", "AudioVideo;Audio;Player"},
    {"foobar2000", "AudioVideo;Audio;Player"},
    {"audacity", "AudioVideo;Audio;AudioVideoEditing"},
    {"sound recorder", "AudioVideo;Audio;Recorder"},
    {"soundrecorder", "AudioVideo;Audio;Recorder"},
    {"voice recorder", "AudioVideo;Audio;Recorder"},
    {"audiorecorder", "AudioVideo;Audio;Recorder"},
    {"tonaufzeichnung", "AudioVideo;Audio;Recorder"},
    {"dolby", "AudioVideo;Audio"},
    {"audio", "AudioVideo;Audio"},
    {"realtek", "AudioVideo;Audio"},
    // CAD
    {"autocad", "Graphics;Engineering"},
    {"fusion 360", "Graphics;Engineering"},
    {"revit", "Graphics;Engineering"},
    // Games
    {"steam", "Game"},
    {"epic games", "Game"},
    {"gog galaxy", "Game"},
    {"xbox", "Game"},
    {"solitaire", "Game;CardGame"},
    {"minesweeper", "Game;LogicGame"},
    {"chess", "Game;BoardGame"},
    {"schach", "Game;BoardGame"},
    {"mahjong", "Game;BoardGame"},
    {"sudoku", "Game;LogicGame"},
    {"minecraft", "Game;AdventureGame"},
    {"flight simulator", "Game;Simulation"},
    {"simulator", "Game;Simulation"},
    {"dosbox", "Game;Emulator"},
    {"retroarch", "Game;Emulator"},
    {"mame", "Game;Emulator"},
    {"rpcemu", "Game;Emulator"},
    {"rpcemucl", "Game;Emulator"},
    {"emulator", "Game;Emulator"},
    {"game bar", "Game"},
    {"games", "Game"},
    {"spiele", "Game"},
    // Tools
    {"calculator", "Utility;Calculator"},
    {"rechner", "Utility;Calculator"},
    {"notepad", "Utility;TextEditor"},
    {"notepad++", "Utility;TextEditor"},
    {"editor", "Utility;TextEditor"},
    {"7 zip", "Utility;Archiving"},
    {"7zip", "Utility;Archiving"},
    {"winrar", "Utility;Archiving"},
    {"winzip", "Utility;Archiving"},
    {"archive", "Utility;Archiving"},
    {"extract", "Utility;Archiving"},
    {"clock", "Utility;Clock"},
    {"uhr", "Utility;Clock"},
    {"alarms", "Utility;Clock"},
    {"wecker", "Utility;Clock"},
    {"magnifier", "Utility;Accessibility"},
    {"bildschirmlupe", "Utility;Accessibility"},
    {"narrator", "Utility;Accessibility"},
    {"sprachausgabe", "Utility;Accessibility"},
    {"on screen keyboard", "Utility;Accessibility"},
    {"bildschirmtastatur", "Utility;Accessibility"},
    {"antivirus", "Utility;Security"},
    {"internet security", "Utility;Security"},
    {"defender", "Utility;Security"},
    {"malwarebytes", "Utility;Security"},
    {"password", "Utility;Security"},
    {"keepass", "Utility;Security"},
    {"bitwarden", "Utility;Security"},
    {"visual studio", "Development;IDE"},
    {"code", "Development;IDE"},
    {"git", "Development"},
    {"python", "Development"},
    {"copilot", "Utility;AIAssistant"},
    {"claude", "Utility;AIAssistant"},
    {"chatgpt", "Utility;AIAssistant"},
    {"gemini", "Utility;AIAssistant"},
    {"etcher", "Utility;DiscBurning"},
    {"balenaetcher", "Utility;DiscBurning"},
    {"rufus", "Utility;DiscBurning"},
    {"command palette", "Utility"},
    {"github", "Development"},
    {"qt", "Development;IDE"},
    {"google earth", "Utility;Maps"},
    {"maps", "Utility;Maps"},
    {"karten", "Utility;Maps"},
    // System
    {"command center", "System;HardwareSettings"},
    {"lenovo", "System;HardwareSettings"},
    {"dell", "System;HardwareSettings"},
    {"hp", "System;HardwareSettings"},
    {"feedback hub", "System"},
    {"get help", "System"},
    {"hilfe", "System"},
    {"iscsi", "System"},
    {"klick und los", "System"},
    {"click to do", "System"},
    {"komponentendienste", "System"},
    {"component services", "System"},
    {"defragmentieren", "System;Filesystem"},
    {"partition", "System;Filesystem"},
    {"mixed reality", "System"},
    {"odbc", "System"},
    {"health check", "System;Monitor"},
    {"schrittaufzeichnung", "System"},
    {"steps recorder", "System"},
    {"systemkonfiguration", "System;Settings"},
    {"system configuration", "System;Settings"},
    {"msconfig", "System;Settings"},
    {"wiederherstellungslaufwerk", "System"},
    {"recovery drive", "System"},
    {"sicherung", "System"},
    {"backup", "System"},
    {"speicherdiagnose", "System;Monitor"},
    {"memory diagnostic", "System;Monitor"},
    {"windows tools", "System"},
    {"windows-tools", "System"},
    {"administrative tools", "System"},
    {"verwaltungsprogramme", "System"},
    {"windows verwaltungsprogramme", "System"},
    {"system tools", "System"},
    {"systemprogramme", "System"},
    {"control panel", "System;Settings"},
    {"systemsteuerung", "System;Settings"},
    {"settings", "System;Settings"},
    {"einstellungen", "System;Settings"},
    {"command prompt", "System;TerminalEmulator"},
    {"eingabeaufforderung", "System;TerminalEmulator"},
    {"cmd", "System;TerminalEmulator"},
    {"powershell", "System;TerminalEmulator"},
    {"terminal", "System;TerminalEmulator"},
    {"explorer", "System;FileManager"},
    {"ultrafiler", "System;FileManager"},
    {"task manager", "System;Monitor"},
    {"task-manager", "System;Monitor"},
    {"resource monitor", "System;Monitor"},
    {"ressourcenmonitor", "System;Monitor"},
    {"performance monitor", "System;Monitor"},
    {"leistungsuberwachung", "System;Monitor"},
    {"event viewer", "System;Monitor"},
    {"ereignisanzeige", "System;Monitor"},
    {"task scheduler", "System"},
    {"aufgabenplanung", "System"},
    {"services", "System"},
    {"dienste", "System"},
    {"computer management", "System"},
    {"computerverwaltung", "System"},
    {"disk cleanup", "System;Filesystem"},
    {"datentragerbereinigung", "System;Filesystem"},
    {"defragment", "System;Filesystem"},
    {"registry", "System"},
    {"regedit", "System"},
    {"run", "System"},
    {"ausfuhren", "System"},
    {"recovery", "System"},
    {"wiederherstellung", "System"},
    {"system information", "System;Monitor"},
    {"systeminformationen", "System;Monitor"},
    {"amd software", "System;HardwareSettings"},
    {"nvidia", "System;HardwareSettings"},
    {"intel", "System;HardwareSettings"},
    {"brother", "System;HardwareSettings"},
    {"printer", "System;HardwareSettings"},
    {"drucker", "System;HardwareSettings"},
    {"family", "System;Settings"},
    {"get started", "System"},
    {"erste schritte", "System"},
    {"tips", "System"},
    {"store", "System;PackageManager"},
    // A maker's name last, for what nothing above named.
    {"magix", "AudioVideo;Video"},
};

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
    for (const std::string& words : sources) {
        if (words.size() <= 1) continue;
        for (const Guess& guess : kGuesses) {
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
    std::vector<std::string> declared = entry.categories;
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

std::vector<AppGroup> GroupApplications(const std::vector<UCDesktopEntry>& apps,
                                        const AppGroupingOptions& options) {
    std::map<AppCategory, std::vector<std::pair<size_t, std::string>>> placed;   // app, heading
    for (size_t i = 0; i < apps.size(); ++i) {
        if (!MatchesAppFilter(apps[i], options.filter)) continue;
        const AppPlace place = PlaceApplication(apps[i]);
        placed[place.category].push_back({i, place.subcategory});
    }

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
            if (group.count > 0) group.sections.push_back(std::move(section));
        } else {
            const auto found = placed.find(info.category);
            if (found == placed.end()) continue;
            const auto& members = found->second;
            group.count = members.size();
            if (options.splitAbove == 0 || members.size() <= options.splitAbove) {
                AppSection section;
                for (const auto& [index, heading] : members) section.apps.push_back(index);
                group.sections.push_back(std::move(section));
            } else {
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
            }
        }
        if (group.count > 0) groups.push_back(std::move(group));
    }
    return groups;
}

} // namespace UltraDesktop
