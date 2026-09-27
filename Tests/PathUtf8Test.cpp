// Tests/PathUtf8Test.cpp
// UltraCanvasPathUtf8.h: the UTF-8 <-> UTF-16 conversion every Windows file
// name goes through.
//
// On Windows, PathToUtf8 / PathFromUtf8 replace libc++'s code-page conversion
// (which threw "__wide_to_char: Illegal byte sequence" and quit UltraFiler on
// a Thai Windows 10 machine). The conversion itself is platform-independent
// code, so it is tested here on every platform:
//   * Thai, Cyrillic, CJK, emoji (a surrogate pair) and NFD names survive a
//     round trip byte for byte;
//   * malformed UTF-8 (a stray byte, a truncated sequence, an overlong form,
//     an encoded surrogate, a value above U+10FFFF) becomes U+FFFD and never
//     swallows the valid text after it;
//   * an unpaired surrogate - legal in an NTFS name - becomes U+FFFD instead
//     of throwing;
//   * PathFromUtf8 / PathToUtf8 round-trip a real file created on disk.
// Version: 1.0.0
// Last Modified: 2026-09-27
// Author: UltraCanvas Framework

#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::PathUtf8Detail;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

std::string Hex(const std::string& s) {
    std::string out;
    char buf[4];
    for (unsigned char c : s) {
        std::snprintf(buf, sizeof buf, "%02X ", c);
        out += buf;
    }
    return out;
}

void RoundTrip(const std::string& utf8, const std::string& label) {
    const std::u16string wide = Utf8ToUtf16<std::u16string>(utf8);
    const std::string back = Utf16ToUtf8(wide);
    Check(back == utf8, label + " round trip: " + Hex(utf8) + "-> " + Hex(back));
}

const std::string kReplacement = "\xEF\xBF\xBD";   // U+FFFD

} // namespace

int main() {
    // --- valid UTF-8 survives, byte for byte -------------------------------
    RoundTrip("plain-ascii.txt", "ASCII");
    RoundTrip("\xE0\xB8\xA3\xE0\xB8\xB9\xE0\xB8\x9B\xE0\xB8\xA0\xE0\xB8\xB2\xE0\xB8\x9E.png",
              "Thai");
    RoundTrip("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82.docx", "Cyrillic");
    RoundTrip("\xE6\x96\x87\xE4\xBB\xB6.txt", "CJK");
    RoundTrip("holiday \xF0\x9F\x8C\xB4.jpg", "emoji");
    RoundTrip("Cafe\xCC\x81", "NFD combining accent");
    RoundTrip("\xF4\x8F\xBF\xBF", "U+10FFFF");
    RoundTrip("", "empty");

    // An astral character is one surrogate pair in UTF-16.
    {
        const std::u16string w = Utf8ToUtf16<std::u16string>("\xF0\x9F\x8C\xB4");
        Check(w.size() == 2 && w[0] == 0xD83C && w[1] == 0xDF34,
              "emoji becomes the surrogate pair D83C DF34");
    }

    // --- malformed UTF-8 becomes U+FFFD, the rest survives ----------------
    struct Bad { const char* bytes; const char* expected; const char* label; };
    const Bad bad[] = {
        {"a\xFF" "b",             "a\xEF\xBF\xBD" "b",                "stray 0xFF"},
        {"a\x80" "b",             "a\xEF\xBF\xBD" "b",                "lone continuation byte"},
        {"a\xE0\xB8" "b",         "a\xEF\xBF\xBD" "b",                "truncated 3-byte sequence"},
        {"a\xC0\xAF" "b",         "a\xEF\xBF\xBD\xEF\xBF\xBD" "b",    "overlong '/'"},
        {"a\xED\xA0\x80" "b",     "a\xEF\xBF\xBD" "b",                "encoded surrogate"},
        {"a\xF4\x90\x80\x80" "b", "a\xEF\xBF\xBD" "b",                "above U+10FFFF"},
        {"Namens\xE4nderung",     "Namens\xEF\xBF\xBDnderung",        "Latin-1 byte"},
        {"\xE0\xB8",              "\xEF\xBF\xBD",                     "truncated at end"},
    };
    for (const Bad& b : bad) {
        const std::string out = Utf16ToUtf8(Utf8ToUtf16<std::u16string>(b.bytes));
        Check(out == b.expected,
              std::string(b.label) + ": got " + Hex(out) + "expected " + Hex(b.expected));
    }

    // --- an unpaired surrogate from NTFS becomes U+FFFD --------------------
    {
        const std::u16string lead = {u'a', char16_t(0xD83C), u'b'};
        Check(Utf16ToUtf8(lead) == "a" + kReplacement + "b", "unpaired lead surrogate");
        const std::u16string trail = {u'a', char16_t(0xDF34)};
        Check(Utf16ToUtf8(trail) == "a" + kReplacement, "unpaired trail surrogate");
        const std::u16string atEnd = {char16_t(0xD83C)};
        Check(Utf16ToUtf8(atEnd) == kReplacement, "lead surrogate at the end");
    }

    // --- the public functions, against a real file ------------------------
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "uc_path_utf8_test";
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        Check(!ec, "create the scratch folder");

        const std::string name =
            "\xE0\xB8\xA3\xE0\xB8\xB9\xE0\xB8\x9B \xF0\x9F\x8C\xB4 \xE6\x96\x87.txt";
        const std::string full = PathToUtf8(dir) + "/" + name;
        {
            std::ofstream out(PathFromUtf8(full), std::ios::binary);
            out << "x";
        }
        Check(fs::exists(PathFromUtf8(full), ec), "the file exists under its UTF-8 name");

        bool listed = false;
        for (const auto& entry : fs::directory_iterator(dir, ec))
            if (PathToUtf8(entry.path().filename()) == name) listed = true;
        Check(listed, "the directory listing gives the name back as UTF-8");

        std::FILE* f = OpenFileUtf8(full, "rb");
        Check(f != nullptr, "OpenFileUtf8 opens it");
        if (f) std::fclose(f);

        fs::remove_all(dir, ec);
    }

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "PathUtf8Test: all checks passed\n";
    return 0;
}
