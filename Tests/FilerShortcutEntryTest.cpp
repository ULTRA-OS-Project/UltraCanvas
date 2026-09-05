// Tests/FilerShortcutEntryTest.cpp
// What the file display makes of a shortcut it lists: the entry a scan
// produces for a Windows ".lnk" and for a freedesktop ".desktop"
// (UltraCanvasFilerWidget::GetEntries()).
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

// Two paths naming the same file, rather than the same spelling of one.
bool SamePath(const std::string& got, const fs::path& expected) {
    if (got.empty()) return false;
    std::error_code ec;
    return fs::equivalent(got, expected, ec) && !ec;
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

    // What the links store. Off Windows the tree is a Wine prefix and the
    // links say "C:\..." like the shortcuts on a real Windows desktop; on
    // Windows there is no prefix to map through, so they say where these
    // files actually are - which is the only path that system resolves.
    const fs::path exe = root / "drive_c" / "Program Files" / "Etcher" / "Etcher.exe";
    const fs::path folderTarget = root / "drive_c" / "Program Files" / "Etcher";
    const fs::path documentTarget =
            root / "drive_c" / "Program Files" / "Etcher" / "readme.txt";
#ifdef _WIN32
    const std::string storedExe = exe.string();
    const std::string storedFolder = folderTarget.string();
    const std::string storedDocument = documentTarget.string();
#else
    const std::string storedExe = "C:\\Program Files\\Etcher\\Etcher.exe";
    const std::string storedFolder = "C:\\Program Files\\Etcher";
    const std::string storedDocument = "C:\\Program Files\\Etcher\\readme.txt";
#endif

    LinkSpec program;
    program.flags = IsUnicode | HasLinkInfo | HasArguments;
    program.localBasePath = storedExe;
    program.arguments = "--no-sandbox";
    WriteBinaryFile(desktop / "balenaEtcher.lnk", BuildShellLink(program));

    LinkSpec folder;
    folder.flags = IsUnicode | HasLinkInfo;
    folder.fileAttributes = 0x00000010;      // FILE_ATTRIBUTE_DIRECTORY
    folder.localBasePath = storedFolder;
    WriteBinaryFile(desktop / "Etcher folder.lnk", BuildShellLink(folder));

    LinkSpec document;
    document.flags = IsUnicode | HasLinkInfo;
    document.localBasePath = storedDocument;
    WriteBinaryFile(desktop / "Read me.lnk", BuildShellLink(document));

    LinkSpec missing;
    missing.flags = IsUnicode | HasLinkInfo;
    // A drive letter no test tree can supply, on either platform.
    missing.localBasePath = "Q:\\Gone\\Away.exe";
    WriteBinaryFile(desktop / "Missing.lnk", BuildShellLink(missing));

    WriteTextFile(desktop / "notes.lnk", "not a shell link at all\n");
    WriteTextFile(desktop / "notes.desktop", "no group header, just text\n");

    // Two freedesktop launchers: the Linux half of the same idea.
    WriteTextFile(desktop / "org.example.Editor.desktop",
                  "[Desktop Entry]\nType=Application\nName=Example Editor\n"
                  "Exec=/bin/sh -c \"echo hi\" %U\nTryExec=/bin/sh\n"
                  "Icon=text-editor\n");
    WriteTextFile(desktop / "Site.desktop",
                  "[Desktop Entry]\nType=Link\nName=A Site\n"
                  "URL=https://example.com/\nIcon=web-browser\n");

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
        Check(e->info == storedExe,
              "the info column names the target -> \"" + e->info + "\"");
        Check(SamePath(e->linkTarget, exe),
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

    std::cout << "\nA desktop entry that launches a program\n";
    if (const FilerEntry* e = FindEntry(*filer, "org.example.Editor.desktop")) {
        Check(e->isShortcut, "it is recognised as a shortcut");
        Check(e->typeName == "Shortcut",
              "its type is \"Shortcut\" -> \"" + e->typeName + "\"");
        Check(e->category == FilerFileCategory::Executable,
              "its category is the program it starts");
        Check(e->linkDisplayName == "Example Editor",
              "it is drawn by the name it calls itself -> \"" +
                      e->linkDisplayName + "\"");
        Check(SamePath(e->linkTarget, "/bin/sh"),
              "linkTarget is the program, resolved on this machine -> \"" +
                      e->linkTarget + "\"");
    } else {
        Check(false, "the desktop entry is listed");
    }

    std::cout << "\nA desktop entry that opens a web address\n";
    if (const FilerEntry* e = FindEntry(*filer, "Site.desktop")) {
        Check(e->isShortcut && e->linkDisplayName == "A Site",
              "it is a shortcut, drawn by its name");
        Check(e->info == "https://example.com/",
              "the info column shows the address -> \"" + e->info + "\"");
        Check(e->linkTarget.empty(),
              "and linkTarget stays empty: an address is not a file here");
    } else {
        Check(false, "the desktop entry is listed");
    }

    std::cout << "\nA file that only ends in .lnk or .desktop\n";
    if (const FilerEntry* e = FindEntry(*filer, "notes.lnk")) {
        Check(!e->isShortcut, "the .lnk is not treated as a shortcut");
        Check(e->linkTarget.empty(), "and points at nothing");
    } else {
        Check(false, "the file is listed");
    }
    if (const FilerEntry* e = FindEntry(*filer, "notes.desktop")) {
        Check(!e->isShortcut, "the .desktop is not treated as a shortcut");
        Check(e->linkDisplayName.empty(),
              "and is drawn by its own file name");
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
