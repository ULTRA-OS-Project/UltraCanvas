// Tests/LinuxHostAppearanceTest.cpp
// LinuxHostAppearance::Detect against a made-up config home, without a
// display or a desktop session: no session and no files gives the default
// (light, no accent, the bundled fonts - what a CI runner under Xvfb must
// see); settings.ini's prefer-dark and theme name are read; kdeglobals'
// scheme, window background and accent are read; a KDE session reads
// kdeglobals first; an XSETTINGS property handed in by the fetcher is
// decoded and names the source; the portal is not asked without a session.
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework
#include "UltraCanvasLinuxHostAppearance.h"
#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace UltraCanvas;

static int testCount = 0, failCount = 0;
static void Check(bool ok, const char* what) {
    ++testCount;
    if (!ok) { ++failCount; std::printf("FAIL: %s\n", what); }
}
static void WriteFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << text;
}
static bool Same(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

int main() {
    const std::filesystem::path home =
        std::filesystem::temp_directory_path() / ("uc-linux-host-appearance-test-" + std::to_string(getpid()));
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home);
    setenv("XDG_CONFIG_HOME", PathToUtf8(home).c_str(), 1);
    unsetenv("XDG_CURRENT_DESKTOP");
    unsetenv("DESKTOP_SESSION");

    // No desktop session, no files, no XSETTINGS: the default, from "linux:default".
    HostAppearance host = LinuxHostAppearance::Detect(nullptr);
    Check(!host.dark && !host.highContrast && !host.reduceMotion && !host.accent, "no session, no files: light, no accent");
    Check(host.source == "linux:default", "no session, no files: the source is linux:default");
    Check(host.uiFont.fontFamily == "Ubuntu" && host.monoFont.fontFamily == "Ubuntu Mono", "no session, no files: the bundled fonts");
    Check(!LinuxHostAppearance::ReadPortalColorScheme().has_value(), "the portal is not asked without a desktop session");

    // A fetcher that answers nothing is the same as none.
    host = LinuxHostAppearance::Detect([](std::vector<uint8_t>&) { return false; });
    Check(!host.dark && host.source == "linux:default", "a fetcher without a manager changes nothing");

    // settings.ini is read without a session.
    WriteFile(home / "gtk-3.0" / "settings.ini", "[Settings]\ngtk-application-prefer-dark-theme=1\ngtk-font-name=Noto Sans 10\n");
    host = LinuxHostAppearance::Detect(nullptr);
    Check(host.dark && host.source == "linux:gtk-settings-ini", "gtk-3.0/settings.ini prefer-dark");
    Check(host.uiFont.fontFamily == "Noto Sans", "the font detector's answer rides along");
    WriteFile(home / "gtk-4.0" / "settings.ini", "[Settings]\ngtk-theme-name=Adwaita\n");
    host = LinuxHostAppearance::Detect(nullptr);
    Check(!host.dark && host.source == "linux:gtk-settings-ini", "gtk-4.0/settings.ini first: a light theme name");

    // kdeglobals is read without a session, after the GTK file in a GNOME-less session.
    WriteFile(home / "kdeglobals",
              "[General]\nColorScheme=BreezeDark\nAccentColor=61,174,233\n[Colors:Window]\nBackgroundNormal=35,38,41\n");
    host = LinuxHostAppearance::Detect(nullptr);
    Check(!host.dark && host.accent && Same(*host.accent, Color(61, 174, 233, 255)),
          "no desktop named: the GTK file answers the look, kdeglobals the accent");

    // A KDE session reads kdeglobals first.
    setenv("XDG_CURRENT_DESKTOP", "KDE", 1);
    host = LinuxHostAppearance::Detect(nullptr);
    Check(host.dark && host.source == "linux:kdeglobals", "KDE session: kdeglobals first");
    Check(host.accent && Same(*host.accent, Color(61, 174, 233, 255)), "KDE session: the accent from kdeglobals");
    unsetenv("XDG_CURRENT_DESKTOP");

    // An XSETTINGS property from the fetcher: Net/ThemeName decides, before
    // the files, when GSettings has no session to answer in.
    std::filesystem::remove(home / "gtk-4.0" / "settings.ini");
    std::filesystem::remove(home / "gtk-3.0" / "settings.ini");
    std::vector<uint8_t> property = {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
                                     1, 0, 13, 0, 'N', 'e', 't', '/', 'T', 'h', 'e', 'm', 'e', 'N', 'a', 'm', 'e', 0, 0, 0,
                                     1, 0, 0, 0, 9, 0, 0, 0, 'Y', 'a', 'r', 'u', '-', 'd', 'a', 'r', 'k', 0, 0, 0};
    host = LinuxHostAppearance::Detect([&](std::vector<uint8_t>& bytes) { bytes = property; return true; });
    Check(host.dark && host.source == "linux:xsettings", "an XSETTINGS property with a dark theme name");
    Check(host.accent && Same(*host.accent, Color(61, 174, 233, 255)), "the accent still comes from kdeglobals");

    std::filesystem::remove_all(home);
    std::printf("%d/%d passed\n", testCount - failCount, testCount);
    return failCount ? 1 : 0;
}
