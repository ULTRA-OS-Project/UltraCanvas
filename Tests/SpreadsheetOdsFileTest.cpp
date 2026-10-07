// Tests/SpreadsheetOdsFileTest.cpp
// UltraCanvasSpreadsheet::SaveODS / LoadODS on a file whose name is Thai and
// an emoji.
//
// The rule this guards: the ODS reader opens the package by its UTF-8 name
// (OpenFileUtf8) and hands the open file to miniz, never the name - miniz's
// own fopen converts a UTF-8 name only under MSVC and 64-bit MinGW, and there
// opens with _wfopen_s, which denies other programs write access while the
// file is read. So the saved sheet must load back from that name, the loader
// must let go of the file when it is done (on Windows a held file cannot be
// deleted), and a missing file or a file that is no spreadsheet must say
// which it is.
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSpreadsheet.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

} // namespace

int main() {
    std::cout << "=== ODS on a Thai-and-emoji path ===\n";
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() /
        PathFromUtf8("\xE0\xB8\x95\xE0\xB8\xB2\xE0\xB8\xA3\xE0\xB8\xB2\xE0\xB8\x87 "
                     "\xF0\x9F\x93\x8A ods-test");
    fs::create_directories(dir, ec);
    const std::string sheetPath = PathToUtf8(
        dir / PathFromUtf8("\xE0\xB8\x87\xE0\xB8\x9A "
                           "\xF0\x9F\x93\x88.ods"));

    {
        UltraCanvasSpreadsheet sheet("Writer", 0, 0, 400, 300);
        sheet.SetCellValue(0, 0, std::string("Umsatz"));
        sheet.SetCellValue(1, 0, std::string("\xE0\xB8\x82\xE0\xB8\xB2\xE0\xB8\xA2"));
        Check(sheet.SaveODS(sheetPath), "the sheet saves under its UTF-8 name");
    }
    Check(fs::exists(PathFromUtf8(sheetPath), ec), "the file is on disk under that name");

    {
        UltraCanvasSpreadsheet sheet("Reader", 0, 0, 400, 300);
        const bool loaded = sheet.LoadODS(sheetPath);
        Check(loaded, "the sheet loads back from that name");
        if (!loaded) std::cout << "         " << sheet.GetLastError() << "\n";
        Check(sheet.GetCellText(0, 0) == "Umsatz", "a Latin cell reads back");
        Check(sheet.GetCellText(1, 0) == "\xE0\xB8\x82\xE0\xB8\xB2\xE0\xB8\xA2",
              "a Thai cell reads back");
    }
    Check(fs::remove(PathFromUtf8(sheetPath), ec),
          "the file can be deleted after loading (the loader let it go)");

    std::cout << "\n=== What does not load ===\n";
    {
        UltraCanvasSpreadsheet sheet("Missing", 0, 0, 400, 300);
        Check(!sheet.LoadODS(PathToUtf8(dir / "missing.ods")), "a missing file does not load");
        Check(!sheet.GetLastError().empty(), "and says why");
    }
    const std::string junk = PathToUtf8(dir / "junk.ods");
    if (std::FILE* f = OpenFileUtf8(junk, "wb")) {
        std::fputs("PK but not an archive", f);
        std::fclose(f);
    }
    {
        UltraCanvasSpreadsheet sheet("Junk", 0, 0, 400, 300);
        Check(!sheet.LoadODS(junk), "a file that is not a ZIP does not load");
        Check(sheet.GetLastError().find("not a valid OpenDocument spreadsheet") !=
                  std::string::npos,
              "and says it is not a spreadsheet");
    }
    Check(fs::remove(PathFromUtf8(junk), ec), "and is not held open after the failure");

    fs::remove_all(dir, ec);
    std::cout << "\n"
              << (g_failures == 0 ? "All checks passed\n"
                                  : std::to_string(g_failures) + " check(s) FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
