// Tests/FilerExtensionDisplayTest.cpp
// Display > File extensions: the name the file display draws for an entry
// (UltraCanvasFilerWidget::DisplayNameOf) and the tag its thumbnail tiles
// carry (UltraCanvasFilerWidget::ExtensionTagOf).
//
// The rule this guards: hiding the extension is a display decision and must
// never invent a file type. A tail that is not a plausible extension - a
// version number, an architecture tag, the whole name of a dot file, a folder
// with a dot in it - is part of the name, so it is neither cut off the drawn
// name nor shown as a type tag. Getting that wrong renames nothing on disk but
// tells the user that "UCDemo-Windows-0.3.27-x86_64" is a file of type
// "27-x86_64".
// Version: 1.0.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

void CheckEqual(const std::string& got, const std::string& expected,
                const std::string& what) {
    Check(got == expected, what + " -> \"" + got + "\" (expected \"" +
                           expected + "\")");
}

// An entry as the folder scan produces it: the extension is the lowercased
// tail after the last dot, and a directory never has one.
FilerEntry MakeEntry(const std::string& name, bool isDirectory = false) {
    FilerEntry e;
    e.name = name;
    e.path = "/tmp/" + name;
    e.isDirectory = isDirectory;
    const size_t dot = name.find_last_of('.');
    if (!isDirectory && dot != std::string::npos && dot != 0 &&
        dot + 1 < name.size()) {
        e.extension = name.substr(dot + 1);
        std::transform(e.extension.begin(), e.extension.end(),
                       e.extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    }
    return e;
}

} // namespace

int main() {
    std::cout << "===== Filer file-extension display =====\n";

    UltraCanvasFilerWidget filer("test-filer", 0, 0, 400, 300);

    std::cout << "\n-- Names keep their extension (the default) --\n";
    Check(filer.AreFileExtensionsInNames(),
          "extensions are shown in names by default");
    Check(filer.GetExtensionBadge() == FilerExtensionBadge::NoneBadge,
          "thumbnail tiles carry no extension tag by default");
    CheckEqual(filer.DisplayNameOf(MakeEntry("UltraFiler.exe")),
               "UltraFiler.exe", "UltraFiler.exe");
    CheckEqual(filer.DisplayNameOf(MakeEntry("sources.tar.gz")),
               "sources.tar.gz", "sources.tar.gz");

    std::cout << "\n-- Names without their extension --\n";
    filer.SetFileExtensionsInNames(false);
    Check(!filer.AreFileExtensionsInNames(), "the switch took");
    CheckEqual(filer.DisplayNameOf(MakeEntry("UltraFiler.exe")),
               "UltraFiler", "UltraFiler.exe");
    CheckEqual(filer.DisplayNameOf(MakeEntry("Holiday.JPG")),
               "Holiday", "Holiday.JPG (upper case extension)");
    // Only the last suffix goes: ".tar" is as much a part of the name as the
    // file manager can tell, and dropping both would hide that this is a
    // tarball rather than a plain gzip.
    CheckEqual(filer.DisplayNameOf(MakeEntry("sources.tar.gz")),
               "sources.tar", "sources.tar.gz");
    CheckEqual(filer.DisplayNameOf(MakeEntry("README")),
               "README", "README (no extension at all)");
    CheckEqual(filer.DisplayNameOf(MakeEntry(".bashrc")),
               ".bashrc", ".bashrc (a dot file is not an extension)");
    CheckEqual(filer.DisplayNameOf(MakeEntry("UCDemo-Windows-0.3.27-x86_64")),
               "UCDemo-Windows-0.3.27-x86_64",
               "UCDemo-Windows-0.3.27-x86_64 (a version, not a type)");
    CheckEqual(filer.DisplayNameOf(MakeEntry("archive.2024")),
               "archive.2024", "archive.2024 (a number is not a type)");
    CheckEqual(filer.DisplayNameOf(MakeEntry("Backup.old", true)),
               "Backup.old", "Backup.old (a folder keeps every dot)");

    std::cout << "\n-- The tile tag --\n";
    CheckEqual(UltraCanvasFilerWidget::ExtensionTagOf(MakeEntry("UltraFiler.exe")),
               "exe", "UltraFiler.exe");
    CheckEqual(UltraCanvasFilerWidget::ExtensionTagOf(MakeEntry("Holiday.JPG")),
               "jpg", "Holiday.JPG (tags are lowercase)");
    CheckEqual(UltraCanvasFilerWidget::ExtensionTagOf(MakeEntry("README")),
               "", "README");
    CheckEqual(UltraCanvasFilerWidget::ExtensionTagOf(MakeEntry(".bashrc")),
               "", ".bashrc");
    CheckEqual(UltraCanvasFilerWidget::ExtensionTagOf(
                       MakeEntry("UCDemo-Windows-0.3.27-x86_64")),
               "", "UCDemo-Windows-0.3.27-x86_64");
    CheckEqual(UltraCanvasFilerWidget::ExtensionTagOf(MakeEntry("Backup.old", true)),
               "", "Backup.old (a folder never carries a tag)");

    std::cout << "\n-- The badge modes --\n";
    Check(UltraCanvasFilerWidget::AllExtensionBadges().size() == 3,
          "three badge modes are offered");
    for (FilerExtensionBadge badge : UltraCanvasFilerWidget::AllExtensionBadges()) {
        filer.SetExtensionBadge(badge);
        Check(filer.GetExtensionBadge() == badge,
              std::string("badge mode \"") +
                      UltraCanvasFilerWidget::ExtensionBadgeLabel(badge) +
                      "\" round-trips");
    }
    // Switching the tag on never changes the drawn name: the two switches are
    // independent, which is what lets a display show "UltraFiler" with an
    // "exe" tag under the icon.
    filer.SetFileExtensionsInNames(true);
    filer.SetExtensionBadge(FilerExtensionBadge::Bar);
    CheckEqual(filer.DisplayNameOf(MakeEntry("UltraFiler.exe")),
               "UltraFiler.exe", "name with the bar tag on");

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All file-extension display checks passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
