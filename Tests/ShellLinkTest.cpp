// Tests/ShellLinkTest.cpp
// The Windows shell-link reader (UltraCanvasShellLink): what a ".lnk" says,
// and where the file it names lives on this host.
//
// The links are built here byte by byte rather than checked in as binaries,
// so what each test asserts is visible next to the assertion - and so the
// suite covers the shapes a real desktop holds: a link with the target in
// its LinkInfo block, one that only names it through %ProgramFiles%, one
// with an icon of its own, a link to a folder, and files that end in ".lnk"
// without being links at all.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework

#include "UltraCanvasShellLink.h"

#include "ShellLinkTestSupport.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

using namespace ShellLinkTestSupport;

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

// A throwaway Wine-prefix-shaped tree: <root>/drive_c/... with the
// dosdevices symlinks a prefix carries, so the reader has a real layout to
// walk instead of a mocked one.
struct TestTree {
    fs::path root;

    TestTree() {
        root = fs::temp_directory_path() / "ultracanvas-shelllink-test";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "drive_c" / "Program Files" / "Etcher", ec);
        fs::create_directories(root / "drive_c" / "users" / "Someone" / "Desktop", ec);
        fs::create_directories(root / "drive_c" / "Windows", ec);
        fs::create_directories(root / "drive_c" / "Users", ec);
        fs::create_directories(root / "dosdevices", ec);
        fs::create_directory_symlink("../drive_c", root / "dosdevices" / "c:", ec);
        WriteTextFile(root / "drive_c" / "Program Files" / "Etcher" / "Etcher.exe", "MZ");
        WriteTextFile(root / "drive_c" / "Program Files" / "Etcher" / "app.ico", "icon");
    }

    ~TestTree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    fs::path Desktop() const {
        return root / "drive_c" / "users" / "Someone" / "Desktop";
    }
};

void TestBasicFields(const TestTree& tree) {
    std::cout << "\nA shortcut to a program\n";
    LinkSpec spec;
    spec.flags = IsUnicode | HasLinkInfo | HasName | HasWorkingDir |
                 HasArguments | HasIconLocation;
    spec.localBasePath = "C:\\Program Files\\Etcher\\Etcher.exe";
    spec.description = "Flash OS images";
    spec.workingDirectory = "C:\\Program Files\\Etcher";
    spec.arguments = "--no-sandbox";
    spec.iconLocation = "C:\\Program Files\\Etcher\\app.ico";
    spec.iconIndex = 0;
    const fs::path link = tree.Desktop() / "balenaEtcher.lnk";
    WriteBinaryFile(link, BuildShellLink(spec));

    UCShellLink read;
    Check(ReadShellLink(link.string(), read), "the link is read");
    CheckEqual(read.targetPath, spec.localBasePath, "target path");
    CheckEqual(read.description, spec.description, "description");
    CheckEqual(read.workingDirectory, spec.workingDirectory, "working directory");
    CheckEqual(read.arguments, spec.arguments, "arguments");
    CheckEqual(read.iconLocation, spec.iconLocation, "icon location");
    Check(!read.targetIsDirectory, "the target is a file, not a folder");
    CheckEqual(read.hostTargetPath,
               (tree.root / "drive_c" / "Program Files" / "Etcher" / "Etcher.exe").string(),
               "the target resolves inside the prefix");
    CheckEqual(read.hostIconLocation,
               (tree.root / "drive_c" / "Program Files" / "Etcher" / "app.ico").string(),
               "the icon file resolves too");

    Check(read.iconIndex == 0, "the icon index is the one the header carries");
}

void TestCaseInsensitiveAndEnvironment(const TestTree& tree) {
    std::cout << "\nPaths as Windows wrote them\n";
    // Windows paths are case-insensitive and the host filesystem is not:
    // a link that says PROGRA~ in capitals still names the same file.
    LinkSpec spec;
    spec.flags = IsUnicode | HasLinkInfo;
    spec.localBasePath = "C:\\PROGRAM FILES\\etcher\\ETCHER.EXE";
    const fs::path link = tree.Desktop() / "Shouty.lnk";
    WriteBinaryFile(link, BuildShellLink(spec));
    UCShellLink read;
    Check(ReadShellLink(link.string(), read), "the link is read");
    CheckEqual(read.hostTargetPath,
               (tree.root / "drive_c" / "Program Files" / "Etcher" / "Etcher.exe").string(),
               "case is not what decides whether the target is found");

    // A link written on another machine: the absolute path is wrong here,
    // the environment form is right, and the environment form wins.
    LinkSpec envSpec;
    envSpec.flags = IsUnicode | HasLinkInfo;
    envSpec.localBasePath = "D:\\Games\\Nothing\\Here.exe";
    envSpec.environmentTarget = "%ProgramFiles%\\Etcher\\Etcher.exe";
    const fs::path envLink = tree.Desktop() / "Moved.lnk";
    WriteBinaryFile(envLink, BuildShellLink(envSpec));
    UCShellLink envRead;
    Check(ReadShellLink(envLink.string(), envRead), "the link is read");
    CheckEqual(envRead.hostTargetPath,
               (tree.root / "drive_c" / "Program Files" / "Etcher" / "Etcher.exe").string(),
               "%ProgramFiles% resolves when the stored path does not");
    CheckEqual(envRead.targetPath, envSpec.environmentTarget,
               "and it is the target the link reports");
}

void TestFolderShortcut(const TestTree& tree) {
    std::cout << "\nA shortcut to a folder\n";
    LinkSpec spec;
    spec.flags = IsUnicode | HasLinkInfo;
    spec.fileAttributes = 0x00000010;    // FILE_ATTRIBUTE_DIRECTORY
    spec.localBasePath = "C:\\Program Files\\Etcher";
    const fs::path link = tree.Desktop() / "Etcher folder.lnk";
    WriteBinaryFile(link, BuildShellLink(spec));
    UCShellLink read;
    Check(ReadShellLink(link.string(), read), "the link is read");
    Check(read.targetIsDirectory, "the target is reported as a folder");
    CheckEqual(read.hostTargetPath,
               (tree.root / "drive_c" / "Program Files" / "Etcher").string(),
               "the folder resolves");

    // No icon of its own: what it is drawn with is the target's icon.
    Check(read.iconLocation.empty() && read.hostIconLocation.empty(),
          "the link names no icon of its own, so its target's is drawn");
}

void TestAnsiStrings(const TestTree& tree) {
    std::cout << "\nA link written without the Unicode flag\n";
    LinkSpec spec;
    spec.flags = HasLinkInfo | HasName | HasArguments;   // no IsUnicode
    spec.localBasePath = "C:\\Program Files\\Etcher\\Etcher.exe";
    spec.description = "Old-style link";
    spec.arguments = "-v";
    const fs::path link = tree.Desktop() / "Ansi.lnk";
    WriteBinaryFile(link, BuildShellLink(spec));
    UCShellLink read;
    Check(ReadShellLink(link.string(), read), "the link is read");
    CheckEqual(read.description, spec.description, "description");
    CheckEqual(read.arguments, spec.arguments, "arguments");
}

void TestNotALink(const TestTree& tree) {
    std::cout << "\nFiles that are not shell links\n";
    const fs::path text = tree.Desktop() / "notes.lnk";
    WriteTextFile(text, "This is a text file that happens to end in .lnk.\n");
    UCShellLink read;
    Check(!ReadShellLink(text.string(), read),
          "a file with the wrong header is refused");

    std::vector<uint8_t> truncated = BuildShellLink(LinkSpec{});
    truncated.resize(40);
    const fs::path shortFile = tree.Desktop() / "truncated.lnk";
    WriteBinaryFile(shortFile, truncated);
    Check(!ReadShellLink(shortFile.string(), read),
          "a truncated link is refused");

    Check(!ReadShellLink((tree.Desktop() / "missing.lnk").string(), read),
          "a link that is not there is refused");

    Check(IsShellLinkPath("C:\\x\\App.LNK") && IsShellLinkPath("/a/b.lnk") &&
                  !IsShellLinkPath("/a/b.link") && !IsShellLinkPath("/a/lnk"),
          "the extension test accepts only real .lnk names");
}

void TestWindowsPathMapping(const TestTree& tree) {
    std::cout << "\nMapping a Windows path onto this host\n";
    const std::string context = (tree.Desktop() / "any.lnk").string();
    CheckEqual(ResolveWindowsPathOnHost("C:\\Windows", context),
               (tree.root / "drive_c" / "Windows").string(),
               "a folder inside the prefix");
    CheckEqual(ResolveWindowsPathOnHost("C:\\No Such Folder\\x.exe", context),
               std::string{}, "a path that is not there resolves to nothing");
    CheckEqual(ResolveWindowsPathOnHost("", context), std::string{},
               "an empty path resolves to nothing");
    Check(ExpandWindowsEnvironmentPath("%SystemRoot%\\notepad.exe") ==
                  "C:\\Windows\\notepad.exe",
          "%SystemRoot% expands");
    Check(ExpandWindowsEnvironmentPath("%NotAThing%\\x") == "%NotAThing%\\x",
          "an unknown variable is left visible rather than dropped");
}

} // namespace

int main() {
    std::cout << "===== UltraCanvas Shell Link Test =====\n";
    {
        TestTree tree;
        TestBasicFields(tree);
        TestCaseInsensitiveAndEnvironment(tree);
        TestFolderShortcut(tree);
        TestAnsiStrings(tree);
        TestNotALink(tree);
        TestWindowsPathMapping(tree);
    }
    std::cout << "\n"
              << (g_failures == 0 ? "All shell link tests passed"
                                  : std::to_string(g_failures) + " failure(s)")
              << "\n";
    return g_failures == 0 ? 0 : 1;
}
