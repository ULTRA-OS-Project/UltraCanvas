// Tests/MacBundleTest.cpp
// The macOS half of the shortcut work: the property-list reader
// (UltraCanvasPropertyList) in both encodings, and the application bundles
// and web locations built on it (UltraCanvasMacBundle).
//
// A bundle is a directory with a particular shape, so the test builds one —
// Contents/Info.plist, Contents/MacOS/<executable>, Contents/Resources with
// the icon — and reads it back. Nothing here needs macOS: that is the point,
// since a Mac disk mounted on ULTRA OS, Linux or Windows has to show its
// applications with their real names and icons too.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework

#include "UltraCanvasMacBundle.h"
#include "UltraCanvasPropertyList.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

void WriteBinaryFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

// ===== A BINARY PROPERTY LIST, BUILT BYTE BY BYTE =====
// "bplist00", an object table, and the 32-byte trailer that says where
// everything is. Two keys, so the offset table has five entries: the
// dictionary, two key strings and two values.
struct Bplist {
    std::vector<uint8_t> bytes;
    std::vector<size_t> offsets;

    void U8(uint8_t v) { bytes.push_back(v); }
    void Big(uint64_t v, int width) {
        for (int i = width - 1; i >= 0; --i)
            U8(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
    void Mark() { offsets.push_back(bytes.size()); }
    // Up to 14 characters the length rides in the marker's low nibble;
    // beyond that the nibble is 0xF and an integer object follows with the
    // real count, which is the form a real plist uses for anything longer.
    void AsciiString(const std::string& s) {
        Mark();
        if (s.size() < 0x0F) {
            U8(static_cast<uint8_t>(0x50 | s.size()));
        } else {
            U8(0x5F);
            U8(0x11);                                  // a two-byte integer
            Big(s.size(), 2);
        }
        for (char c : s) U8(static_cast<uint8_t>(c));
    }
    void Integer(uint8_t value) {
        Mark();
        U8(0x10);          // one byte wide
        U8(value);
    }
};

std::vector<uint8_t> BuildBinaryPlist() {
    Bplist plist;
    for (char c : std::string("bplist00")) plist.U8(static_cast<uint8_t>(c));

    // Object 0: the top-level dictionary with two entries; its key refs are
    // objects 1 and 3, its value refs 2 and 4.
    plist.Mark();
    plist.U8(0xD2);        // dict, two entries
    plist.U8(1); plist.U8(3);
    plist.U8(2); plist.U8(4);
    plist.AsciiString("CFBundleName");         // 1
    plist.AsciiString("Binary Example");       // 2
    plist.AsciiString("CFBundleVersionNum");   // 3
    plist.Integer(7);                          // 4

    const size_t offsetTable = plist.bytes.size();
    for (size_t offset : plist.offsets) plist.Big(offset, 1);
    // Trailer: 6 unused bytes, offset size, ref size, counts.
    for (int i = 0; i < 6; ++i) plist.U8(0);
    plist.U8(1);           // one byte per offset
    plist.U8(1);           // one byte per object reference
    plist.Big(plist.offsets.size(), 8);
    plist.Big(0, 8);       // the top object is object 0
    plist.Big(offsetTable, 8);
    return plist.bytes;
}

} // namespace

int main() {
    std::cout << "===== UltraCanvas Mac Bundle Test =====\n";
    const fs::path root = fs::temp_directory_path() / "ultracanvas-macbundle-test";
    std::error_code ec;
    fs::remove_all(root, ec);

    std::cout << "\nProperty lists, XML\n";
    const fs::path xml = root / "Sample.plist";
    WriteTextFile(xml,
                  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                  "<plist version=\"1.0\">\n<dict>\n"
                  "  <key>CFBundleName</key><string>Example</string>\n"
                  "  <key>CFBundleShortVersionString</key><string>2.5</string>\n"
                  "  <key>LSUIElement</key><true/>\n"
                  "  <key>CFBundleNumericThing</key><integer>42</integer>\n"
                  "  <key>CFBundleURLTypes</key>\n"
                  "  <array><dict><key>Nested</key><string>skipped</string></dict></array>\n"
                  "</dict>\n</plist>\n");
    UCPropertyList plist;
    Check(UCPropertyList::Read(xml.string(), plist), "an XML plist is read");
    CheckEqual(plist.GetString("CFBundleName"), "Example", "a string value");
    CheckEqual(plist.GetString("CFBundleShortVersionString"), "2.5", "another");
    Check(plist.GetBool("LSUIElement"), "a boolean value");
    Check(plist.GetInteger("CFBundleNumericThing") == 42, "an integer value");
    Check(!plist.Has("CFBundleURLTypes"),
          "a nested container is skipped rather than half-read");
    Check(!plist.Has("Nested"), "and its contents do not leak to the top level");
    CheckEqual(plist.GetString("NoSuchKey", "fallback"), "fallback",
               "a key that is not there gives the fallback");

    std::cout << "\nProperty lists, binary\n";
    const fs::path binary = root / "Binary.plist";
    WriteBinaryFile(binary, BuildBinaryPlist());
    UCPropertyList binaryPlist;
    Check(UCPropertyList::Read(binary.string(), binaryPlist),
          "a bplist00 is read");
    CheckEqual(binaryPlist.GetString("CFBundleName"), "Binary Example",
               "its string value");
    Check(binaryPlist.GetInteger("CFBundleVersionNum") == 7,
          "its integer value");

    const fs::path notAPlist = root / "notes.plist";
    WriteTextFile(notAPlist, "just text, no plist at all\n");
    UCPropertyList ignored;
    Check(!UCPropertyList::Read(notAPlist.string(), ignored),
          "a file that is not a property list is refused");

    std::cout << "\nAn application bundle\n";
    const fs::path app = root / "Applications" / "Example Editor.app";
    WriteTextFile(app / "Contents" / "Info.plist",
                  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                  "<plist version=\"1.0\">\n<dict>\n"
                  "  <key>CFBundleName</key><string>Example Editor</string>\n"
                  "  <key>CFBundleDisplayName</key><string>Example Editor Pro</string>\n"
                  "  <key>CFBundleIdentifier</key><string>com.example.editor</string>\n"
                  "  <key>CFBundleShortVersionString</key><string>3.1</string>\n"
                  "  <key>CFBundleExecutable</key><string>ExampleEditor</string>\n"
                  "  <key>CFBundlePackageType</key><string>APPL</string>\n"
                  "  <key>CFBundleIconFile</key><string>app</string>\n"
                  "</dict>\n</plist>\n");
    WriteTextFile(app / "Contents" / "MacOS" / "ExampleEditor", "\x7f" "ELF-ish");
    WriteTextFile(app / "Contents" / "Resources" / "app.icns", "icns");
    UCAppBundle bundle;
    Check(ReadApplicationBundle(app.string(), bundle), "the bundle is read");
    CheckEqual(bundle.displayName, "Example Editor Pro",
               "the display name wins over the bundle name");
    CheckEqual(bundle.identifier, "com.example.editor", "the identifier");
    CheckEqual(bundle.version, "3.1", "the version");
    CheckEqual(bundle.executable,
               (app / "Contents" / "MacOS" / "ExampleEditor").string(),
               "the executable inside it");
    CheckEqual(bundle.iconFile, (app / "Contents" / "Resources" / "app.icns").string(),
               "the icon it names, completed with its extension");
    Check(bundle.isApplication, "it is an application");
    Check(IsApplicationBundlePath(app.string()) && IsBundlePath(app.string()),
          "and its path is recognised as one");

    std::cout << "\nA bundle that names no icon\n";
    const fs::path plain = root / "Applications" / "Plain.app";
    WriteTextFile(plain / "Contents" / "Info.plist",
                  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                  "<plist version=\"1.0\">\n<dict>\n"
                  "  <key>CFBundleExecutable</key><string>Plain</string>\n"
                  "</dict>\n</plist>\n");
    WriteTextFile(plain / "Contents" / "MacOS" / "Plain", "x");
    WriteTextFile(plain / "Contents" / "Resources" / "AppIcon.icns", "icns");
    UCAppBundle plainBundle;
    Check(ReadApplicationBundle(plain.string(), plainBundle), "it is read");
    CheckEqual(plainBundle.displayName, "Plain",
               "its name falls back to the folder name without \".app\"");
    CheckEqual(plainBundle.iconFile,
               (plain / "Contents" / "Resources" / "AppIcon.icns").string(),
               "and the icon falls back to the one in Resources");
    Check(plainBundle.isApplication,
          "an executable where an application keeps one makes it one");

    std::cout << "\nDirectories that only look like bundles\n";
    const fs::path fake = root / "Applications" / "NotAnApp.app";
    fs::create_directories(fake, ec);
    UCAppBundle none;
    Check(!ReadApplicationBundle(fake.string(), none),
          "a folder with no Info.plist is not an application");
    Check(!ReadApplicationBundle((root / "Applications").string(), none),
          "and neither is an ordinary folder");

    std::cout << "\nA web location\n";
    const fs::path webloc = root / "Example.webloc";
    WriteTextFile(webloc,
                  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                  "<plist version=\"1.0\">\n<dict>\n"
                  "  <key>URL</key><string>https://example.com/page</string>\n"
                  "</dict>\n</plist>\n");
    std::string url;
    Check(ReadWebLocation(webloc.string(), url), "it is read");
    CheckEqual(url, "https://example.com/page", "the address it holds");
    Check(IsWebLocationPath(webloc.string()) &&
                  !IsWebLocationPath((root / "x.web").string()),
          "the extension test accepts only real .webloc names");
    std::string ignoredUrl;
    Check(!ReadWebLocation(notAPlist.string(), ignoredUrl),
          "a file that is not one is refused");

    std::cout << "\nFinder aliases\n";
    const fs::path aliasFile = root / "Some Alias";
    WriteBinaryFile(aliasFile, {'b', 'o', 'o', 'k', 0, 0, 0, 0});
    Check(IsFinderAliasFile(aliasFile.string()),
          "bookmark data is recognised by its magic");
    Check(!IsFinderAliasFile(notAPlist.string()),
          "and a text file is not");
#ifndef __APPLE__
    std::string target;
    Check(!ResolveFinderAlias(aliasFile.string(), target),
          "off macOS an alias cannot be resolved, and says so");
#endif

    fs::remove_all(root, ec);
    std::cout << "\n"
              << (g_failures == 0 ? "All mac bundle tests passed"
                                  : std::to_string(g_failures) + " failure(s)")
              << "\n";
    return g_failures == 0 ? 0 : 1;
}
