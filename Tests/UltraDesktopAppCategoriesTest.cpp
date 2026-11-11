// Tests/UltraDesktopAppCategoriesTest.cpp
// Where UltraDesktop's Apps window puts an application, and what the desktop
// keeps of its history.
//
// An application with freedesktop Categories= goes to the category and the
// sub-category they name (Network;WebBrowser -> Internet, Web browsers; the
// game kinds -> Games' 3D, Retro, Board games, Adventure, Strategy,
// Simulation). A Windows Start-menu shortcut, which names none, is placed by
// its name and its folder, in English and German. GroupApplications splits a
// category only above the threshold, puts "Other" last, and fills Recent
// from the history in its order, at most recentShown. The settings keep the
// history (latest first, once each), the view and the threshold across a save.
// The names of a German Windows 11 Start menu are each checked against the
// category a person would look for them in.
// Version: 1.0.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework

#include "UltraDesktopAppCategories.h"
#include "UltraDesktopSettings.h"

#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using namespace UltraCanvas;
using namespace UltraDesktop;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

UCDesktopEntry Entry(const std::string& name, std::vector<std::string> categories, const std::string& program = "",
                     const std::string& shortcut = "") {
    UCDesktopEntry entry;
    entry.kind = UCDesktopEntry::Kind::Application;
    entry.name = name;
    entry.categories = std::move(categories);
    entry.program = program.empty() ? "/usr/bin/" + name : program;
    entry.exec = entry.program;
    entry.shortcutPath = shortcut;
    return entry;
}

bool Placed(const UCDesktopEntry& entry, AppCategory category, const std::string& sub) {
    const AppPlace place = PlaceApplication(entry);
    return place.category == category && place.subcategory == sub;
}

void TestFreedesktopCategories() {
    std::cout << "Freedesktop categories\n";
    Check(Placed(Entry("Firefox", {"Network", "WebBrowser"}), AppCategory::Internet, "Web browsers"),
          "Network;WebBrowser is Internet, Web browsers");
    Check(Placed(Entry("GIMP", {"Graphics", "2DGraphics", "RasterGraphics"}), AppCategory::Graphics, "Painting"),
          "Graphics;RasterGraphics is Graphics, Painting");
    Check(Placed(Entry("VLC", {"AudioVideo", "Player", "Video"}), AppCategory::Video, "Players"),
          "AudioVideo;Video;Player is Video, Players");
    Check(Placed(Entry("Audacity", {"AudioVideo", "Audio", "AudioVideoEditing"}), AppCategory::Audio, "Editing"),
          "AudioVideo;Audio is Audio");
    Check(Placed(Entry("FreeCAD", {"Graphics", "Science", "Engineering"}), AppCategory::CAD, ""),
          "Engineering is CAD, before Graphics");
    Check(PlaceApplication(Entry("LibreCAD", {"Graphics"})).category == AppCategory::CAD, "a name with CAD is CAD");
    Check(PlaceApplication(Entry("Writer", {"Office", "WordProcessor"})).category == AppCategory::Office, "Office");
    Check(PlaceApplication(Entry("Terminal", {"System", "TerminalEmulator"})).category == AppCategory::System,
          "System");
    Check(Placed(Entry("Calculator", {"Utility", "Calculator"}), AppCategory::Tools, "Calculators"),
          "Utility;Calculator is Tools, Calculators");
    Check(Placed(Entry("Mystery", {}, "/opt/mystery/bin/x"), AppCategory::Tools, ""),
          "nothing known: Tools");
}

void TestGames() {
    std::cout << "Games\n";
    Check(Placed(Entry("SuperTuxKart", {"Game", "ActionGame"}), AppCategory::Games, "3D"), "ActionGame is 3D");
    Check(Placed(Entry("RetroArch", {"Game", "Emulator"}), AppCategory::Games, "Retro"), "Emulator is Retro");
    Check(Placed(Entry("GNU Chess", {"Game", "BoardGame"}), AppCategory::Games, "Board games"),
          "BoardGame is Board games");
    Check(Placed(Entry("Minetest", {"Game", "AdventureGame"}), AppCategory::Games, "Adventure"),
          "AdventureGame is Adventure");
    Check(Placed(Entry("Wesnoth", {"Game", "StrategyGame"}), AppCategory::Games, "Strategy"),
          "StrategyGame is Strategy");
    Check(Placed(Entry("FlightGear", {"Game", "Simulation"}), AppCategory::Games, "Simulation"),
          "Simulation is Simulation");
}

void TestWindowsShortcuts() {
    std::cout << "Windows Start-menu shortcuts (no categories)\n";
    const std::string menu = "C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\";
    Check(Placed(Entry("Excel", {}, "", menu + "Excel.lnk"), AppCategory::Office, "Spreadsheets"),
          "Excel is Office, Spreadsheets");
    Check(Placed(Entry("Outlook", {}, "", menu + "Outlook.lnk"), AppCategory::Internet, "Email"),
          "Outlook is Internet, Email");
    Check(PlaceApplication(Entry("Windows Media Player Legacy", {}, "", menu + "Windows Media Player Legacy.lnk"))
                  .category == AppCategory::Video,
          "Windows Media Player is Video");
    Check(PlaceApplication(Entry("Windows Defender Firewall mit erweiterter Sicherheit", {}, "",
                                 menu + "Windows Tools\\WF.lnk"))
                  .category == AppCategory::System,
          "the Defender firewall console is System");
    Check(PlaceApplication(Entry("Datentr\xC3\xA4gerbereinigung", {}, "", menu + "x.lnk")).category ==
              AppCategory::System,
          "a German name with an umlaut (Datentr\xC3\xA4gerbereinigung) is System");
    Check(PlaceApplication(Entry("Some Tool", {}, "", menu + "Windows Tools\\Some Tool.lnk")).category ==
              AppCategory::System,
          "a shortcut in Windows Tools is System");
    Check(PlaceApplication(Entry("Add to archive", {}, "", menu + "7-Zip\\Add.lnk")).category == AppCategory::Tools,
          "Add to archive is Tools");
    Check(PlaceApplication(Entry("Command Line", {}, "", menu + "cl.lnk")).category == AppCategory::System,
          "\"Command Line\" is a terminal, not the LINE messenger");
    Check(PlaceApplication(Entry("Password Manager", {}, "", menu + "pm.lnk")).subcategory == "Security",
          "a whole word only: \"password\" is not \"word\"");
}

// The Start menu of a German Windows 11 machine, as the Apps window listed
// it: every name must land in the category a person would look in.
void TestWindowsStartMenu() {
    std::cout << "A German Windows Start menu\n";
    struct Expected {
        const char* name;
        AppCategory category;
    };
    using C = AppCategory;
    const Expected expected[] = {
        {"4K Stogram", C::Internet}, {"4K Video Downloader+", C::Internet}, {"Access", C::Office},
        {"Add to archive", C::Tools}, {"AMD Software", C::System}, {"Animation Shop 3", C::Graphics},
        {"Aufgabenplanung", C::System}, {"Ausf\xC3\xBChren", C::System}, {"AVG Internet Security", C::Tools},
        {"AVG Secure Browser", C::Internet}, {"balenaEtcher", C::Tools}, {"Bildschirmlupe", C::Tools},
        {"Bildschirmtastatur", C::Tools}, {"Brother Utilities", C::System}, {"Calculator", C::Tools},
        {"Calendar", C::Office}, {"Camera", C::Video}, {"Claude", C::Tools}, {"Clock", C::Tools},
        {"Codec Tweak Tool", C::Video}, {"Command Center", C::System}, {"Command Palette", C::Tools},
        {"Computerverwaltung", C::System}, {"Copilot", C::Tools}, {"Datentr\xC3\xA4gerbereinigung", C::System},
        {"Dienste", C::System}, {"DirectVobSub", C::Video}, {"Dolby Audio", C::Audio}, {"Dropbox", C::Internet},
        {"DuckDuckGo", C::Internet}, {"Eingabeaufforderung", C::System}, {"Einstellungen", C::System},
        {"Ereignisanzeige", C::System}, {"Erste Schritte", C::System}, {"Excel", C::Office}, {"Exodus", C::Office},
        {"Explorer", C::System}, {"Extract\xE2\x80\xA6", C::Tools}, {"Family", C::System},
        {"Feedback Hub", C::System}, {"FileZilla", C::Internet}, {"Firefox", C::Internet},
        {"Firefox Privater Modus", C::Internet}, {"Game Bar", C::Games}, {"Get Help", C::System},
        {"GitHub", C::Tools}, {"GitHub Desktop", C::Tools}, {"Google Chrome", C::Internet},
        {"Google Earth Pro", C::Tools}, {"IJ Scan Utility", C::Graphics}, {"iSCSI-Initiator", C::System},
        {"Jasc Tube Converter", C::Graphics}, {"KeePass 2", C::Tools}, {"Klick-und-Los", C::System},
        {"Komponentendienste", C::System}, {"Laufwerke defragmentieren und optimieren", C::System},
        {"LAV Audio", C::Audio}, {"LAV Audio (MPC-HC internal)", C::Audio}, {"LAV Splitter", C::Video},
        {"LAV Splitter (MPC-HC internal)", C::Video}, {"LAV Video", C::Video}, {"LAV Video (MPC-HC internal)", C::Video},
        {"Leistungs\xC3\xBC" "berwachung", C::System}, {"Lenovo Hotkeys", C::System},
        {"Lenovo Smart Noise Cancellation", C::System}, {"Lenovo Vantage", C::System}, {"Lenovo Voice", C::System},
        {"LINE", C::Internet}, {"LINE Uninstall", C::System}, {"Live-Untertitel", C::Tools},
        {"Macrorit Partition Extender Free", C::System}, {"Macrorit Partition Expert Free", C::System},
        {"madVR", C::Video}, {"MAGIX Capture", C::Video}, {"MAGIX Connect", C::Video},
        {"MAGIX Movie Edit Pro Plus", C::Video}, {"MAGIX Movie Studio 2025 Suite", C::Video},
        {"MAGIX Travel Maps", C::Tools}, {"Mail", C::Internet}, {"Maps", C::Tools}, {"Media Player", C::Video},
        {"Media Player Classic", C::Video}, {"MediaInfo", C::Video}, {"Microsoft Defender", C::Tools},
        {"Microsoft Edge", C::Internet}, {"Microsoft Store", C::System}, {"Microsoft Teams", C::Internet},
        {"Microsoft To Do", C::Office}, {"Mixed Reality Portal", C::System}, {"Mozilla Thunderbird", C::Internet},
        {"MPC Video Renderer", C::Video}, {"Music Maker (64-Bit)", C::Audio}, {"News", C::Internet},
        {"Notepad", C::Tools}, {"OBS Studio (64bit)", C::Video}, {"ODBC-Datenquellen (32-Bit)", C::System},
        {"ODBC-Datenquellen (64-Bit)", C::System}, {"Office-Spracheinstellungen", C::Office},
        {"OneDrive", C::Internet}, {"OneNote", C::Office}, {"OneNote for Windows 10", C::Office},
        {"Open as archive", C::Tools}, {"OpenOffice", C::Office}, {"OpenOffice Calc", C::Office},
        {"OpenOffice Writer", C::Office}, {"Opera-Browser", C::Internet}, {"Outlook (classic)", C::Internet},
        {"Outlook (new)", C::Internet}, {"Paint", C::Graphics}, {"Paint Shop Pro 7", C::Graphics},
        {"PC Health Check", C::System}, {"PDF24", C::Office}, {"PeaZip", C::Tools}, {"Phone Link", C::Internet},
        {"Photos", C::Graphics}, {"Power Automate", C::Tools}, {"PowerPoint", C::Office},
        {"PowerToys (Preview)", C::Tools}, {"Publisher", C::Office}, {"Qt Creator 16.0.0 (Enterprise)", C::Tools},
        {"Qt Design Studio", C::Tools}, {"Qt Maintenance Tool", C::Tools}, {"Quick Assist", C::Internet},
        {"Realtek Audio Console", C::Audio}, {"Registrierungs-Editor", C::System},
        {"Remotedesktopverbindung", C::Internet}, {"Ressourcenmonitor", C::System},
        {"RPCEmu-Recompiler", C::Games}, {"RPCEmuCL", C::Games}, {"Schrittaufzeichnung", C::System},
        {"SelectLine Warenwirtschaft", C::Office}, {"Smart Microphone Settings", C::Audio},
        {"Snipping Tool", C::Graphics}, {"Solitaire & Casual Games", C::Games}, {"Sound Recorder", C::Audio},
        {"Sprachausgabe", C::Tools}, {"Sprachzugriff", C::Tools}, {"Sticky Notes", C::Office},
        {"Sticky Notes (new)", C::Office}, {"Systeminformationen", C::System}, {"Systemkonfiguration", C::System},
        {"Systemsteuerung", C::System}, {"Task-Manager", C::System}, {"Telegram", C::Internet},
        {"Terminal", C::System}, {"Thunderbird", C::Internet}, {"TikTok LIVE Studio", C::Video},
        {"TunnelBear", C::Internet}, {"UltraCanvasTexter", C::Office}, {"Uninstall Qt", C::System},
        {"USB Image Tool", C::Tools}, {"Virtual Moon Atlas", C::Office}, {"Virtual teleprompter Free", C::Video},
        {"Weather", C::Tools}, {"WhatsApp", C::Internet}, {"Wiederherstellungslaufwerk", C::System},
        {"Windows Defender Firewall mit erweiterter Sicherheit", C::System},
        {"Windows Media Player Legacy", C::Video}, {"Windows PowerShell", C::System},
        {"Windows PowerShell (x86)", C::System}, {"Windows PowerShell ISE", C::System},
        {"Windows Security", C::System}, {"Windows-Fax und -Scan", C::Office}, {"Windows-Sicherung", C::System},
        {"Windows-Speicherdiagnose", C::System}, {"Windows-Tools", C::System}, {"WireGuard", C::Internet},
        {"Word", C::Office}, {"Xara Designer Pro X", C::Graphics}, {"Xara Designer Pro X (19)", C::Graphics},
        {"XBOX", C::Games}, {"Xbox Console Companion", C::Games}, {"XING", C::Internet},
        {"Zeichentabelle", C::Tools}, {"Zoom Workplace", C::Internet},
    };
    int wrong = 0;
    for (const Expected& e : expected) {
        const UCDesktopEntry entry =
            Entry(e.name, {}, "", "C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\" +
                                      std::string(e.name) + ".lnk");
        const AppPlace place = PlaceApplication(entry);
        if (place.category != e.category) {
            ++wrong;
            std::cout << "         " << e.name << ": " << AppCategoryInfoOf(place.category).title << ", not "
                      << AppCategoryInfoOf(e.category).title << "\n";
        }
    }
    Check(wrong == 0, std::to_string(std::size(expected)) + " Start-menu names, each in its category");
}

void CheckNames(const char* what, const std::vector<std::pair<const char*, AppCategory>>& expected,
                const std::string& folder) {
    int wrong = 0;
    for (const auto& [name, category] : expected) {
        const UCDesktopEntry entry =
            Entry(name, {}, "", folder.empty() ? "" : folder + std::string(name) + ".lnk");
        const AppPlace place = PlaceApplication(entry);
        if (place.category != category) {
            ++wrong;
            std::cout << "         " << name << ": " << AppCategoryInfoOf(place.category).title << ", not "
                      << AppCategoryInfoOf(category).title << "\n";
        }
    }
    Check(wrong == 0, std::to_string(expected.size()) + " " + what + ", each in its category");
}

// macOS' own applications, by name (a macOS listing reports no categories
// for most of them), and the type a bundle declares.
void TestMacOS() {
    std::cout << "macOS\n";
    using C = AppCategory;
    CheckNames("macOS applications",
               {{"Pages", C::Office}, {"Numbers", C::Office}, {"Keynote", C::Office}, {"Safari", C::Internet},
                {"Finder", C::System}, {"Xcode", C::Tools}, {"GarageBand", C::Audio}, {"iMovie", C::Video},
                {"Final Cut Pro", C::Video}, {"Logic Pro", C::Audio}, {"Preview", C::Graphics},
                {"TextEdit", C::Tools}, {"Activity Monitor", C::System}, {"Disk Utility", C::System},
                {"System Settings", C::System}, {"App Store", C::System}, {"Messages", C::Internet},
                {"FaceTime", C::Internet}, {"Music", C::Audio}, {"TV", C::Video}, {"Podcasts", C::Audio},
                {"Photo Booth", C::Graphics}, {"QuickTime Player", C::Video}, {"Keychain Access", C::Tools},
                {"Console", C::System}, {"Notes", C::Office}, {"Reminders", C::Office}, {"Books", C::Office},
                {"Stocks", C::Office}, {"Home", C::Tools}, {"Find My", C::Tools}, {"Freeform", C::Office},
                {"Chess", C::Games}, {"Font Book", C::Graphics}, {"Screenshot", C::Graphics},
                {"Migration Assistant", C::System}, {"Time Machine", C::System}, {"VoiceOver Utility", C::Tools},
                {"Grapher", C::Office}, {"Audio MIDI Setup", C::Audio}, {"Image Capture", C::Graphics},
                {"Stickies", C::Office}, {"Dictionary", C::Office}, {"Calculator", C::Tools},
                {"Weather", C::Tools}, {"Passwords", C::Tools}, {"Shortcuts", C::Tools}, {"Automator", C::Tools}},
               "");
    UCDesktopEntry bundle = Entry("Some Game", {"public.app-category.strategy-games"});
    Check(Placed(bundle, AppCategory::Games, "Strategy"), "LSApplicationCategoryType strategy-games is Games, Strategy");
    bundle = Entry("Some Editor", {"public.app-category.developer-tools"});
    Check(PlaceApplication(bundle).category == AppCategory::Tools, "developer-tools is Tools");
}

// Applications people install on every system, by name.
void TestPopularApplications() {
    std::cout << "Popular applications\n";
    using C = AppCategory;
    CheckNames("popular applications",
               {{"Spotify", C::Audio}, {"Slack", C::Internet}, {"Discord", C::Internet}, {"Steam", C::Games},
                {"Visual Studio Code", C::Tools}, {"IntelliJ IDEA Community Edition", C::Tools},
                {"Blender", C::Graphics}, {"OBS Studio", C::Video}, {"VLC media player", C::Video},
                {"Zoom", C::Internet}, {"Notion", C::Office}, {"Obsidian", C::Office}, {"1Password", C::Tools},
                {"Bitwarden", C::Tools}, {"DaVinci Resolve", C::Video}, {"Audacity", C::Audio}, {"GIMP", C::Graphics},
                {"Inkscape", C::Graphics}, {"Krita", C::Graphics}, {"LibreOffice Writer", C::Office},
                {"Docker Desktop", C::Tools}, {"Postman", C::Tools}, {"Oracle VM VirtualBox", C::System},
                {"qBittorrent", C::Internet}, {"Minecraft Launcher", C::Games}, {"Epic Games Launcher", C::Games},
                {"Cities: Skylines", C::Games}, {"Sid Meier's Civilization VI", C::Games},
                {"Stardew Valley", C::Games}, {"Counter-Strike 2", C::Games}, {"RetroArch", C::Games},
                {"Microsoft Solitaire Collection", C::Games}, {"PrusaSlicer", C::CAD}, {"KiCad", C::CAD},
                {"FreeCAD", C::CAD}, {"SketchUp", C::CAD}, {"Signal", C::Internet}, {"Brave", C::Internet},
                {"Adobe Photoshop 2025", C::Graphics}, {"Adobe Premiere Pro 2025", C::Video},
                {"Adobe Acrobat", C::Office}, {"Microsoft Outlook", C::Internet}, {"Thunderbird", C::Internet},
                {"Calibre", C::Office}, {"Zotero", C::Office}, {"Anki", C::Office}, {"GnuCash", C::Office},
                {"FileZilla", C::Internet}, {"WinSCP", C::Internet}, {"PuTTY", C::Internet},
                {"Wireshark", C::Internet}, {"7-Zip File Manager", C::Tools}, {"WinRAR", C::Tools},
                {"Notepad++", C::Tools}, {"Sublime Text", C::Tools}, {"CCleaner", C::System},
                {"HWiNFO64", C::System}, {"CPU-Z", C::System}, {"CrystalDiskInfo", C::System},
                {"Logitech G HUB", C::System}, {"NVIDIA App", C::System}, {"Rufus", C::Tools},
                {"Ventoy", C::Tools}, {"Kodi", C::Video}, {"Plex", C::Video}, {"foobar2000", C::Audio},
                {"MusicBee", C::Audio}, {"Ableton Live 12 Suite", C::Audio}, {"FL Studio 2024", C::Audio},
                {"REAPER", C::Audio}, {"Everything", C::Tools}, {"PowerToys (Preview)", C::Tools},
                {"Total Commander", C::System}, {"WinDirStat", C::System}, {"Macrium Reflect", C::System},
                {"VMware Workstation Pro", C::System}, {"ShareX", C::Graphics}, {"Greenshot", C::Graphics},
                {"Paint.NET", C::Graphics}, {"IrfanView", C::Graphics}, {"Affinity Designer 2", C::Graphics},
                {"Figma", C::Graphics}, {"Canva", C::Graphics}, {"Mixed Reality Portal", C::System}},
               "");
}

void TestGrouping() {
    std::cout << "GroupApplications\n";
    std::vector<UCDesktopEntry> apps;
    for (int i = 0; i < 6; ++i) apps.push_back(Entry("Browser" + std::to_string(i), {"Network", "WebBrowser"}));
    for (int i = 0; i < 4; ++i) apps.push_back(Entry("Mail" + std::to_string(i), {"Network", "Email"}));
    apps.push_back(Entry("NetThing", {"Network"}));
    apps.push_back(Entry("Paint", {"Graphics", "RasterGraphics"}));

    AppGroupingOptions options;
    options.keyOf = [](const UCDesktopEntry& e) { return e.program; };
    options.splitAbove = 8;
    options.recentShown = 2;
    options.recent = {"/usr/bin/Paint", "/usr/bin/gone", "/usr/bin/Mail2", "/usr/bin/Browser1"};
    std::vector<AppGroup> groups = GroupApplications(apps, options);

    Check(!groups.empty() && groups[0].category == AppCategory::Recent, "Recent comes first");
    if (!groups.empty()) {
        const AppSection& recent = groups[0].sections.front();
        Check(recent.apps.size() == 2, "Recent holds recentShown applications");
        Check(recent.apps.size() == 2 && apps[recent.apps[0]].name == "Paint" && apps[recent.apps[1]].name == "Mail2",
              "in the history's order, an application no longer installed left out");
    }
    const AppGroup* internet = nullptr;
    for (const AppGroup& g : groups) {
        if (g.category == AppCategory::Internet) internet = &g;
    }
    Check(internet && internet->count == 11, "Internet holds its 11 applications");
    Check(internet && internet->sections.size() == 3, "above 8 it is split: Email, Web browsers, Other");
    if (internet && internet->sections.size() == 3) {
        Check(internet->sections[0].title == "Email" && internet->sections[1].title == "Web browsers",
              "the headings sorted by title");
        Check(internet->sections[2].title == kOtherSubcategory && internet->sections[2].apps.size() == 1,
              "Other last, with the application that named no kind");
    }

    options.splitAbove = 20;
    groups = GroupApplications(apps, options);
    for (const AppGroup& g : groups) {
        if (g.category == AppCategory::Internet) {
            Check(g.sections.size() == 1 && g.sections[0].title.empty(), "below the threshold it is not split");
        }
    }

    options.filter = "mail";
    groups = GroupApplications(apps, options);
    size_t listed = 0;
    for (const AppGroup& g : groups) {
        if (g.category != AppCategory::Recent) listed += g.count;
    }
    Check(listed == 4, "the filter lists only the matching applications");

    options.filter = "";
    options.recent.clear();
    groups = GroupApplications(apps, options);
    Check(!groups.empty() && groups[0].category != AppCategory::Recent, "no history: no Recent");
}

void TestFoldersAndMoves() {
    std::cout << "Folders and moves\n";
    const std::string menu = "C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\";
    std::vector<UCDesktopEntry> apps;
    const char* magix[] = {"MAGIX Capture", "MAGIX Connect", "MAGIX Movie Edit Pro Plus", "MAGIX Movie Studio",
                           "MAGIX Travel Maps"};
    for (const char* name : magix) apps.push_back(Entry(name, {}, "", menu + "MAGIX\\" + name + ".lnk"));
    const char* tools[] = {"Dienste", "Ereignisanzeige", "Computerverwaltung", "Aufgabenplanung", "Ressourcenmonitor",
                           "Komponentendienste"};
    for (const char* name : tools) apps.push_back(Entry(name, {}, "", menu + "Windows Tools\\" + name + ".lnk"));
    apps.push_back(Entry("Qt Creator", {}, "", menu + "Qt\\Qt Creator.lnk"));
    apps.push_back(Entry("Qt Maintenance Tool", {}, "", menu + "Qt\\Qt Maintenance Tool.lnk"));
    apps.push_back(Entry("Excel", {}, "", menu + "Excel.lnk"));

    Check(AppFolderOf(apps[0]) == "MAGIX", "a shortcut's folder is its Start-menu folder");
    Check(AppFolderOf(apps[5]).empty(), "Windows Tools is Windows' own folder, not a category");
    Check(AppFolderOf(apps.back()).empty(), "a shortcut at the top of the Start menu has none");

    AppGroupingOptions options;
    options.keyOf = [](const UCDesktopEntry& e) { return e.program; };
    std::vector<AppGroup> groups = GroupApplications(apps, options);
    const AppGroup* folder = nullptr;
    bool qt = false;
    for (const AppGroup& g : groups) {
        if (g.key == std::string(kFolderGroupPrefix) + "MAGIX") folder = &g;
        if (g.key == std::string(kFolderGroupPrefix) + "Qt") qt = true;
    }
    Check(folder && folder->count == 5 && folder->title == "MAGIX" && folder->category == AppCategory::Folder,
          "five applications in one folder are a category of their own, named after it");
    Check(!groups.empty() && groups.back().category == AppCategory::Folder, "after the other categories");
    Check(!qt, "two in a folder stay where their names put them");
    size_t system = 0;
    for (const AppGroup& g : groups) {
        if (g.category == AppCategory::System) system = g.count;
    }
    Check(system == 6, "Windows Tools' applications stay in System");

    options.moved["/usr/bin/Excel"] = "games";
    options.moved["/usr/bin/MAGIX Travel Maps"] = "tools";
    options.moved["/usr/bin/Dienste"] = "folder:MAGIX";
    options.moved["/usr/bin/Qt Creator"] = "folder:Gone";
    groups = GroupApplications(apps, options);
    auto countOf = [&groups](const std::string& key) {
        for (const AppGroup& g : groups) {
            if (g.key == key) return g.count;
        }
        return size_t(0);
    };
    Check(countOf("games") == 1, "an application dragged to Games is in Games");
    Check(countOf("folder:MAGIX") == 5, "one dragged out of a folder category and one into it");
    Check(countOf("tools") >= 2, "the one dragged out is where it was dragged (Tools)");
    Check(countOf("tools") + countOf("games") + countOf("system") + countOf("office") + countOf("folder:MAGIX") ==
              apps.size(),
          "a move to a folder category that is not there is ignored");
}

// macOS: the shortcut is the bundle (UltraCanvasMacOSApplications.mm), so a
// maker's folder under /Applications is a category and /Applications is not.
void TestMacOSFolders() {
    std::cout << "macOS folders\n";
    std::vector<UCDesktopEntry> apps;
    const char* adobe[] = {"Adobe Photoshop 2025", "Adobe Illustrator 2025", "Adobe InDesign 2025",
                           "Adobe Bridge 2025", "Adobe Acrobat"};
    for (const char* name : adobe) {
        apps.push_back(Entry(name, {}, "", std::string("/Applications/Adobe Creative Cloud/") + name + ".app"));
    }
    const char* top[] = {"Safari", "Mail", "Pages", "Numbers", "Keynote", "Xcode", "GarageBand"};
    for (const char* name : top) apps.push_back(Entry(name, {}, "", std::string("/Applications/") + name + ".app"));
    const char* utilities[] = {"Terminal", "Activity Monitor", "Disk Utility", "Console", "Script Editor"};
    for (const char* name : utilities) {
        apps.push_back(Entry(name, {}, "", std::string("/System/Applications/Utilities/") + name + ".app"));
    }
    Check(AppFolderOf(apps[0]) == "Adobe Creative Cloud", "a maker's folder under /Applications is a folder");
    Check(AppFolderOf(apps[5]).empty() && AppFolderOf(apps[12]).empty(),
          "/Applications and Utilities are not folders of their own");
    AppGroupingOptions options;
    options.keyOf = [](const UCDesktopEntry& e) { return e.program; };
    const std::vector<AppGroup> groups = GroupApplications(apps, options);
    bool adobeGroup = false, applicationsGroup = false, utilitiesGroup = false;
    for (const AppGroup& g : groups) {
        adobeGroup = adobeGroup || g.key == std::string(kFolderGroupPrefix) + "Adobe Creative Cloud";
        applicationsGroup = applicationsGroup || g.title == "Applications";
        utilitiesGroup = utilitiesGroup || g.title == "Utilities";
    }
    Check(adobeGroup && !applicationsGroup && !utilitiesGroup,
          "five in a maker's folder are a category; the system's folders are none");
}

void TestSettings() {
    std::cout << "Settings\n";
    DesktopSettings settings;
    settings.NoteAppStarted("a");
    settings.NoteAppStarted("b");
    settings.NoteAppStarted("a");
    Check(settings.recentApps == std::vector<std::string>({"a", "b"}), "the latest first, once each");
    for (int i = 0; i < 40; ++i) settings.NoteAppStarted("k" + std::to_string(i));
    Check(settings.recentApps.size() == DesktopSettings::kRecentAppsKept, "at most kRecentAppsKept kept");
    settings.appMoves["/usr/bin/firefox"] = "games";
    settings.appMoves["C:\\x.lnk"] = "folder:MAGIX";
    settings.appsView = "tree";
    settings.appsSplitAbove = 12;
    settings.recentAppsShown = 6;

    DesktopSettings loaded;
    Check(loaded.FromJson(settings.ToJson()), "the settings read back");
    Check(loaded.recentApps == settings.recentApps, "the history survives a save");
    Check(loaded.appMoves == settings.appMoves, "the applications moved to another category survive a save");
    Check(loaded.appsView == "tree" && loaded.appsSplitAbove == 12 && loaded.recentAppsShown == 6,
          "the view, the threshold and Recent's count survive a save");

    DesktopSettings odd;
    Check(odd.FromJson(R"({"appsView":"cards","appsSplitAbove":1,"recentAppsShown":99})"), "odd values read");
    Check(odd.appsView == "tiles" && odd.appsSplitAbove == 2 && odd.recentAppsShown == 24,
          "an unknown view is tiles; the numbers are kept in range");
}

} // namespace

void TestStartupDefaults() {
    std::cout << "\nWhat starts with the desktop by default\n";
    DesktopSettings fresh;
    Check(fresh.StartsWithDesktop("UltraNotifications"), "a new desktop starts UltraNotifications (the bell counts)");
    Check(!fresh.StartsWithDesktop("UltraClipboard") && !fresh.StartsWithDesktop("ChatCube"),
          "and nothing else");

    // A settings file from before the default: given it once.
    DesktopSettings old;
    std::string error;
    Check(old.FromJson(R"({"taskbarEdge":"left","startWithDesktop":["UltraMail"]})", &error),
          "a settings file from before the default reads" + (error.empty() ? "" : ": " + error));
    Check(old.StartsWithDesktop("UltraMail") && old.StartsWithDesktop("UltraNotifications"),
          "it keeps what it had, and gains UltraNotifications once");

    // Unticked afterwards: the choice holds through a save.
    old.SetStartsWithDesktop("UltraNotifications", false);
    DesktopSettings reread;
    Check(reread.FromJson(old.ToJson(), &error), "the settings read back");
    Check(!reread.StartsWithDesktop("UltraNotifications") && reread.StartsWithDesktop("UltraMail"),
          "UltraNotifications unticked stays unticked");

    // An empty list written with the default applied means none.
    DesktopSettings none;
    Check(none.FromJson(R"({"startWithDesktop":[],"startupDefaults":1})", &error) &&
              none.startWithDesktop.empty(),
          "an empty list, the default applied, starts nothing");
}

int main() {
    std::cout << "UltraDesktopAppCategoriesTest\n";
    TestFreedesktopCategories();
    TestGames();
    TestWindowsShortcuts();
    TestWindowsStartMenu();
    TestMacOS();
    TestPopularApplications();
    TestGrouping();
    TestFoldersAndMoves();
    TestMacOSFolders();
    TestSettings();
    TestStartupDefaults();
    std::cout << (g_failures == 0 ? "All passed\n" : std::to_string(g_failures) + " failed\n");
    return g_failures == 0 ? 0 : 1;
}
