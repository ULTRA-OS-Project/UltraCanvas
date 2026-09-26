// Tests/FilerNameEncodingTest.cpp
// The file display draws every file name as readable text: German umlauts,
// Thai, Russian, Chinese - and names that are not UTF-8 at all.
//
// A name on disk is whatever bytes the program that made it wrote. One in a
// legacy code page ("Namens\xE4nderung" from an old Latin-1 tool,
// "Namens\x84nderung" out of a ZIP made on Windows and unpacked without
// re-encoding) reached the text renderer as invalid UTF-8, and every such byte
// was drawn as U+FFFD: the folder "Namensänderung" read "Namens•nderung".
// This guards:
//   * RepairLegacyEncodedName (UltraCanvasTextUtils): UTF-8 is returned
//     unchanged, Windows-1252 and IBM437 names are decoded, malformed UTF-8
//     never passes as valid;
//   * UltraCanvasFilerWidget::DisplayNameOf draws the decoded name, with the
//     extension switch still working on it;
//   * the caption wrapping and ellipsizing never cut a Thai / Cyrillic / CJK
//     name inside a character;
//   * a real folder scan lists such names intact, and a non-UTF-8 name keeps
//     its real bytes for every file operation while showing decoded.
// Version: 1.0.1
// Last Modified: 2026-09-25
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCanvasTextWrapping.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if defined(__linux__) && defined(__GLIBC__)
#include <execinfo.h>
#include <csignal>
#include <unistd.h>
#define FILER_NAME_TEST_HAVE_BACKTRACE 1
#endif

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

#ifdef FILER_NAME_TEST_HAVE_BACKTRACE
// This test crashed on the ARM Linux runner only, after its last check, and a
// CI runner has no core dump and no debugger to ask. The handler names the
// frames. Only async-signal-safe calls here: backtrace_symbols_fd writes
// straight to the fd.
void CrashHandler(int sig) {
    static const char msg[] = "\n*** FilerNameEncodingTest crashed - backtrace ***\n";
    ssize_t ignored = write(STDERR_FILENO, msg, sizeof(msg) - 1);
    (void)ignored;
    void* frames[48];
    const int n = backtrace(frames, 48);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    _exit(128 + sig);
}
#endif

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

void CheckEqual(const std::string& got, const std::string& expected,
                const std::string& what) {
    Check(got == expected, what + " -> \"" + got + "\" (expected \"" +
                           expected + "\")");
}

// Names in UTF-8, spelled as bytes so the test does not depend on the
// encoding the compiler assumes for its source file.
const std::string kGerman  = "Namens\xC3\xA4nderung";                     // Namensänderung
const std::string kUmlauts = "\xC3\x84rger \xC3\x96l \xC3\x9C" "ber Gr\xC3\xBC\xC3\x9F" "e";  // Ärger Öl Über Grüße
const std::string kThai    = "\xE0\xB8\xA0\xE0\xB8\xB2\xE0\xB8\xA9\xE0\xB8\xB2"
                             "\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2";      // ภาษาไทย
const std::string kRussian = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 "
                             "\xD0\xBC\xD0\xB8\xD1\x80";                  // Привет мир
const std::string kChinese = "\xE4\xB8\xAD\xE6\x96\x87\xE6\x96\x87\xE4\xBB\xB6"; // 中文文件
const std::string kEmoji   = "Urlaub \xF0\x9F\x8C\xB4";                    // Urlaub 🌴
// "Namensänderung" as the same name looks decomposed (macOS writes NFD):
// a + COMBINING DIAERESIS. Valid UTF-8, so it must be left alone.
const std::string kGermanNfd = "Namensa\xCC\x88nderung";

FilerEntry MakeEntry(const std::string& name, bool isDirectory = false) {
    FilerEntry e;
    e.name = name;
    e.path = "/tmp/" + name;
    e.isDirectory = isDirectory;
    const size_t dot = name.find_last_of('.');
    if (!isDirectory && dot != std::string::npos && dot != 0 && dot + 1 < name.size()) {
        e.extension = name.substr(dot + 1);
    }
    return e;
}

// A synthetic font for the wrapping code: every code point is 8 px wide.
struct CodePointMeasure {
    int operator()(const std::string& s) const {
        return static_cast<int>(TextWrapping::Utf8Count(s, 0, s.size())) * 8;
    }
};

const FilerEntry* FindByDisplayName(const UltraCanvasFilerWidget& filer,
                                    const std::string& shown) {
    for (const FilerEntry& e : filer.GetEntries())
        if (filer.DisplayNameOf(e) == shown) return &e;
    return nullptr;
}

} // namespace

int main() {
#ifdef FILER_NAME_TEST_HAVE_BACKTRACE
    std::signal(SIGSEGV, CrashHandler);
    std::signal(SIGABRT, CrashHandler);
    std::signal(SIGBUS, CrashHandler);
#endif
    // Unbuffered: stdout to a pipe is block-buffered, and a crash would take
    // the buffer with it - the log would stop mid-line, far from the fault.
    std::cout << std::unitbuf;
    std::cout << "===== Filer: file names in every script =====\n";

    std::cout << "\n-- UTF-8 is recognised and left alone --\n";
    for (const std::string& s : {kGerman, kUmlauts, kThai, kRussian, kChinese,
                                 kEmoji, kGermanNfd, std::string("plain.txt"),
                                 std::string()}) {
        Check(IsWellFormedUtf8(s), "well-formed: \"" + s + "\"");
        CheckEqual(RepairLegacyEncodedName(s), s, "unchanged");
    }

    std::cout << "\n-- Malformed UTF-8 is not taken for UTF-8 --\n";
    Check(!IsWellFormedUtf8("Namens\xE4nderung"), "a lone Latin-1 byte");
    Check(!IsWellFormedUtf8("\xE0\xB8"), "a Thai character cut in half");
    Check(!IsWellFormedUtf8("\xC0\xAF"), "an overlong '/'");
    Check(!IsWellFormedUtf8("\xED\xA0\x80"), "a UTF-16 surrogate");
    Check(!IsWellFormedUtf8("\xF4\x90\x80\x80"), "a code point past U+10FFFF");
    Check(!IsWellFormedUtf8("\x80"), "a stray continuation byte");

    std::cout << "\n-- Legacy code pages are decoded --\n";
    CheckEqual(RepairLegacyEncodedName("Namens\xE4nderung"), kGerman,
               "Windows-1252 / Latin-1 \"Namens\\xE4nderung\"");
    CheckEqual(RepairLegacyEncodedName("Namens\x84nderung"), kGerman,
               "IBM437 (ZIP from Windows) \"Namens\\x84nderung\"");
    CheckEqual(RepairLegacyEncodedName("Gr\xFC\xDF" "e.txt"),
               "Gr\xC3\xBC\xC3\x9F" "e.txt", "Latin-1 \"Gr\\xFC\\xDFe.txt\"");
    CheckEqual(RepairLegacyEncodedName("Gr\x81\xE1" "e.txt"),
               "Gr\xC3\xBC\xC3\x9F" "e.txt", "IBM437 \"Gr\\x81\\xE1e.txt\"");
    CheckEqual(RepairLegacyEncodedName("\x8E" "nderung \x99l"),
               "\xC3\x84nderung \xC3\x96l", "IBM437 capitals \"\\x8Enderung \\x99l\"");
    CheckEqual(RepairLegacyEncodedName("caf\xE9"), "caf\xC3\xA9", "Latin-1 \"caf\\xE9\"");
    CheckEqual(RepairLegacyEncodedName("Preis 5\x80"), "Preis 5\xE2\x82\xAC",
               "Windows-1252 euro sign");
    CheckEqual(RepairLegacyEncodedName("Bericht \x96 Entwurf"),
               "Bericht \xE2\x80\x93 Entwurf", "Windows-1252 en dash between words");
    CheckEqual(RepairLegacyEncodedName("M\xE4rz \x96 Entwurf"),
               "M\xC3\xA4rz \xE2\x80\x93 Entwurf", "Latin-1 umlaut beside a 1252 dash");
    CheckEqual(RepairLegacyEncodedName(kGerman + " \xE4"), kGerman + " \xC3\xA4",
               "valid UTF-8 runs are kept beside a stray byte");
    Check(IsWellFormedUtf8(RepairLegacyEncodedName("\xFF\xFE\x80\xC0\xAF")),
          "whatever the bytes, the result is UTF-8");

    std::cout << "\n-- The file display draws the decoded name --\n";
    UltraCanvasFilerWidget filer("encoding-test-filer", 0, 0, 400, 300);
    CheckEqual(filer.DisplayNameOf(MakeEntry("Namens\xE4nderung", true)), kGerman,
               "Latin-1 folder name");
    CheckEqual(filer.DisplayNameOf(MakeEntry("Namens\x84nderung", true)), kGerman,
               "IBM437 folder name");
    CheckEqual(filer.DisplayNameOf(MakeEntry(kThai + ".txt")), kThai + ".txt", "Thai");
    CheckEqual(filer.DisplayNameOf(MakeEntry(kRussian + ".doc")), kRussian + ".doc",
               "Russian");
    CheckEqual(filer.DisplayNameOf(MakeEntry(kChinese + ".pdf")), kChinese + ".pdf",
               "Chinese");
    filer.SetFileExtensionsInNames(false);
    CheckEqual(filer.DisplayNameOf(MakeEntry(kChinese + ".pdf")), kChinese,
               "Chinese without its extension");
    CheckEqual(filer.DisplayNameOf(MakeEntry("Gr\xFC\xDF" "e.txt")),
               "Gr\xC3\xBC\xC3\x9F" "e", "Latin-1 name without its extension");
    filer.SetFileExtensionsInNames(true);

    std::cout << "\n-- Captions never cut a character --\n";
    for (const std::string& name : {kGerman, kUmlauts, kThai + kThai, kRussian,
                                    kChinese + kChinese + kChinese, kEmoji}) {
        TextWrapping::Options o;
        o.lineWidth = 40;          // five code points per line
        o.maxLines = 3;
        bool truncated = false;
        const std::vector<std::string> lines =
                TextWrapping::Wrap(CodePointMeasure{}, name, o, &truncated);
        bool linesValid = !lines.empty();
        for (const std::string& l : lines) linesValid = linesValid && IsWellFormedUtf8(l);
        Check(linesValid, "wrapped lines are whole characters: \"" + name + "\"");
        const std::string cut = TextWrapping::Ellipsize(CodePointMeasure{}, name, 40);
        Check(IsWellFormedUtf8(cut), "ellipsized is whole characters: \"" + cut + "\"");
    }

    std::cout << "\n-- A folder of such names, scanned from disk --\n";
    const fs::path dir = fs::temp_directory_path() / "uc-filer-name-encoding-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::vector<std::string> names = {kGerman, kUmlauts + ".txt", kThai + ".txt",
                                            kRussian + ".txt", kChinese + ".txt",
                                            kEmoji + ".jpg"};
    for (const std::string& name : names) {
        std::ofstream(PathFromUtf8(PathToUtf8(dir) + "/" + name)) << "x";
    }
#if !defined(_WIN32)
    // A name that is not UTF-8: only POSIX file systems can hold one.
    const std::string latin1 = "Alte Namens\xE4nderung.txt";
    std::ofstream(dir / latin1) << "x";
#endif

    filer.SetPath(PathToUtf8(dir));
    for (const std::string& name : names) {
        Check(FindByDisplayName(filer, name) != nullptr, "listed and drawn: \"" + name + "\"");
    }
#if !defined(_WIN32)
    const std::string latin1Shown = "Alte " + kGerman + ".txt";
    const FilerEntry* legacy = FindByDisplayName(filer, latin1Shown);
    Check(legacy != nullptr, "the Latin-1 name draws as \"" + latin1Shown + "\"");
    if (legacy) {
        Check(legacy->name == latin1, "and keeps its real bytes as its name");
        Check(fs::exists(fs::path(legacy->path)), "and its path still reaches the file");
    }
#endif
    fs::remove_all(dir, ec);
    std::cout << "\n(folder removed; the widget is torn down on return)\n";

    std::cout << "\n" << (g_failures ? "FAILED" : "ALL PASSED") << " ("
              << g_failures << " failure" << (g_failures == 1 ? "" : "s") << ")\n";
    return g_failures ? 1 : 0;
}
