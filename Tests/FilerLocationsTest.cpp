// Tests/FilerLocationsTest.cpp
// UltraFiler's Extras > Open location (Apps/UltraFiler/UltraFilerLocations.h):
// the folders each platform lists - the crash dumps where the crash reporter
// writes them, the system's places where the system keeps them - and a typed
// location: %VAR% on Windows, $VAR / ${VAR} elsewhere, a leading ~, quotes,
// a relative path, the recent list. A menu entry that opens the wrong folder,
// or a typed %LOCALAPPDATA% that is not expanded, is the feature not working.
// Version: 1.0.0
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework
#include "UltraFilerLocations.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int failures = 0;
int checks = 0;

void Check(bool ok, const std::string& what) {
    ++checks;
    std::printf("%s  %s\n", ok ? "  ok  " : " FAIL ", what.c_str());
    if (!ok) ++failures;
}

void CheckEq(const std::string& got, const std::string& want, const std::string& what) {
    ++checks;
    const bool ok = got == want;
    std::printf("%s  %s", ok ? "  ok  " : " FAIL ", what.c_str());
    if (!ok) std::printf("  (got \"%s\", want \"%s\")", got.c_str(), want.c_str());
    std::printf("\n");
    if (!ok) ++failures;
}

// Compared as paths, so the separator the platform joins with does not matter.
void CheckPath(const std::string& got, const fs::path& want, const std::string& what) {
    ++checks;
    const bool ok = PathFromUtf8(got).lexically_normal() == want.lexically_normal();
    std::printf("%s  %s", ok ? "  ok  " : " FAIL ", what.c_str());
    if (!ok) std::printf("  (got \"%s\", want \"%s\")", got.c_str(), PathToUtf8(want).c_str());
    std::printf("\n");
    if (!ok) ++failures;
}

FilerEnvLookup Env(std::map<std::string, std::string> values) {
    return [values](const std::string& name) {
        const auto it = values.find(name);
        return it == values.end() ? std::string() : it->second;
    };
}

// The path of the entry labelled `label` in `group`; empty when it is not listed.
std::string Find(const std::vector<FilerLocation>& list, FilerLocationGroup group,
                 const std::string& label) {
    for (const FilerLocation& l : list)
        if (l.group == group && l.label == label) return l.path;
    return {};
}

const FilerLocationGroup kOwn = FilerLocationGroup::UltraCanvas;
const FilerLocationGroup kSystem = FilerLocationGroup::System;

void TestWindowsList() {
    std::printf("Windows list\n");
    FilerLocationEnvironment in;
    in.platform = DataFolderPlatform::Windows;
    in.env = Env({{"USERPROFILE", "C:/Users/erika"},
                  {"APPDATA", "C:/Users/erika/AppData/Roaming"},
                  {"LOCALAPPDATA", "C:/Users/erika/AppData/Local"},
                  {"ProgramData", "C:/ProgramData"},
                  {"ProgramFiles", "C:/Program Files"},
                  {"ProgramFiles(x86)", "C:/Program Files (x86)"},
                  {"SystemRoot", "C:/Windows"}});
    in.cacheRoot = "C:/Users/erika/AppData/Local/UltraCanvas";
    in.tempDir = "C:/Users/erika/AppData/Local/Temp";
    in.filerSettingsDir = "C:/Users/erika/AppData/Roaming/UltraCanvas/UltraFiler";
    const std::vector<FilerLocation> list = FilerOsLocationsFor(in);

    CheckPath(Find(list, kOwn, "Crash dumps"),
              "C:/Users/erika/AppData/Local/UltraCanvas/CrashDumps", "crash dumps");
    CheckPath(Find(list, kOwn, "Settings"), "C:/Users/erika/AppData/Roaming/UltraCanvas", "settings");
    CheckPath(Find(list, kOwn, "UltraFiler settings"),
              "C:/Users/erika/AppData/Roaming/UltraCanvas/UltraFiler", "UltraFiler settings");
    CheckPath(Find(list, kOwn, "Cache"), "C:/Users/erika/AppData/Local/UltraCanvas", "cache");
    CheckPath(Find(list, kSystem, "Application data (local)"),
              "C:/Users/erika/AppData/Local", "local application data");
    CheckPath(Find(list, kSystem, "Startup"),
              "C:/Users/erika/AppData/Roaming/Microsoft/Windows/Start Menu/Programs/Startup",
              "startup");
    CheckPath(Find(list, kSystem, "Crash dumps (Windows)"),
              "C:/Users/erika/AppData/Local/CrashDumps", "Windows Error Reporting's dumps");
    CheckPath(Find(list, kSystem, "Network configuration (hosts)"),
              "C:/Windows/System32/drivers/etc", "hosts");
    CheckPath(Find(list, kSystem, "Temporary files"), "C:/Users/erika/AppData/Local/Temp", "temp");
    Check(Find(list, kSystem, "Caches").empty(), "no macOS / Linux entry");

    // The UltraCanvas group comes first, as one block.
    bool ordered = !list.empty() && list.front().group == kOwn;
    bool seenSystem = false;
    for (const FilerLocation& l : list) {
        if (l.group == kSystem) seenSystem = true;
        else if (seenSystem) ordered = false;
    }
    Check(ordered, "UltraCanvas block, then the system's");

    // The crash reporter's own variable moves the entry with it.
    in.env = Env({{"LOCALAPPDATA", "C:/Users/erika/AppData/Local"},
                  {"ULTRACANVAS_CRASH_DUMP_DIR", "D:/dumps"}});
    CheckPath(Find(FilerOsLocationsFor(in), kOwn, "Crash dumps"), "D:/dumps",
              "ULTRACANVAS_CRASH_DUMP_DIR");

    // An unset variable leaves its entries out instead of listing a relative path.
    in.env = Env({});
    in.cacheRoot.clear();
    in.tempDir.clear();
    in.filerSettingsDir.clear();
    Check(FilerOsLocationsFor(in).empty(), "an empty environment lists nothing");
}

void TestMacList() {
    std::printf("macOS list\n");
    FilerLocationEnvironment in;
    in.platform = DataFolderPlatform::MacOS;
    in.env = Env({{"HOME", "/Users/erika"}});
    const std::vector<FilerLocation> list = FilerOsLocationsFor(in);
    Check(Find(list, kOwn, "Crash dumps").empty(), "no UltraCanvas crash dumps on macOS");
    CheckPath(Find(list, kOwn, "Settings"), "/Users/erika/Library/Application Support/UltraCanvas",
              "settings");
    CheckPath(Find(list, kSystem, "Crash reports"), "/Users/erika/Library/Logs/DiagnosticReports",
              "DiagnosticReports");
    CheckPath(Find(list, kSystem, "Startup (launch agents)"), "/Users/erika/Library/LaunchAgents",
              "launch agents");
    CheckPath(Find(list, kSystem, "Programs"), "/Applications", "programs");
}

void TestLinuxList() {
    std::printf("Linux list\n");
    FilerLocationEnvironment in;
    in.platform = DataFolderPlatform::Unix;
    in.env = Env({{"HOME", "/home/erika"}});
    in.tempDir = "/tmp";
    std::vector<FilerLocation> list = FilerOsLocationsFor(in);
    CheckPath(Find(list, kOwn, "Settings"), "/home/erika/.config/UltraCanvas", "settings");
    CheckPath(Find(list, kSystem, "Application data"), "/home/erika/.local/share", "data");
    CheckPath(Find(list, kSystem, "Startup (autostart)"), "/home/erika/.config/autostart",
              "autostart");
    CheckPath(Find(list, kSystem, "User fonts"), "/home/erika/.local/share/fonts", "user fonts");
    CheckPath(Find(list, kSystem, "Core dumps (systemd)"), "/var/lib/systemd/coredump",
              "systemd core dumps");

    // The XDG variables win over the defaults under the home folder.
    in.env = Env({{"HOME", "/home/erika"},
                  {"XDG_CONFIG_HOME", "/srv/config"},
                  {"XDG_DATA_HOME", "/srv/data"}});
    list = FilerOsLocationsFor(in);
    CheckPath(Find(list, kOwn, "Settings"), "/srv/config/UltraCanvas", "XDG_CONFIG_HOME: settings");
    CheckPath(Find(list, kSystem, "Startup (autostart)"), "/srv/config/autostart",
              "XDG_CONFIG_HOME: autostart");
    CheckPath(Find(list, kSystem, "User fonts"), "/srv/data/fonts", "XDG_DATA_HOME: fonts");
}

void TestExpand() {
    std::printf("expand\n");
    const FilerEnvLookup windowsEnv = Env({{"LOCALAPPDATA", "C:\\Users\\erika\\AppData\\Local"},
                                           {"USERPROFILE", "C:\\Users\\erika"},
                                           {"ProgramFiles(x86)", "C:\\Program Files (x86)"}});
    const auto win = [&](const std::string& text) {
        return ExpandFilerLocationText(text, DataFolderPlatform::Windows, windowsEnv);
    };
    CheckEq(win("%LOCALAPPDATA%\\UltraCanvas\\CrashDumps"),
            "C:\\Users\\erika\\AppData\\Local\\UltraCanvas\\CrashDumps", "the example");
    CheckEq(win("  \"%LOCALAPPDATA%\\Temp\"  "), "C:\\Users\\erika\\AppData\\Local\\Temp",
            "blanks and the quotes of Copy as path");
    CheckEq(win("%ProgramFiles(x86)%\\App"), "C:\\Program Files (x86)\\App",
            "a name with parentheses");
    CheckEq(win("%NOPE%\\x"), "%NOPE%\\x", "an unset variable stays as typed");
    CheckEq(win("%NOPE%LOCALAPPDATA%"), "%NOPEC:\\Users\\erika\\AppData\\Local",
            "the '%' that did not close a name opens the next");
    CheckEq(win("100%"), "100%", "a lone '%'");
    CheckEq(win("\\\\server\\C$\\logs"), "\\\\server\\C$\\logs", "'$' is text on Windows");
    CheckEq(win("~\\Downloads"), "C:\\Users\\erika\\Downloads", "~ is USERPROFILE on Windows");

    const FilerEnvLookup unixEnv = Env({{"HOME", "/home/erika"},
                                        {"XDG_CONFIG_HOME", "/home/erika/.config"}});
    const auto nix = [&](const std::string& text) {
        return ExpandFilerLocationText(text, DataFolderPlatform::Unix, unixEnv);
    };
    CheckEq(nix("$XDG_CONFIG_HOME/autostart"), "/home/erika/.config/autostart", "$NAME");
    CheckEq(nix("${HOME}/.local"), "/home/erika/.local", "${NAME}");
    CheckEq(nix("~"), "/home/erika", "~ alone");
    CheckEq(nix("~/Downloads"), "/home/erika/Downloads", "~/...");
    CheckEq(nix("~erika/x"), "~erika/x", "~user is not expanded");
    CheckEq(nix("/a/~/b"), "/a/~/b", "a ~ inside the path is a name");
    CheckEq(nix("$NOPE/x"), "$NOPE/x", "an unset $NAME stays as typed");
    CheckEq(nix("${NOPE}/x"), "${NOPE}/x", "an unset ${NAME} stays as typed");
    CheckEq(nix("/price$"), "/price$", "a trailing '$'");
    CheckEq(nix("/100%/%HOME%"), "/100%/%HOME%", "'%' is text outside Windows");
    CheckEq(nix("'/tmp/a b'"), "/tmp/a b", "single quotes");
    CheckEq(nix("   "), "", "blank");
}

void TestResolve() {
    std::printf("resolve\n");
    CheckPath(ResolveFilerLocation("/var/log/", "/home/erika"), "/var/log", "trailing separator dropped");
    CheckEq(ResolveFilerLocation("/var/log/", "/home/erika"), PathToUtf8(fs::path("/var/log")),
            "no trailing separator at all");
    CheckPath(ResolveFilerLocation("../x/./y", "/home/erika/docs"), "/home/erika/x/y",
              "relative, from the folder on screen");
    CheckEq(ResolveFilerLocation("x", ""), "", "relative without a folder");
    CheckEq(ResolveFilerLocation("/", "/home"), "/", "the root keeps its separator");
    CheckEq(ResolveFilerLocation("", "/home"), "", "empty");
}

void TestRecent() {
    std::printf("recent\n");
    std::vector<std::string> recent;
    RememberFilerLocation(recent, "a", 3);
    RememberFilerLocation(recent, "b", 3);
    RememberFilerLocation(recent, "c", 3);
    Check(recent == std::vector<std::string>{"c", "b", "a"}, "newest first");
    RememberFilerLocation(recent, "a", 3);
    Check(recent == std::vector<std::string>{"a", "c", "b"}, "an old one moves to the front, once");
    RememberFilerLocation(recent, "d", 3);
    Check(recent == std::vector<std::string>{"d", "a", "c"}, "the oldest falls off");
    RememberFilerLocation(recent, "  ", 3);
    Check(recent.size() == 3 && recent.front() == "d", "a blank entry changes nothing");
}

}  // namespace

int main() {
    TestWindowsList();
    TestMacList();
    TestLinuxList();
    TestExpand();
    TestResolve();
    TestRecent();
    std::printf("FilerLocationsTest: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
