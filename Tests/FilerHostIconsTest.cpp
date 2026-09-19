// Tests/FilerHostIconsTest.cpp
// Display > File icons: the setting itself (UltraCanvasFilerWidget) and the
// cache key the host icon service answers by (UltraCanvasHostFileIcons).
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
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasHostFileIcons.h"

#include <iostream>
#include <string>

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

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All host file-icon checks passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
