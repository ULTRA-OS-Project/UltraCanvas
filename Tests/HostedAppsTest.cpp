// Tests/HostedAppsTest.cpp
// Applications hosted inside their program (UltraCanvasHostedApps): the
// registry, the "hosted:<id>" shortcut paths, and the program names an
// application stands in for - UltraCanvasDesktopShell::LaunchProgram starts
// the hosted one with the program's arguments (MobilePC's UltraFiler, started
// by UltraDesktop's taskbar button) and still looks for an executable for any
// other name.
// No platform and no display: nothing is drawn.

#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasHostedApps.h"

#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!condition) ++g_failures;
}

} // namespace

int main() {
    std::vector<std::string> filerArguments;
    int filerStarts = 0;
    std::string launchingInside;
    UltraCanvasHostedApps::Register({
        "ultrafiler", "UltraFiler", "Files and folders", "", {"System", "FileManager"},
        [&](const std::vector<std::string>& arguments) {
            ++filerStarts;
            filerArguments = arguments;
            launchingInside = UltraCanvasHostedApps::Launching();
            return true;
        },
        {"UltraFiler"}});
    UltraCanvasHostedApps::Register({
        "broken", "Broken", "", "", {}, [](const std::vector<std::string>&) { return false; }, {"Broken"}});

    std::cout << "Registry" << std::endl;
    Check(UltraCanvasHostedApps::Has("ultrafiler"), "a registered application is there");
    Check(!UltraCanvasHostedApps::Has("texter"), "an unregistered one is not");
    Check(UltraCanvasHostedApps::List().size() == 2, "both are listed");
    Check(UltraCanvasHostedApps::PathFor("ultrafiler") == "hosted:ultrafiler", "its shortcut path");
    Check(UltraCanvasHostedApps::IdFromPath("hosted:ultrafiler") == "ultrafiler", "the id from that path");
    Check(UltraCanvasHostedApps::IdFromPath("/usr/share/applications/x.desktop").empty(),
          "no id from another path");

    std::cout << "Program names" << std::endl;
    Check(UltraCanvasHostedApps::IdForProgram("UltraFiler") == "ultrafiler", "by its name");
    Check(UltraCanvasHostedApps::IdForProgram("ultrafiler") == "ultrafiler", "in any letter case");
    Check(UltraCanvasHostedApps::IdForProgram("/opt/ultraos/bin/UltraFiler") == "ultrafiler", "by a path");
    Check(UltraCanvasHostedApps::IdForProgram("C:\\UltraOS\\UltraFiler.exe") == "ultrafiler",
          "by a Windows path, .exe aside");
    Check(UltraCanvasHostedApps::IdForProgram("UltraFilerHelper").empty(), "not by a longer name");
    Check(UltraCanvasHostedApps::IdForProgram("").empty(), "not by nothing");

    std::cout << "LaunchProgram" << std::endl;
    std::string error;
    Check(UltraCanvasDesktopShell::LaunchProgram("UltraFiler", {"--ram-disc", "/sdcard/Download"}, &error),
          "starts the hosted application");
    Check(filerStarts == 1, "once");
    Check(filerArguments == std::vector<std::string>{"--ram-disc", "/sdcard/Download"},
          "with the program's arguments");
    Check(launchingInside == "ultrafiler", "its windows belong to it while it starts");
    Check(UltraCanvasHostedApps::Launching().empty(), "and to nothing afterwards");

    error.clear();
    Check(!UltraCanvasDesktopShell::LaunchProgram("Broken", {}, &error), "a start that fails is false");
    Check(error.find("Broken") != std::string::npos, "with the application's name in the reason");

    error.clear();
    Check(!UltraCanvasDesktopShell::LaunchProgram("no-such-program-ultracanvas-test", {}, &error),
          "any other name still looks for an executable");
    Check(error.find("not installed") != std::string::npos, "and says it is not installed");
    Check(filerStarts == 1, "without starting the hosted application");

    if (g_failures) {
        std::cout << g_failures << " check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "All checks passed" << std::endl;
    return 0;
}
