// Tests/FilerSourceTextFormatsTest.cpp
// Every source-text type the syntax highlighter knows is a Text format of the
// file display: its tile shows a miniature page of its text, and Display >
// Thumbnails > Text lists a switch for it.
//
// The widget's own table named 19 text types. Swift, Rust, SQL, Go, Kotlin,
// Java, PHP, Lua, Ruby, C#, CSS, Pascal and the assemblers were "Other": a
// blank sheet on the tile, and no switch anywhere to ask for more. This test
// checks that GetPreviewableFormats() - what the Thumbnails / Detail view
// settings pages list - carries them as Text, that the binary formats riding
// along in a language's list (MATLAB .mat / .mlx, gzip .svgz) stay out of
// Text, that the table and the registered plugins still win (.svg stays a
// vector graphic), and that a scanned source file gets the Text category and
// its language's name. R, Scala, MATLAB and VBA, switched on in the
// highlighter since, are Text too - but .bas stays BASIC, not VBA.
// Version: 1.1.0
// Last Modified: 2026-09-25
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

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
    std::cout << "===== Filer: source text formats =====\n";

    std::map<std::string, FilerFormatInfo> formats;
    for (const FilerFormatInfo& f : UltraCanvasFilerWidget::GetPreviewableFormats())
        formats.emplace(f.extension, f);
    auto kindOf = [&formats](const std::string& ext) {
        auto it = formats.find(ext);
        return it == formats.end() ? FilerPreviewType::NonePreview : it->second.kind;
    };

    std::cout << "\n-- Listed as Text, with a thumbnail --\n";
    for (const char* ext : {"swift", "sql", "rs", "rb", "php", "lua", "kt", "java",
                            "go", "cs", "css", "pas", "asm", "arm", "68k", "dart",
                            "pl", "f90", "glsl", "z80", "tsx", "mjs",
                            "r", "rmd", "scala", "sc", "sbt", "m", "vba", "cls", "frm"}) {
        auto it = formats.find(ext);
        const bool ok = it != formats.end() && it->second.kind == FilerPreviewType::Text &&
                        it->second.thumbnailSupported;
        Check(ok, std::string(ext) + " is a Text format with a thumbnail" +
                  (ok ? std::string()
                      : it == formats.end() ? std::string(" (not listed)")
                      : " (kind " + std::to_string(static_cast<uint32_t>(it->second.kind)) +
                        ", \"" + it->second.label + "\")"));
    }
    // The ones the widget's table always had stay as they were.
    for (const char* ext : {"txt", "json", "cpp", "py", "yaml", "sh", "js", "ts"})
        Check(kindOf(ext) == FilerPreviewType::Text, std::string(ext) + " is still Text");

    std::cout << "\n-- Kept out of Text --\n";
    for (const char* ext : {"mat", "mlx", "svgz"})
        Check(kindOf(ext) != FilerPreviewType::Text,
              std::string(ext) + " (binary) is not a Text format");
    Check(kindOf("svg") == FilerPreviewType::VectorGraphics,
          "svg stays a vector graphic (the table speaks first)");

    std::cout << "\n-- A scanned source file --\n";
    const fs::path dir = fs::temp_directory_path() / "uc-filer-source-text-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    std::ofstream(dir / "main.swift") << "import Foundation\nprint(\"hi\")\n";
    std::ofstream(dir / "query.sql") << "SELECT 1;\n";
    std::ofstream(dir / "stats.R") << "x <- c(1, 2, 3)\nmean(x)\n";
    std::ofstream(dir / "Module1.bas") << "10 PRINT \"HI\"\n";
    std::ofstream(dir / "Report.vba") << "Sub Main()\nEnd Sub\n";
    std::ofstream(dir / "solve.m") << "x = A \\ b;\n";
    UltraCanvasFilerWidget filer("source-text-filer", 0, 0, 400, 300);
    filer.SetPath(dir.string());
    for (const FilerEntry& e : filer.GetEntries()) {
        if (e.name == "main.swift") {
            Check(e.category == FilerFileCategory::Text, "main.swift is Text");
            Check(e.typeName == "Swift Text",
                  "and named after its language -> \"" + e.typeName + "\"");
        }
        if (e.name == "query.sql")
            Check(e.category == FilerFileCategory::Text, "query.sql is Text");
        if (e.name == "stats.R")
            Check(e.category == FilerFileCategory::Text && e.typeName == "R Text",
                  "stats.R (upper-case extension) is R Text -> \"" + e.typeName + "\"");
        if (e.name == "Module1.bas")
            Check(e.typeName == "BASIC Text",
                  "Module1.bas stays BASIC, not VBA -> \"" + e.typeName + "\"");
        if (e.name == "Report.vba")
            Check(e.category == FilerFileCategory::Text && e.typeName == "VBA Text",
                  "Report.vba is VBA Text -> \"" + e.typeName + "\"");
        if (e.name == "solve.m")
            Check(e.category == FilerFileCategory::Text && e.typeName == "MATLAB Text",
                  "solve.m is MATLAB Text -> \"" + e.typeName + "\"");
    }
    Check(filer.GetEntries().size() == 6, "all six files listed");
    fs::remove_all(dir, ec);

    std::cout << "\n" << (g_failures ? "FAILED" : "ALL PASSED") << " (" << g_failures
              << " failure" << (g_failures == 1 ? "" : "s") << ")\n";
    return g_failures ? 1 : 0;
}
