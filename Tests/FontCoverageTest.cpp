// Tests/FontCoverageTest.cpp
// UltraCanvasFontCoverage.h: the characters of a text no font covers, by
// script, with a font to get for each.
//
// UltraCanvasRichTextEdit names them in a notice when the computer has no
// font for a document's script, rather than leaving boxes of hex digits that
// look like a bug in the editor. Runs headless: the text engine is asked
// without a window. A character from the last private-use plane (U+10FFFD) stands for a
// script no font covers - no font assigns it a glyph.
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasFontCoverage.h"

#include <cctype>

#include <iostream>
#include <string>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

int main() {
    const std::string uncovered = "\xF4\x8F\xBF\xBD";   // U+10FFFD

    TEST("an empty text needs no font", CountCharactersWithoutFont("") == 0);
    TEST("Latin letters have a font", CountCharactersWithoutFont("Hello, world") == 0);
    TEST("one character without a font is counted once",
         CountCharactersWithoutFont("ab" + uncovered + "cd") == 1);
    TEST("each character without a font is counted",
         CountCharactersWithoutFont(uncovered + " " + uncovered + uncovered) == 3);
    TEST("a family that is not installed falls back like any text",
         CountCharactersWithoutFont("Hello", "No Such Family 12345") == 0);

    TEST("no scripts are missing from covered text", FindScriptsWithoutFont("Hello, world").empty());
    const std::vector<MissingScriptFont> scripts = FindScriptsWithoutFont("ab" + uncovered + uncovered + "cd");
    TEST("the uncovered characters are one script", scripts.size() == 1);
    if (!scripts.empty()) {
        TEST("...counted as two characters", scripts[0].characters == 2);
        TEST("...named by its ISO 15924 code when it has no name", scripts[0].isoCode == "Zzzz" &&
                                                                  scripts[0].script == "Zzzz");
        TEST("...with a font to get and where", !scripts[0].suggestedFont.empty() &&
             scripts[0].downloadUrl.rfind("https://fonts.google.com/", 0) == 0);
    }
    TEST("a family's download page is a search for it",
         FontDownloadUrl("Noto Sans SC") == "https://fonts.google.com/?query=Noto+Sans+SC");
    TEST("...with its name escaped", FontDownloadUrl("A&B") == "https://fonts.google.com/?query=A%26B");
    TEST("no family is always available", IsFontFamilyAvailable(""));
    TEST("a family nobody has is not", !IsFontFamilyAvailable("No Such Family 12345"));

    // A family another font stands in for by name (Helvetica in Arial on
    // Windows, in Liberation Sans on most Linux systems).
    TEST("a family nobody has has no stand-in", FontFamilySubstitute("No Such Family 12345").empty());
    TEST("no family needs none", FontFamilySubstitute("").empty());
    for (const FontFamilySubstitutes& known : GetFontFamilySubstitutes()) {
        std::string expected;
        if (!IsFontFamilyAvailable(known.family)) {
            for (const char* substitute : known.substitutes) {
                if (IsFontFamilyAvailable(substitute)) { expected = substitute; break; }
            }
        }
        const std::string found = FontFamilySubstitute(known.family);
        std::cerr << "  " << known.family << ": " << (IsFontFamilyAvailable(known.family) ? "installed"
                     : found.empty() ? "no stand-in installed" : "drawn in " + found) << std::endl;
        TEST(std::string(known.family) + " has its first installed stand-in", found == expected);
        std::string lowered = known.family;
        for (char& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        TEST("...whatever its case", FontFamilySubstitute(lowered) == expected);
    }
    const bool helveticaDrawn = IsFontFamilyAvailable("Helvetica") || !FontFamilySubstitute("Helvetica").empty();
    const bool hasStandIn = IsFontFamilyAvailable("Arial") || IsFontFamilyAvailable("Liberation Sans");
    TEST("Helvetica is drawn where Arial or Liberation Sans is installed", !hasStandIn || helveticaDrawn);

    std::cerr << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
