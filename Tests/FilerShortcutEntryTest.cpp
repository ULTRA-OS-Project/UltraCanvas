// Tests/FilerShortcutEntryTest.cpp
// What the file display makes of a Windows shortcut it lists: the entry a
// scan produces for a ".lnk" (UltraCanvasFilerWidget::GetEntries()).
//
// The rule this guards: a shortcut reads as the thing it points at, not as a
// file called "LNK". Its type is "Shortcut", its category is the target's —
// which is what its colour, its grouping and the preview switch that governs
// it come from — its info column names the target, and `linkTarget` is that
// target as THIS host opens it, which is what an application needs to launch
// the real program. A file that merely ends in ".lnk" is none of this.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"

#include "ShellLinkTestSupport.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

using namespace ShellLinkTestSupport;

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

void WriteBinaryFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

void WriteTextFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

const FilerEntry* FindEntry(const UltraCanvasFilerWidget& filer,
                            const std::string& name) {
    for (const FilerEntry& e : filer.GetEntries())
        if (e.name == name) return &e;
    return nullptr;
}

} // namespace

int main() {
    std::cout << "===== UltraCanvas Filer Shortcut Entry Test =====\n";

    // A Wine-prefix-shaped tree, so the shortcuts' Windows paths have
    // something on this host to resolve against.
    const fs::path root = fs::temp_directory_path() / "ultracanvas-filer-shortcut-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path desktop = root / "drive_c" / "users" / "Someone" / "Desktop";
    fs::create_directories(desktop, ec);
    fs::create_directories(root / "dosdevices", ec);
    fs::create_directory_symlink("../drive_c", root / "dosdevices" / "c:", ec);
    WriteTextFile(root / "drive_c" / "Program Files" / "Etcher" / "Etcher.exe", "MZ");
    WriteTextFile(root / "drive_c" / "Program Files" / "Etcher" / "readme.txt", "hi");

    LinkSpec program;
    program.flags = IsUnicode | HasLinkInfo | HasArguments;
    program.localBasePath = "C:\\Program Files\\Etcher\\Etcher.exe";
    program.arguments = "--no-sandbox";
    WriteBinaryFile(desktop / "balenaEtcher.lnk", BuildShellLink(program));

    LinkSpec folder;
    folder.flags = IsUnicode | HasLinkInfo;
    folder.fileAttributes = 0x00000010;      // FILE_ATTRIBUTE_DIRECTORY
    folder.localBasePath = "C:\\Program Files\\Etcher";
    WriteBinaryFile(desktop / "Etcher folder.lnk", BuildShellLink(folder));

    LinkSpec document;
    document.flags = IsUnicode | HasLinkInfo;
    document.localBasePath = "C:\\Program Files\\Etcher\\readme.txt";
    WriteBinaryFile(desktop / "Read me.lnk", BuildShellLink(document));

    LinkSpec missing;
    missing.flags = IsUnicode | HasLinkInfo;
    missing.localBasePath = "D:\\Gone\\Away.exe";
    WriteBinaryFile(desktop / "Missing.lnk", BuildShellLink(missing));

    WriteTextFile(desktop / "notes.lnk", "not a shell link at all\n");

    auto filer = std::make_shared<UltraCanvasFilerWidget>("shortcut-test",
                                                          0, 0, 800, 600);
    filer->SetPath(desktop.string());

    std::cout << "\nA shortcut to a program\n";
    if (const FilerEntry* e = FindEntry(*filer, "balenaEtcher.lnk")) {
        Check(e->isShortcut, "it is recognised as a shortcut");
        Check(e->typeName == "Shortcut",
              "its type is \"Shortcut\" -> \"" + e->typeName + "\"");
        Check(e->category == FilerFileCategory::Executable,
              "its category is the target's (a program)");
        Check(e->info == program.localBasePath,
              "the info column names the target -> \"" + e->info + "\"");
        Check(e->linkTarget ==
                      (root / "drive_c" / "Program Files" / "Etcher" / "Etcher.exe").string(),
              "linkTarget is the target as this host opens it -> \"" +
                      e->linkTarget + "\"");
    } else {
        Check(false, "the shortcut is listed");
    }

    std::cout << "\nA shortcut to a folder\n";
    if (const FilerEntry* e = FindEntry(*filer, "Etcher folder.lnk")) {
        Check(e->isShortcut && e->category == FilerFileCategory::Folder,
              "it groups and colours as a folder");
        Check(!e->isDirectory,
              "but it is still a file: it is not navigated into as one");
    } else {
        Check(false, "the shortcut is listed");
    }

    std::cout << "\nA shortcut to a document\n";
    if (const FilerEntry* e = FindEntry(*filer, "Read me.lnk")) {
        Check(e->isShortcut && e->category == FilerFileCategory::Text,
              "it takes the category of the document it points at");
    } else {
        Check(false, "the shortcut is listed");
    }

    std::cout << "\nA shortcut whose target is not on this machine\n";
    if (const FilerEntry* e = FindEntry(*filer, "Missing.lnk")) {
        Check(e->isShortcut, "it is still a shortcut");
        Check(e->linkTarget.empty(), "with nothing on this host to point at");
        Check(e->info == missing.localBasePath,
              "and the info column still says where it wanted to go");
    } else {
        Check(false, "the shortcut is listed");
    }

    std::cout << "\nA file that only ends in .lnk\n";
    if (const FilerEntry* e = FindEntry(*filer, "notes.lnk")) {
        Check(!e->isShortcut, "it is not treated as a shortcut");
        Check(e->linkTarget.empty(), "and points at nothing");
    } else {
        Check(false, "the file is listed");
    }

    fs::remove_all(root, ec);
    std::cout << "\n"
              << (g_failures == 0 ? "All filer shortcut entry tests passed"
                                  : std::to_string(g_failures) + " failure(s)")
              << "\n";
    return g_failures == 0 ? 0 : 1;
}
