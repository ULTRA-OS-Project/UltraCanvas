// Tests/LinuxSystemFontTest.cpp
// LinuxFonts::DetectSystemFontStyle / DetectMonospacedFontStyle against a
// made-up config home, without a display: no desktop session and no files
// gives the bundled Ubuntu 12 (what a CI runner under Xvfb must see, not the
// GNOME schema's compiled default), settings.ini and kdeglobals are read,
// and a KDE session reads kdeglobals first.
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework
#include "UltraCanvasLinuxFonts.h"
#include "UltraCanvasPathUtf8.h"

#include <cmath>
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

static bool Is(const FontStyle& style, const char* family, double points) {
    return style.fontFamily == family && std::fabs(style.fontSize - points) < 1e-9;
}

static void WriteFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

int main() {
    const std::filesystem::path home =
        std::filesystem::temp_directory_path() / ("uc-linux-font-test-" + std::to_string(getpid()));
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home);
    setenv("XDG_CONFIG_HOME", PathToUtf8(home).c_str(), 1);
    unsetenv("XDG_CURRENT_DESKTOP");
    unsetenv("DESKTOP_SESSION");

    // No desktop session, no files: the bundled fonts, whatever GSettings
    // schemas the machine has installed.
    Check(Is(LinuxFonts::DetectSystemFontStyle(), "Ubuntu", 12), "no desktop, no files: Ubuntu 12");
    Check(Is(LinuxFonts::DetectMonospacedFontStyle(), "Ubuntu Mono", 12), "no desktop, no files: Ubuntu Mono 12");

    // The GTK settings file is read without a desktop; it has no monospace key.
    WriteFile(home / "gtk-3.0" / "settings.ini", "[Settings]\ngtk-font-name=Noto Sans 10\n");
    Check(Is(LinuxFonts::DetectSystemFontStyle(), "Noto Sans", 10), "gtk-3.0/settings.ini gtk-font-name");
    Check(Is(LinuxFonts::DetectMonospacedFontStyle(), "Ubuntu Mono", 12), "settings.ini has no monospace key");

    // gtk-4.0 wins over gtk-3.0.
    WriteFile(home / "gtk-4.0" / "settings.ini", "[Settings]\ngtk-font-name=Inter 11\n");
    Check(Is(LinuxFonts::DetectSystemFontStyle(), "Inter", 11), "gtk-4.0/settings.ini first");

    // kdeglobals is read without a desktop too, before the GTK files.
    WriteFile(home / "kdeglobals",
              "[General]\nfont=DejaVu Sans,9,-1,5,50,0,0,0,0,0\nfixed=Hack,10,-1,5,50,0,0,0,0,0\n");
    Check(Is(LinuxFonts::DetectSystemFontStyle(), "DejaVu Sans", 9), "kdeglobals font before settings.ini");
    Check(Is(LinuxFonts::DetectMonospacedFontStyle(), "Hack", 10), "kdeglobals fixed");

    // A KDE session reads kdeglobals first whatever GSettings says.
    setenv("XDG_CURRENT_DESKTOP", "KDE", 1);
    Check(Is(LinuxFonts::DetectSystemFontStyle(), "DejaVu Sans", 9), "KDE session: kdeglobals first");
    Check(Is(LinuxFonts::DetectMonospacedFontStyle(), "Hack", 10), "KDE session: kdeglobals fixed first");

    // Any other desktop session asks GSettings first; whether the schema is
    // installed here is the machine's business, so only the shape is checked.
    setenv("XDG_CURRENT_DESKTOP", "GNOME", 1);
    const FontStyle gnome = LinuxFonts::DetectSystemFontStyle();
    Check(!gnome.fontFamily.empty() && gnome.fontSize > 0, "GNOME session answers a font");

    std::filesystem::remove_all(home);
    std::printf("%d/%d passed\n", testCount - failCount, testCount);
    return failCount == 0 ? 0 : 1;
}
