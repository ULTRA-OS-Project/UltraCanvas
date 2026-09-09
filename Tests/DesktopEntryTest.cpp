// Tests/DesktopEntryTest.cpp
// The freedesktop desktop-entry reader (UltraCanvasDesktopEntry): what a
// ".desktop" file says, which image file its Icon= name resolves to, and the
// argument vector its Exec= line expands into.
//
// The icon lookup is the part worth guarding: an icon name is not a file, it
// is a name to be found in the configured theme, in what that theme
// inherits, and finally in hicolor, at whichever installed size fits best.
// The test builds a throwaway icon theme and points XDG_DATA_HOME at it, so
// it asserts against icons it put there rather than against whatever the
// machine running it happens to have installed.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopEntry.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
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

void WriteTextFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string Join(const std::vector<std::string>& parts) {
    std::string joined;
    for (const std::string& part : parts) {
        if (!joined.empty()) joined += ' ';
        joined += part;
    }
    return joined;
}

} // namespace

int main() {
    std::cout << "===== UltraCanvas Desktop Entry Test =====\n";

    const fs::path root = fs::temp_directory_path() / "ultracanvas-desktopentry-test";
    std::error_code ec;
    fs::remove_all(root, ec);

    // ===== A THROWAWAY ICON THEME =====
    // "UCTest" inherits hicolor and has one 32px icon of its own; hicolor has
    // the same icon at two sizes plus a scalable one, and an icon that only
    // it carries.
    const fs::path dataHome = root / "share";
    const fs::path uctest = dataHome / "icons" / "UCTest";
    const fs::path hicolor = dataHome / "icons" / "hicolor";
    WriteTextFile(uctest / "index.theme",
                  "[Icon Theme]\nName=UCTest\nInherits=hicolor\n");
    WriteTextFile(uctest / "32x32" / "apps" / "themed.png", "PNG32");
    WriteTextFile(hicolor / "48x48" / "apps" / "testapp.png", "PNG48");
    WriteTextFile(hicolor / "64x64" / "apps" / "testapp.png", "PNG64");
    WriteTextFile(hicolor / "scalable" / "apps" / "testapp.svg", "<svg/>");
    WriteTextFile(hicolor / "16x16" / "places" / "somewhere.png", "PNG16");
    WriteTextFile(dataHome / "pixmaps" / "legacy.png", "PNGFLAT");

    setenv("XDG_DATA_HOME", dataHome.c_str(), 1);
    setenv("XDG_DATA_DIRS", (root / "empty").c_str(), 1);
    setenv("HOME", (root / "home").c_str(), 1);
    SetDesktopIconTheme("UCTest");

    std::cout << "\nReading an application launcher\n";
    const fs::path launcher = root / "applications" / "org.example.Editor.desktop";
    WriteTextFile(launcher,
                  "[Desktop Entry]\n"
                  "Type=Application\n"
                  "Version=1.0\n"
                  "Name=Example Editor\n"
                  "Name[de]=Beispiel-Editor\n"
                  "GenericName=Text Editor\n"
                  "Comment=Edit text files\n"
                  "Exec=/bin/sh -c \"echo hello world\" %U\n"
                  "TryExec=/bin/sh\n"
                  "Path=/tmp\n"
                  "Icon=testapp\n"
                  "Terminal=false\n"
                  "MimeType=text/plain;text/markdown;\n"
                  "\n"
                  "[Desktop Action New]\n"
                  "Name=New Window\n"
                  "Exec=/bin/false\n");
    UCDesktopEntry entry;
    Check(ReadDesktopEntry(launcher.string(), entry), "the entry is read");
    Check(entry.kind == UCDesktopEntry::Kind::Application, "it is an application");
    CheckEqual(entry.name, "Example Editor", "name");
    CheckEqual(entry.genericName, "Text Editor", "generic name");
    CheckEqual(entry.comment, "Edit text files", "comment");
    CheckEqual(entry.workingDirectory, "/tmp", "working directory");
    CheckEqual(entry.program, "/bin/sh", "the program it runs (from TryExec)");
    Check(entry.mimeTypes.size() == 2 && entry.mimeTypes[0] == "text/plain",
          "the MIME types it handles");
    Check(!entry.terminal && !entry.noDisplay && !entry.hidden,
          "the flags it does not set");
    // The action group below the main one must not overwrite anything.
    Check(entry.exec.rfind("/bin/sh -c", 0) == 0,
          "a [Desktop Action] group does not leak into the entry -> \"" +
                  entry.exec + "\"");

    std::cout << "\nThe icon it names\n";
    CheckEqual(entry.iconName, "testapp", "the raw Icon= value");
    CheckEqual(FindDesktopIconFile("testapp", 48),
               (hicolor / "48x48" / "apps" / "testapp.png").string(),
               "the exact size wins");
    CheckEqual(FindDesktopIconFile("testapp", 64),
               (hicolor / "64x64" / "apps" / "testapp.png").string(),
               "so does the other exact size");
    CheckEqual(FindDesktopIconFile("testapp", 40),
               (hicolor / "scalable" / "apps" / "testapp.svg").string(),
               "a size nobody installs falls to the scalable icon");
    CheckEqual(FindDesktopIconFile("themed", 32),
               (uctest / "32x32" / "apps" / "themed.png").string(),
               "an icon the configured theme carries itself");
    CheckEqual(FindDesktopIconFile("somewhere", 16),
               (hicolor / "16x16" / "places" / "somewhere.png").string(),
               "an icon filed under another category");
    CheckEqual(FindDesktopIconFile("legacy", 48),
               (dataHome / "pixmaps" / "legacy.png").string(),
               "the flat pixmaps directory is still searched");
    CheckEqual(FindDesktopIconFile("no-such-icon", 48), std::string{},
               "an icon nothing installs resolves to nothing");
    const fs::path absolute = root / "custom" / "icon.png";
    WriteTextFile(absolute, "PNG");
    CheckEqual(FindDesktopIconFile(absolute.string(), 48), absolute.string(),
               "an absolute Icon= path is taken as it stands");
    CheckEqual(FindDesktopIconFile((root / "gone.png").string(), 48), std::string{},
               "an absolute path that is not there resolves to nothing");
    CheckEqual(GetDesktopIconTheme(), "UCTest", "the theme in use");

    std::cout << "\nThe command it runs\n";
    // "echo hello world" is one argument, so the joined form below shows it
    // spaced but DesktopEntryCommand returns three tokens before the file.
    const std::vector<std::string> argv = DesktopEntryCommand(entry, {"/tmp/a.txt"});
    Check(argv.size() == 4 && argv[2] == "echo hello world" &&
                  argv[3] == "/tmp/a.txt",
          "%U takes the files, and a quoted argument stays one argument -> \"" +
                  Join(argv) + "\"");
    Check(DesktopEntryCommand(entry).size() == 3,
          "with no files the field code expands to nothing");
    UCDesktopEntry noCode;
    noCode.exec = "viewer --fullscreen";
    CheckEqual(Join(DesktopEntryCommand(noCode, {"/tmp/a.png"})),
               "viewer --fullscreen /tmp/a.png",
               "an Exec with no field code gets the files appended");
    UCDesktopEntry dropped;
    dropped.exec = "app %i %c %k --flag=100%% %f";
    CheckEqual(Join(DesktopEntryCommand(dropped, {"/tmp/b"})),
               "app --flag=100% /tmp/b",
               "%i/%c/%k are dropped and %% is a literal percent");

    std::cout << "\nOther kinds of entry\n";
    const fs::path link = root / "applications" / "Site.desktop";
    WriteTextFile(link, "[Desktop Entry]\nType=Link\nName=A Site\n"
                        "URL=https://example.com/\nIcon=testapp\n");
    UCDesktopEntry linkEntry;
    Check(ReadDesktopEntry(link.string(), linkEntry) &&
                  linkEntry.kind == UCDesktopEntry::Kind::Link,
          "a Type=Link entry is read as a link");
    CheckEqual(linkEntry.url, "https://example.com/", "the URL it points at");
    Check(linkEntry.program.empty(), "a link runs no program");

    const fs::path hidden = root / "applications" / "Gone.desktop";
    WriteTextFile(hidden, "[Desktop Entry]\nType=Application\nName=Gone\n"
                          "Exec=/bin/false\nHidden=true\nNoDisplay=true\n");
    UCDesktopEntry hiddenEntry;
    Check(ReadDesktopEntry(hidden.string(), hiddenEntry) &&
                  hiddenEntry.hidden && hiddenEntry.noDisplay,
          "Hidden and NoDisplay are reported rather than hiding the entry");

    std::cout << "\nFiles that are not desktop entries\n";
    const fs::path text = root / "applications" / "notes.desktop";
    WriteTextFile(text, "just some text, no group header\n");
    UCDesktopEntry ignored;
    Check(!ReadDesktopEntry(text.string(), ignored),
          "a file with no [Desktop Entry] group is refused");
    Check(!ReadDesktopEntry((root / "missing.desktop").string(), ignored),
          "a file that is not there is refused");
    Check(IsDesktopEntryPath("/a/b.desktop") && IsDesktopEntryPath("/a/B.DESKTOP") &&
                  !IsDesktopEntryPath("/a/b.desk") && !IsDesktopEntryPath("/a/desktop"),
          "the extension test accepts only real .desktop names");

    fs::remove_all(root, ec);
    std::cout << "\n"
              << (g_failures == 0 ? "All desktop entry tests passed"
                                  : std::to_string(g_failures) + " failure(s)")
              << "\n";
    return g_failures == 0 ? 0 : 1;
}
