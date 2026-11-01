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
// Version: 1.0.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework

#include "UltraDesktopAppCategories.h"
#include "UltraDesktopSettings.h"

#include <iostream>
#include <string>
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
    Check(PlaceApplication(Entry("Password Manager", {}, "", menu + "pm.lnk")).subcategory == "Security",
          "a whole word only: \"password\" is not \"word\"");
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

void TestSettings() {
    std::cout << "Settings\n";
    DesktopSettings settings;
    settings.NoteAppStarted("a");
    settings.NoteAppStarted("b");
    settings.NoteAppStarted("a");
    Check(settings.recentApps == std::vector<std::string>({"a", "b"}), "the latest first, once each");
    for (int i = 0; i < 40; ++i) settings.NoteAppStarted("k" + std::to_string(i));
    Check(settings.recentApps.size() == DesktopSettings::kRecentAppsKept, "at most kRecentAppsKept kept");
    settings.appsView = "tree";
    settings.appsSplitAbove = 12;
    settings.recentAppsShown = 6;

    DesktopSettings loaded;
    Check(loaded.FromJson(settings.ToJson()), "the settings read back");
    Check(loaded.recentApps == settings.recentApps, "the history survives a save");
    Check(loaded.appsView == "tree" && loaded.appsSplitAbove == 12 && loaded.recentAppsShown == 6,
          "the view, the threshold and Recent's count survive a save");

    DesktopSettings odd;
    Check(odd.FromJson(R"({"appsView":"cards","appsSplitAbove":1,"recentAppsShown":99})"), "odd values read");
    Check(odd.appsView == "tiles" && odd.appsSplitAbove == 2 && odd.recentAppsShown == 24,
          "an unknown view is tiles; the numbers are kept in range");
}

} // namespace

int main() {
    std::cout << "UltraDesktopAppCategoriesTest\n";
    TestFreedesktopCategories();
    TestGames();
    TestWindowsShortcuts();
    TestGrouping();
    TestSettings();
    std::cout << (g_failures == 0 ? "All passed\n" : std::to_string(g_failures) + " failed\n");
    return g_failures == 0 ? 0 : 1;
}
