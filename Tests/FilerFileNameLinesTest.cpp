// Tests/FilerFileNameLinesTest.cpp
// Display > File names: how many lines a name may take under a thumbnail
// tile - the full name, or at most 2, 3 or 4 rows. Three places carry it:
//   * the caption wrapping (UltraCanvasTextWrapping.h), where "full name" is
//     a limit nothing reaches, so a long name is never cut and the lines it
//     needs are counted as they are;
//   * the file display (UltraCanvasFilerWidget::SetCaptionMaxLines), where 0
//     is that "no limit" and a negative value cannot sneak in;
//   * UltraFiler's config file (Apps/UltraFiler/UltraFilerSettings.h), read
//     from a temporary folder here: 2 rows on a first start (the caption
//     every earlier release drew), each choice across a restart, and a hand
//     edit outside 2..4 pulled into the range rather than dropped.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextWrapping.h"
#include "UltraFilerSettings.h"        // Apps/UltraFiler

#include <cstdlib>   // setenv
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
// Declared rather than taken from <windows.h>, for the reason
// UltraCanvasPathUtf8.h gives for GetEnvironmentVariableW.
extern "C" __declspec(dllimport) int __stdcall SetEnvironmentVariableW(
        const wchar_t* lpName, const wchar_t* lpValue);
#endif

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// Ten pixels per code point: a stand-in font, so the wrapping is checked
// without a render context.
struct CodePointMeasure {
    int operator()(const std::string& s) const {
        int n = 0;
        for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
        return n * 10;
    }
};

void RedirectConfigDirectory(const std::filesystem::path& root) {
#if defined(_WIN32) || defined(_WIN64)
    SetEnvironmentVariableW(L"APPDATA", root.c_str());
#elif defined(__APPLE__)
    setenv("HOME", PathToUtf8(root).c_str(), 1);
#else
    setenv("XDG_CONFIG_HOME", PathToUtf8(root).c_str(), 1);
#endif
}

void WriteConfig(const std::string& lines) {
    std::error_code ec;
    std::filesystem::create_directories(
            PathFromUtf8(UltraFilerSettings::GetConfigDirectory()), ec);
    std::ofstream file(PathFromUtf8(UltraFilerSettings::GetConfigPath()),
                       std::ios::trunc);
    file << "# UltraFiler Configuration\n\n" << lines;
}

int LoadedLines() {
    UltraFilerSettings settings;
    settings.Load();
    return settings.fileNameMaxLines;
}

// ===== THE WRAPPING =====
void TestWrapping() {
    std::cout << "\n-- the caption wrapping --\n";
    const std::string name =
            "IHK Berlin Rechnung Mitgliedsbeitrag 2023 Berlin.pdf";  // 52 chars
    TextWrapping::Options o;
    o.lineWidth = 120;                 // 12 characters a line

    o.maxLines = 2;
    bool cut = false;
    std::vector<std::string> two = TextWrapping::Wrap(CodePointMeasure{}, name, o, &cut);
    Check(two.size() == 2 && cut, "2 rows: the name is cut");
    Check(!two.empty() && two.back().rfind("…", 0) == 0,
          "2 rows: the last row opens with the ellipsis");
    Check(!two.empty() && two.back().size() >= 4 &&
                  two.back().compare(two.back().size() - 4, 4, ".pdf") == 0,
          "2 rows: the extension stays readable");

    o.maxLines = 4;
    std::vector<std::string> four = TextWrapping::Wrap(CodePointMeasure{}, name, o, &cut);
    Check(four.size() == 4, "4 rows: four rows are used");

    o.maxLines = std::numeric_limits<int>::max();   // the full name
    std::vector<std::string> full = TextWrapping::Wrap(CodePointMeasure{}, name, o, &cut);
    std::string joined;
    for (const std::string& ln : full) joined += ln;
    Check(!cut, "full name: nothing is dropped");
    Check(full.size() >= 5, "full name: as many rows as the name needs");
    Check(joined.find("…") == std::string::npos, "full name: no ellipsis");
    Check(TextWrapping::LineCount(CodePointMeasure{}, name, o) ==
                  static_cast<int>(full.size()),
          "full name: the layout counts the rows the drawing uses");
    Check(TextWrapping::LineCount(CodePointMeasure{}, "a.txt", o) == 1,
          "full name: a short name keeps one row");
}

// ===== THE FILE DISPLAY =====
void TestWidget() {
    std::cout << "\n-- the file display --\n";
    UltraCanvasFilerWidget filer("test-filer", 0, 0, 400, 300);
    Check(filer.GetCaptionMaxLines() == 2, "2 rows by default");
    filer.SetCaptionMaxLines(4);
    Check(filer.GetCaptionMaxLines() == 4, "4 rows is taken");
    Check(filer.GetStyle().captionMaxLines == 4, "the style carries it");
    filer.SetCaptionMaxLines(0);
    Check(filer.GetCaptionMaxLines() == 0, "0 = the full name");
    filer.SetCaptionMaxLines(-3);
    Check(filer.GetCaptionMaxLines() == 0, "a negative count is no limit too");
}

// ===== ULTRAFILER'S SETTING =====
void TestSetting() {
    std::cout << "\n-- UltraFiler's saved choice --\n";
    std::error_code ec;
    const std::filesystem::path root =
            std::filesystem::temp_directory_path(ec) / "ultrafiler-file-names-test";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    if (ec) {
        Check(false, "a temporary config directory can be made");
        return;
    }
    RedirectConfigDirectory(root);

    Check(UltraFilerSettings().fileNameMaxLines == 2, "a fresh installation shows 2 rows");
    Check(LoadedLines() == 2, "no config file: 2 rows");

    for (int lines : {UltraFilerSettings::kFileNameLinesFull, 2, 3, 4}) {
        UltraFilerSettings settings;
        settings.fileNameMaxLines = lines;
        Check(settings.Save(), "the choice is saved with the other settings");
        Check(LoadedLines() == lines,
              "comes back after a restart: " + std::to_string(lines));
    }

    WriteConfig("display.file.names.lines = full\n");
    Check(LoadedLines() == UltraFilerSettings::kFileNameLinesFull, "\"full\" is the full name");
    WriteConfig("display.file.names.lines = 9\n");
    Check(LoadedLines() == UltraFilerSettings::kMaxFileNameLines,
          "a count past 4 is held at 4");
    WriteConfig("display.file.names.lines = 1\n");
    Check(LoadedLines() == UltraFilerSettings::kMinFileNameLines,
          "a count under 2 is raised to 2");
    WriteConfig("display.file.names.lines = lots\n");
    Check(LoadedLines() == 2, "an unreadable value keeps the default");

    std::filesystem::remove_all(root, ec);
}

}  // namespace

int main() {
    std::cout << "=== Filer file name rows ===\n";
    TestWrapping();
    TestWidget();
    TestSetting();
    std::cout << "\n" << (g_failures == 0 ? "All file name tests passed."
                                          : "SOME FILE NAME TESTS FAILED.")
              << "\n";
    return g_failures == 0 ? 0 : 1;
}
