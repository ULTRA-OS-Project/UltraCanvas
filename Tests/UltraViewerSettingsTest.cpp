// Tests/UltraViewerSettingsTest.cpp
// UltraViewer's settings file (Apps/UltraViewer/UltraViewerSettings.h): how
// long the viewer may be left alone before it forgets the passwords Unlock
// was given. The default is the media viewer's own ten minutes, zero ("never")
// survives a save, and a hand-edited value is brought into the range or, when
// it is not a number, left at what was there. Works in a temporary folder.
// Version: 1.0.0
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework

#include "UltraViewerSettings.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

int Read(const fs::path& path) {
    UltraViewerSettings settings;
    settings.LoadFrom(path);
    return settings.forgetPasswordsAfterMinutes;
}

void Write(const fs::path& path, const std::string& value) {
    std::ofstream file(path, std::ios::binary);
    file << "# by hand\npasswords.forget.minutes = " << value << "\n";
}

} // namespace

int main() {
    std::cout << "UltraViewerSettingsTest\n";
    const fs::path dir = fs::temp_directory_path() / PathFromUtf8(
        "uc-viewer-settings-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path file = dir / "UltraViewer" / "config.ini";

    {
        UltraViewerSettings fresh;
        Check(fresh.forgetPasswordsAfterMinutes * 60 ==
                  UltraCanvasMediaViewer::kDefaultForgetPasswordsAfterIdle.count(),
              "the default is the media viewer's own idle time");
        Check(!fresh.LoadFrom(file) && fresh.forgetPasswordsAfterMinutes ==
                  UltraViewerSettings::kDefaultForgetPasswordsMinutes,
              "no file yet: the default stands");
    }
    {
        UltraViewerSettings settings;
        settings.forgetPasswordsAfterMinutes = 0;
        Check(settings.SaveTo(file), "saved, its folder made on the way");
        Check(Read(file) == 0, "never comes back as never, not as the default");
        settings.forgetPasswordsAfterMinutes = 30;
        settings.SaveTo(file);
        Check(Read(file) == 30, "thirty minutes come back");
    }
    Write(file, "5000");
    Check(Read(file) == UltraViewerSettings::kMaxForgetPasswordsMinutes, "past the longest choice: held at it");
    Write(file, "-3");
    Check(Read(file) == 0, "below zero: never");
    Write(file, "soon");
    Check(Read(file) == UltraViewerSettings::kDefaultForgetPasswordsMinutes, "not a number: the default stays");

    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (g_failures == 0 ? "All checks passed\n" : "Some checks FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
