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
// highlighter since, are Text too - but .bas stays BASIC, not VBA. The shared
// extensions .cls (VBA or LaTeX), .m (MATLAB or Objective-C) and .pl (Perl or
// Prolog) are named after what the file's first lines say it is; without a
// clue, after the extension's default rather than whichever language the
// highlighter's unordered map listed first. Their switch under Text names both
// languages ("VBA / LaTeX").
// Version: 1.3.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasSyntaxTokenizer.h"

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

    std::cout << "\n-- A shared extension's switch names both languages --\n";
    for (const auto& [ext, label] : std::map<std::string, std::string>{
             {"cls", "VBA / LaTeX"}, {"m", "MATLAB / Objective-C"}, {"pl", "Perl / Prolog"}}) {
        auto it = formats.find(ext);
        const std::string got = it == formats.end() ? "(not listed)" : it->second.label;
        Check(got == label, ext + " is labelled \"" + label + "\" -> \"" + got + "\"");
    }

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
    // Shared extensions: the file's first lines name the language.
    std::ofstream(dir / "thesis.cls") << "\\NeedsTeXFormat{LaTeX2e}\n\\ProvidesClass{thesis}\n";
    std::ofstream(dir / "Invoice.cls") << "VERSION 1.0 CLASS\nBEGIN\n  MultiUse = -1\nEND\n"
                                          "Attribute VB_Name = \"Invoice\"\n";
    std::ofstream(dir / "AppDelegate.m") << "#import \"AppDelegate.h\"\n\n@implementation AppDelegate\n@end\n";
    std::ofstream(dir / "fit.m") << "% Fit a line\nfunction p = fit(x, y)\n";
    std::ofstream(dir / "family.pl") << "% Family tree\nparent(tom, bob).\n"
                                        "grandparent(X, Z) :- parent(X, Y), parent(Y, Z).\n";
    std::ofstream(dir / "report.pl") << "#!/usr/bin/perl\nuse strict;\nmy $n = 1;\n";
    std::ofstream(dir / "bare.pl") << "\n";   // no clue: the default, Perl
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
        const std::map<std::string, std::string> shared = {
            {"thesis.cls", "LaTeX Text"}, {"Invoice.cls", "VBA Text"},
            {"AppDelegate.m", "Objective-C Text"}, {"fit.m", "MATLAB Text"},
            {"family.pl", "Prolog Text"}, {"report.pl", "Perl Text"},
            {"bare.pl", "Perl Text"}};
        if (auto it = shared.find(e.name); it != shared.end())
            Check(e.category == FilerFileCategory::Text && e.typeName == it->second,
                  e.name + " reads as " + it->second + " -> \"" + e.typeName + "\"");
    }
    Check(filer.GetEntries().size() == 13, "all thirteen files listed");

    std::cout << "\n-- The extension alone picks the default --\n";
    for (const auto& [ext, want] : std::map<std::string, std::string>{
             {"pl", "Perl"}, {"cls", "VBA"}, {"m", "MATLAB"}}) {
        SyntaxTokenizer tokenizer;
        const bool set = tokenizer.SetLanguageByExtension(ext);
        const std::string got = tokenizer.GetCurrentProgrammingLanguage();
        Check(set && got == want, "." + ext + " -> " + got + " (want " + want + ")");
    }

    std::cout << "\n-- SyntaxTokenizer::LanguageFromContent --\n";
    struct Sniff { const char* ext; const char* text; const char* want; };
    for (const Sniff& t : std::initializer_list<Sniff>{
             {"cls", "% My class\n\\LoadClass{article}\n", "LaTeX"},
             {"cls", "\xEF\xBB\xBF\\ProvidesClass{x}\n", "LaTeX"},
             {"CLS", "Option Explicit\nPrivate m As Long\n", "VBA"},
             {"cls", "\n\n", ""},
             {".m", "\n  % comment\nx = 1;\n", "MATLAB"},
             {"m", "// main.m\n#include <stdio.h>\n", "Objective-C"},
             {"m", "@interface Foo : NSObject\n@end\n", "Objective-C"},
             {"m", "x = 1;\n", ""},
             {"pl", "% facts\nparent(tom, bob).\n", "Prolog"},
             {"pl", "ancestor(X, Y) :- parent(X, Y).\n", "Prolog"},
             {"pl", "/* rules */\n", "Prolog"},
             {"pl", "#!/usr/bin/env perl\nprint 1;\n", "Perl"},
             {"PL", "use strict;\nuse warnings;\n", "Perl"},
             {"pl", "=pod\n\nDocs\n", "Perl"},
             {"pl", "print \"hi\";\n", ""},
             {"vba", "\\ProvidesClass{x}\n", ""},
             {"r", "#import\n", ""}}) {
        const std::string got = SyntaxTokenizer::LanguageFromContent(t.ext, t.text);
        Check(got == t.want, std::string(".") + t.ext + " -> \"" + got + "\" (want \"" +
                             t.want + "\")");
    }
    fs::remove_all(dir, ec);

    std::cout << "\n" << (g_failures ? "FAILED" : "ALL PASSED") << " (" << g_failures
              << " failure" << (g_failures == 1 ? "" : "s") << ")\n";
    return g_failures ? 1 : 0;
}
