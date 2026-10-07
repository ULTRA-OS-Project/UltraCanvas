// Tests/FilerHostIconsTest.cpp
// Display > File icons: the setting itself (UltraCanvasFilerWidget), the
// cache key the host icon service answers by (UltraCanvasHostFileIcons), and
// UltraFiler's saved choice (Apps/UltraFiler/UltraFilerSettings.h).
//
// The rule this guards: a host icon is the icon of a TYPE, and the key says
// which type. Key two file kinds the same and a folder draws one of them with
// the other's icon; key them too finely and a folder of four thousand ".txt"
// files resolves four thousand icons instead of one - which is the whole cost
// of the feature, paid on the UI's behalf by a worker thread. Both failures
// are invisible in a screenshot of five files, so they are checked here.
//
// The lookups themselves are the host's, so what a system answers cannot be
// asserted: a build machine has no icon theme installed, and a desktop that
// has one is free to change it. What IS asserted is that asking is harmless -
// an unresolvable type returns null rather than failing - so the display
// always has something to draw.
//
// UltraFiler draws the host's icons by default. Its config file is read from
// a temporary folder here, so the test never touches a real installation: a
// first start, both choices across a restart, and the files earlier releases
// left behind - every one of which says "simple" under the old key, chosen or
// not, and must take the new default rather than keep the old one forever.
// Version: 1.1.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasHostFileIcons.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraFilerSettings.h"        // Apps/UltraFiler

#include <cstdlib>   // setenv
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
// Declared rather than taken from <windows.h>, for the reason
// UltraCanvasPathUtf8.h gives for GetEnvironmentVariableW: windows.h would
// rename our own functions. This is the declaration windows.h makes (BOOL is
// int, LPCWSTR const wchar_t*).
extern "C" __declspec(dllimport) int __stdcall SetEnvironmentVariableW(
        const wchar_t* lpName, const wchar_t* lpValue);
#endif

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

std::string FileKey(const std::string& path) {
    return HostFileIconKey(path, /*isDirectory=*/false);
}

// Points UltraFilerSettings::GetConfigDirectory() at `root` - through the
// wide API on Windows, which is where GetEnvUtf8 reads APPDATA.
void RedirectConfigDirectory(const std::filesystem::path& root) {
#if defined(_WIN32) || defined(_WIN64)
    SetEnvironmentVariableW(L"APPDATA", root.c_str());
#elif defined(__APPLE__)
    setenv("HOME", PathToUtf8(root).c_str(), 1);
#else
    setenv("XDG_CONFIG_HOME", PathToUtf8(root).c_str(), 1);
#endif
}

// A config file holding exactly `lines`, the way an earlier release (or a
// hand edit) left it.
void WriteConfig(const std::string& lines) {
    std::error_code ec;
    std::filesystem::create_directories(
            PathFromUtf8(UltraFilerSettings::GetConfigDirectory()), ec);
    std::ofstream file(PathFromUtf8(UltraFilerSettings::GetConfigPath()),
                       std::ios::trunc);
    file << "# UltraFiler Configuration\n\n" << lines;
}

// What the next start of UltraFiler draws with.
FilerFileIconStyle LoadedStyle() {
    UltraFilerSettings settings;
    settings.Load();
    return settings.fileIconStyle;
}

void TestUltraFilerSavedChoice() {
    std::cout << "\n-- UltraFiler's saved choice --\n";
    std::error_code ec;
    const std::filesystem::path root =
            std::filesystem::temp_directory_path(ec) / "ultrafiler-file-icons-test";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    if (ec) {
        Check(false, "a temporary config directory can be made");
        return;
    }
    RedirectConfigDirectory(root);

    Check(UltraFilerSettings().fileIconStyle ==
                  FilerFileIconStyle::HostOperatingSystem,
          "UltraFiler draws the host's icons by default");
    Check(LoadedStyle() == FilerFileIconStyle::HostOperatingSystem,
          "a first start, with no config file, draws the host's icons");

    for (FilerFileIconStyle style : UltraCanvasFilerWidget::AllFileIconStyles()) {
        UltraFilerSettings settings;
        settings.fileIconStyle = style;
        Check(settings.Save(), "the choice is saved with the other settings");
        Check(LoadedStyle() == style,
              std::string("\"") + UltraCanvasFilerWidget::FileIconStyleLabel(style) +
                      "\" comes back after a restart");
    }

    // Every save of 1.66.1 and earlier wrote "simple" under the old key,
    // chosen or not, so such a file is the old default, not a choice.
    WriteConfig("display.file.icons = simple\n");
    Check(LoadedStyle() == FilerFileIconStyle::HostOperatingSystem,
          "a config from before the change takes the host's icons");
    WriteConfig("display.file.icons = host\n");
    Check(LoadedStyle() == FilerFileIconStyle::HostOperatingSystem,
          "one that had chosen the host's icons keeps them");
    // A choice made from this release on has its own key, which the old one
    // cannot override.
    WriteConfig("display.file.icons = host\ndisplay.file.icons.style = simple\n");
    Check(LoadedStyle() == FilerFileIconStyle::Simple,
          "the simple icons, chosen now, stay chosen");
    // A style a later release might add reads back as what every build draws.
    WriteConfig("display.file.icons.style = some-later-style\n");
    Check(LoadedStyle() == FilerFileIconStyle::Simple,
          "an unknown style reads back as the simple icons");

    std::filesystem::remove_all(root, ec);
}

} // namespace

int main() {
    std::cout << "=== Filer host file icons ===\n\n";

    std::cout << "-- the type key --\n";
    // Every folder is one icon. This is what keeps a tree of ten thousand
    // folders at a single lookup.
    Check(HostFileIconKey("/home/user/Documents", true) ==
                  HostFileIconKey("/etc", true),
          "two folders share one key");
    Check(HostFileIconKey("/home/user/notes.txt", true) !=
                  FileKey("/home/user/notes.txt"),
          "the same path as a folder and as a file key differently");

    // Same type, one key - whatever the file is called or where it lives.
    Check(FileKey("/a/notes.txt") == FileKey("/b/other.txt"),
          "two .txt files share one key");
    Check(FileKey("/a/NOTES.TXT") == FileKey("/a/notes.txt"),
          "the key ignores case");
    Check(FileKey("/a/notes.txt") != FileKey("/a/photo.png"),
          "different extensions key differently");

    // A compound suffix is a type of its own: a tarball is not a gzip file,
    // and the desktops draw them differently.
    Check(FileKey("/a/source.tar.gz") != FileKey("/a/blob.gz"),
          "\"tar.gz\" and \"gz\" key differently");
    Check(FileKey("/a/source.tar.gz") == FileKey("/b/backup.tar.gz"),
          "two .tar.gz files share one key");

    // Extension-less names are matched by the name itself (the freedesktop
    // database knows "makefile"), so that is the key.
    Check(FileKey("/a/Makefile") == FileKey("/b/makefile"),
          "an extension-less name keys by its name, case-folded");
    Check(FileKey("/a/Makefile") != FileKey("/a/README"),
          "two different extension-less names key differently");
    // A dot file is a name, not a suffix: ".bashrc" is not "bashrc".
    Check(FileKey("/a/.bashrc") != FileKey("/a/config.bashrc"),
          "a leading dot is part of the name, not a suffix");

    // A file that carries its own icon is not a type at all - two programs
    // must never share a key, or a folder of them would draw one icon.
    Check(FileKey("/a/first.exe") != FileKey("/a/second.exe"),
          "programs key per file, not per extension");
    Check(FileKey("/a/link.lnk") != FileKey("/a/other.lnk"),
          "shortcuts key per file, not per extension");

    std::cout << "\n-- asking the host --\n";
    // Available or not, a lookup answers rather than throws, and an answer of
    // "nothing" is legitimate: the caller then draws its own icon.
    std::cout << "  (host icons "
              << (HostFileIconsAvailable() ? "available" : "not available")
              << " on this system)\n";
    auto icon = LoadHostFileIconPixmap("/a/definitely-not-a-type.zzqq", false, 48);
    Check(icon == nullptr || icon->IsValid(),
          "an unknown type answers with nothing, or with a usable pixmap");
    auto folderIcon = LoadHostFileIconPixmap("/", true, 48);
    Check(folderIcon == nullptr || folderIcon->IsValid(),
          "the folder icon is either absent or usable");
    // Asking again must not disturb anything - the caller asks once per type
    // per size, but a refresh makes it ask again.
    RefreshHostFileIcons();
    auto again = LoadHostFileIconPixmap("/", true, 48);
    Check(again == nullptr || again->IsValid(),
          "the same question after a refresh answers the same way");

    std::cout << "\n-- the setting --\n";
    UltraCanvasFilerWidget filer("host-icons-test", 0, 0, 800, 600);
    Check(filer.GetFileIconStyle() == FilerFileIconStyle::Simple,
          "the display ships with its own icons");
    Check(UltraCanvasFilerWidget::AllFileIconStyles().size() == 2,
          "two icon styles are offered");
    for (FilerFileIconStyle style : UltraCanvasFilerWidget::AllFileIconStyles()) {
        filer.SetFileIconStyle(style);
        Check(filer.GetFileIconStyle() == style,
              std::string("icon style \"") +
                      UltraCanvasFilerWidget::FileIconStyleLabel(style) +
                      "\" round-trips");
    }
    // The two labels are what a menu and a settings page show, so they have
    // to differ and to be worth reading.
    Check(std::string(UltraCanvasFilerWidget::FileIconStyleLabel(
                  FilerFileIconStyle::Simple)) !=
                  UltraCanvasFilerWidget::FileIconStyleLabel(
                          FilerFileIconStyle::HostOperatingSystem),
          "the two styles are labelled differently");
    Check(UltraCanvasFilerWidget::AreHostFileIconsAvailable() ==
                  HostFileIconsAvailable(),
          "the widget reports what the icon service reports");
    // Switching styles drops the resolved icons; with host icons selected the
    // display must survive a refresh with no window and no theme.
    filer.SetFileIconStyle(FilerFileIconStyle::HostOperatingSystem);
    filer.RefreshHostIcons();
    Check(filer.GetFileIconStyle() == FilerFileIconStyle::HostOperatingSystem,
          "a refresh keeps the chosen style");
    filer.SetFileIconStyle(FilerFileIconStyle::Simple);

    TestUltraFilerSavedChoice();

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All host file-icon checks passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
