// Tests/Utf8FindAllTest.cpp
// utf8_find_all must return exactly what a utf8_find loop returns - every
// non-overlapping match, as codepoint positions - and do it in linear time;
// utf8_replace_all must produce what replacing one match at a time in place
// produced (the old Replace All), also in linear time.
//
// The text area's search highlighting used to be that loop. Each utf8_find call
// walks the haystack from its start, and the case-insensitive one lowercases a
// copy of the whole haystack first, so collecting N matches cost N full passes:
// typing one letter into UltraTexter's search bar with a ~1 MB file open hung
// the app. The timing check below fails if the scan goes quadratic again.
// Version: 1.1.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework

#include "UltraCanvasUtilsUtf8.h"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

static void Check(bool condition, const std::string& name) {
    testCount++;
    if (!condition) {
        failCount++;
        std::cout << "FAIL: " << name << std::endl;
    }
}

// The behaviour utf8_find_all replaces, kept as the reference.
static std::vector<int> FindLoop(const std::string& haystack, const std::string& needle,
                                 bool caseSensitive) {
    std::vector<int> result;
    int len = utf8_length(needle);
    int pos = 0;
    while ((pos = utf8_find(haystack, needle, pos, caseSensitive)) >= 0) {
        result.push_back(pos);
        pos += len;
    }
    return result;
}

// The old Replace All in UltraCanvasTextArea::ReplaceText, kept as the reference.
static std::string ReplaceLoop(std::string text, const std::string& needle,
                               const std::string& rep, bool caseSensitive) {
    int needleLen = utf8_length(needle);
    int repLen = utf8_length(rep);
    int pos = 0;
    while ((pos = utf8_find(text, needle, pos, caseSensitive)) >= 0) {
        utf8_replace(text, pos, needleLen, rep);
        pos += repLen;
    }
    return text;
}

static double MsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

int main() {
    const std::string text = "Ärger äRGER ä aa aaa — Straße STRASSE αβγ ΑΒΓ x";
    const std::vector<std::string> needles = {"a", "aa", "ä", "Ä", "Αβ", "—", "x", "zz", "STRA"};
    for (const auto& needle : needles) {
        for (bool cs : {true, false}) {
            Check(utf8_find_all(text, needle, cs) == FindLoop(text, needle, cs),
                  "matches loop: '" + needle + "' cs=" + (cs ? "1" : "0"));
        }
    }

    Check(utf8_find_all(text, "", false).empty(), "empty needle finds nothing");
    Check(utf8_find_all("", "a", false).empty(), "empty haystack finds nothing");
    Check(utf8_find_all("aaaa", "aa", true) == std::vector<int>({0, 2}), "matches do not overlap");
    Check(utf8_find_all("ÄxÄx", "äX", false) == std::vector<int>({0, 2}),
          "case-insensitive positions are codepoints");

    const std::vector<std::string> replacements = {"", "b", "ä", "xx", "Αβγ—"};
    for (const auto& needle : needles) {
        for (const auto& rep : replacements) {
            for (bool cs : {true, false}) {
                Check(utf8_replace_all(text, needle, rep, cs) == ReplaceLoop(text, needle, rep, cs),
                      "replace matches loop: '" + needle + "' -> '" + rep + "' cs=" + (cs ? "1" : "0"));
            }
        }
    }
    Check(utf8_replace_all("aaaa", "aa", "a", true) == "aa", "replacement is not searched again");
    Check(utf8_replace_all("abc", "zz", "y", true) == "abc", "no match leaves text unchanged");
    Check(utf8_replace_all("ÄxÄx", "äX", "-", false) == "--", "case-insensitive replace");

    // ~1 MB, ~100k matches. The old loop needs minutes here; one pass is
    // milliseconds, so a second is a wide margin for a slow CI runner.
    std::string big;
    while (big.size() < 1000000) big += "Ünïcode line with some text, e and E.\n";
    for (bool cs : {true, false}) {
        auto start = std::chrono::steady_clock::now();
        std::vector<int> matches = utf8_find_all(big, "e", cs);
        double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        Check(!matches.empty(), std::string("large document has matches cs=") + (cs ? "1" : "0"));
        Check(ms < 1000.0, std::string("large document scanned in linear time cs=") + (cs ? "1" : "0")
              + " (" + std::to_string(ms) + " ms)");
    }

    for (bool cs : {true, false}) {
        auto start = std::chrono::steady_clock::now();
        std::string replaced = utf8_replace_all(big, "e", "éé", cs);
        double ms = MsSince(start);
        Check(replaced.size() > big.size(), std::string("large replace changed text cs=") + (cs ? "1" : "0"));
        Check(ms < 1000.0, std::string("large replace in linear time cs=") + (cs ? "1" : "0")
              + " (" + std::to_string(ms) + " ms)");
    }

    std::cout << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
