// Tests/IconResourceTest.cpp
// The portable icon reader (UltraCanvasIconResource): an ".ico" file, and
// the RT_GROUP_ICON / RT_ICON resources of a PE binary.
//
// Both inputs are assembled here rather than checked in, so the bytes the
// reader is asked to understand are visible beside the expectation - and so
// the PE walk (section table, three-level resource tree, group icon naming
// the icon resource) is exercised without shipping an executable in the
// source tree.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework

#include "UltraCanvasIconResource.h"

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

struct Blob {
    std::vector<uint8_t> bytes;
    void U8(uint8_t v) { bytes.push_back(v); }
    void U16(uint16_t v) { U8(v & 0xFF); U8((v >> 8) & 0xFF); }
    void U32(uint32_t v) { U16(v & 0xFFFF); U16((v >> 16) & 0xFFFF); }
    void Raw(const std::vector<uint8_t>& v) {
        bytes.insert(bytes.end(), v.begin(), v.end());
    }
    void Pad(size_t to) { while (bytes.size() < to) U8(0); }
    size_t Size() const { return bytes.size(); }
};

// ===== ONE 4x4 ICON =====
// Opaque red, except the top-left pixel, which the AND mask hides. Small
// enough to check pixel by pixel, and it covers what a real icon frame
// exercises: bottom-up rows, the mask below the colour bitmap, and the
// premultiplication the pixmap expects.
constexpr int kIconEdge = 4;

std::vector<uint8_t> BuildIconImage() {
    Blob out;
    out.U32(40);                       // BITMAPINFOHEADER
    out.U32(kIconEdge);                // width
    out.U32(kIconEdge * 2);            // height: colour rows + mask rows
    out.U16(1);                        // planes
    out.U16(32);                       // bits per pixel
    out.U32(0);                        // BI_RGB
    out.U32(0);                        // image size (may be 0 for BI_RGB)
    out.U32(0); out.U32(0);            // pixels per metre
    out.U32(0); out.U32(0);            // palette counts
    // Colour rows, bottom-up. The alpha channel is left at zero so the AND
    // mask is what decides coverage - the pre-XP form, and the one that
    // catches a decoder that trusts an unused alpha band.
    for (int y = 0; y < kIconEdge; ++y) {
        for (int x = 0; x < kIconEdge; ++x) {
            out.U8(0x00);              // blue
            out.U8(0x00);              // green
            out.U8(0xFF);              // red
            out.U8(0x00);              // alpha (unused)
        }
    }
    // AND mask rows, bottom-up, 4 bytes per row: a set bit is transparent.
    for (int y = 0; y < kIconEdge; ++y) {
        const bool topRow = (y == kIconEdge - 1);   // bottom-up: last is top
        out.U8(topRow ? 0x80 : 0x00);
        out.U8(0); out.U8(0); out.U8(0);
    }
    return out.bytes;
}

std::vector<uint8_t> BuildIcoFile() {
    const std::vector<uint8_t> image = BuildIconImage();
    Blob out;
    out.U16(0);                        // reserved
    out.U16(1);                        // type: icon
    out.U16(1);                        // one frame
    out.U8(kIconEdge);                 // width
    out.U8(kIconEdge);                 // height
    out.U8(0);                         // palette size
    out.U8(0);                         // reserved
    out.U16(1);                        // planes
    out.U16(32);                       // bits per pixel
    out.U32(static_cast<uint32_t>(image.size()));
    out.U32(22);                       // offset of the frame
    out.Raw(image);
    return out.bytes;
}

// ===== A PE BINARY WITH ONE ICON =====
// The smallest file the reader accepts: a DOS stub, a PE header with one
// section, and a resource section holding the three-level tree.
std::vector<uint8_t> BuildExecutable() {
    const std::vector<uint8_t> image = BuildIconImage();
    constexpr uint32_t kSectionRva = 0x1000;
    constexpr uint32_t kSectionFileOffset = 0x400;

    // --- the resource section, laid out first so its size is known ---
    // Offsets inside it are relative to its start.
    // The root directory starts at 0 and is 16 + 2 * 8 bytes; every offset
    // below is where the record after it lands.
    constexpr uint32_t kIconTypeDir = 32;             // 16 + 8 = 24
    constexpr uint32_t kIconNameDir = 56;             // 16 + 8 = 24
    constexpr uint32_t kIconLeaf = 80;                // 16
    constexpr uint32_t kGroupTypeDir = 96;            // 24
    constexpr uint32_t kGroupNameDir = 120;           // 24
    constexpr uint32_t kGroupLeaf = 144;              // 16
    constexpr uint32_t kGroupData = 160;              // 6 + 14 = 20
    constexpr uint32_t kIconData = 180;

    Blob res;
    auto directory = [&res](uint16_t idEntries) {
        res.U32(0);                    // characteristics
        res.U32(0);                    // time stamp
        res.U16(0); res.U16(0);        // version
        res.U16(0);                    // named entries
        res.U16(idEntries);            // id entries
    };
    auto entry = [&res](uint32_t id, uint32_t offset, bool isDirectory) {
        res.U32(id);
        res.U32(isDirectory ? (offset | 0x80000000u) : offset);
    };
    // Root: RT_ICON (3) and RT_GROUP_ICON (14).
    directory(2);
    entry(3, kIconTypeDir, true);
    entry(14, kGroupTypeDir, true);
    // RT_ICON -> name 1 -> language 1033 -> data.
    directory(1); entry(1, kIconNameDir, true);
    directory(1); entry(1033, kIconLeaf, false);
    res.U32(kSectionRva + kIconData);
    res.U32(static_cast<uint32_t>(image.size()));
    res.U32(0); res.U32(0);
    // RT_GROUP_ICON -> name 1 -> language 1033 -> data.
    directory(1); entry(1, kGroupNameDir, true);
    directory(1); entry(1033, kGroupLeaf, false);
    res.U32(kSectionRva + kGroupData);
    res.U32(20);
    res.U32(0); res.U32(0);
    // The group icon directory: one record, naming RT_ICON resource 1.
    res.U16(0); res.U16(1); res.U16(1);
    res.U8(kIconEdge); res.U8(kIconEdge); res.U8(0); res.U8(0);
    res.U16(1); res.U16(32);
    res.U32(static_cast<uint32_t>(image.size()));
    res.U16(1);
    // The icon itself.
    res.Raw(image);

    // --- the file around it ---
    Blob out;
    out.U8('M'); out.U8('Z');
    out.Pad(0x3C);
    out.U32(0x40);                     // e_lfanew
    out.U32(0x00004550);               // "PE\0\0"
    out.U16(0x8664);                   // machine
    out.U16(1);                        // one section
    out.U32(0); out.U32(0); out.U32(0);
    out.U16(240);                      // size of the optional header
    out.U16(0x2022);                   // characteristics
    const size_t optionalAt = out.Size();
    out.U16(0x020B);                   // PE32+
    out.Pad(optionalAt + 112);         // up to the data directory
    out.U32(0); out.U32(0);            // [0] export
    out.U32(0); out.U32(0);            // [1] import
    out.U32(kSectionRva);              // [2] resource: RVA
    out.U32(static_cast<uint32_t>(res.Size()));
    out.Pad(optionalAt + 240);         // the rest of the data directory
    // The section table.
    const std::string name = ".rsrc";
    for (size_t i = 0; i < 8; ++i)
        out.U8(i < name.size() ? static_cast<uint8_t>(name[i]) : 0);
    out.U32(static_cast<uint32_t>(res.Size()));   // virtual size
    out.U32(kSectionRva);                         // virtual address
    out.U32(static_cast<uint32_t>(res.Size()));   // raw size
    out.U32(kSectionFileOffset);                  // raw offset
    out.U32(0); out.U32(0); out.U32(0); out.U32(0);
    out.Pad(kSectionFileOffset);
    out.Raw(res.bytes);
    return out.bytes;
}

void WriteBinaryFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

// The pixmap holds premultiplied ARGB32 in native-endian words.
bool IsOpaqueRed(uint32_t pixel) { return pixel == 0xFFFF0000u; }
bool IsTransparent(uint32_t pixel) { return (pixel >> 24) == 0; }

void CheckDecodedIcon(const std::shared_ptr<UCPixmap>& pixmap,
                      const std::string& what) {
    if (!pixmap || !pixmap->IsValid()) {
        Check(false, what + ": decoded");
        return;
    }
    Check(pixmap->GetWidth() == kIconEdge && pixmap->GetHeight() == kIconEdge,
          what + ": " + std::to_string(pixmap->GetWidth()) + "x" +
                  std::to_string(pixmap->GetHeight()) + " pixels");
    Check(IsOpaqueRed(pixmap->GetPixel(1, 1)),
          what + ": the picture is the colour the frame stores");
    Check(IsTransparent(pixmap->GetPixel(0, 0)),
          what + ": the AND mask makes its pixel transparent");
    Check(!IsTransparent(pixmap->GetPixel(3, 3)),
          what + ": the rest of the icon stays opaque");
}

} // namespace

int main() {
    std::cout << "===== UltraCanvas Icon Resource Test =====\n";
    const fs::path root = fs::temp_directory_path() / "ultracanvas-iconresource-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    std::cout << "\nWhich files can hold an icon\n";
    Check(HasIconResourceExtension("C:\\x\\App.EXE") &&
                  HasIconResourceExtension("/a/b.dll") &&
                  HasIconResourceExtension("/a/b.ico") &&
                  !HasIconResourceExtension("/a/b.png") &&
                  !HasIconResourceExtension("/a/b"),
          "the extension test accepts the icon-carrying kinds only");

    std::cout << "\nAn .ico file\n";
    const fs::path ico = root / "app.ico";
    WriteBinaryFile(ico, BuildIcoFile());
    CheckDecodedIcon(LoadIconResource(ico.string(), 0, 32), "the icon file");
    CheckDecodedIcon(DecodeIconFileBytes(BuildIcoFile(), 32), "the same bytes in memory");

    std::cout << "\nA program's own icon\n";
    const fs::path exe = root / "App.exe";
    WriteBinaryFile(exe, BuildExecutable());
    CheckDecodedIcon(LoadIconResource(exe.string(), 0, 32), "icon 0 of the program");
    // A negative index names a resource id - the form a shortcut's icon
    // location uses when it points into a library.
    CheckDecodedIcon(LoadIconResource(exe.string(), -1, 32), "resource id 1");
    // An index past the end still draws the program's icon rather than
    // nothing: a stale index in a shortcut must not blank the tile.
    CheckDecodedIcon(LoadIconResource(exe.string(), 7, 32),
                     "an index the file does not have");

    std::cout << "\nFiles with no icon in them\n";
    const fs::path text = root / "notes.ico";
    WriteBinaryFile(text, {'h', 'e', 'l', 'l', 'o', '\n'});
    Check(LoadIconResource(text.string(), 0, 32) == nullptr,
          "a file that is not an icon decodes to nothing");
    Check(LoadIconResource((root / "missing.exe").string(), 0, 32) == nullptr,
          "a file that is not there decodes to nothing");
    Check(LoadIconResource((root / "app.png").string(), 0, 32) == nullptr,
          "a format this reader does not claim is refused outright");

    fs::remove_all(root, ec);
    std::cout << "\n"
              << (g_failures == 0 ? "All icon resource tests passed"
                                  : std::to_string(g_failures) + " failure(s)")
              << "\n";
    return g_failures == 0 ? 0 : 1;
}
