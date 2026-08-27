// Tests/TextWrapTest.cpp
// Unit tests for UltraCanvasTextWrapping.h — the caption wrapper behind the
// Filer widget's tile names. Covers the "Logo CoderBox with text.png"
// report: the name was broken inside the word ("Logo CoderBo" / "x with
// text.png"), a break that bought the first line a single character.
// The wrapper measures text through a callable, so the test supplies a
// synthetic proportional font instead of a render context: no display, no
// framework link, and the widths are stable across platforms.
// Version: 1.0.0
// Last Modified: 2026-08-27
// Author: UltraCanvas Framework

#include "UltraCanvasTextWrapping.h"

#include <cstdio>
#include <string>
#include <vector>

namespace TW = UltraCanvas::TextWrapping;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

namespace {

// ===== SYNTHETIC FONT =====
// A proportional font in the spirit of the UI faces the Filer draws with:
// "i" and "." are narrow, "m" and "W" are wide. It is what makes the reported
// case reproducible — "x with text.png" (15 characters) is *narrower* than
// "Logo CoderBox" (13), which is exactly why the greedy break landed inside
// the word.
int GlyphWidth(char c) {
    switch (c) {
        case ' ': case '.': case ',': case ':': case ';':
        case '\'': case '!': case '|': case 'i': case 'j': case 'l':
            return 3;
        case 'f': case 'r': case 't':
            return 4;
        case 'm': case 'w':
            return 9;
        case 'M': case 'W':
            return 11;
        default:
            return (c >= 'A' && c <= 'Z') ? 8 : 6;
    }
}

// Multibyte code points are one 6 px glyph, not one per byte.
int Measure(const std::string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        if ((b & 0xC0) == 0x80) continue;           // continuation byte
        w += (b < 0x80) ? GlyphWidth(s[i]) : 6;
    }
    return w;
}

std::string Join(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += " | ";
        out += lines[i];
    }
    return out;
}

bool Same(const std::vector<std::string>& got,
          const std::vector<std::string>& want, const char* what) {
    ++checks;
    if (got == want) return true;
    ++failures;
    std::printf("FAIL %s\n  got : %s\n  want: %s\n", what,
                Join(got).c_str(), Join(want).c_str());
    return false;
}

TW::Options CaptionOptions(int width, int maxLines = 2) {
    TW::Options o;
    o.lineWidth = width;
    o.maxLines = maxLines;
    o.breakTolerance = 3;
    o.overflowSlack = 5;        // what UltraCanvasFilerWidget derives at 11 px
    return o;
}

// ===== THE REPORTED CASE =====
// A tile just too narrow for "Logo CoderBox": the last "x" of the word does
// not fit by four pixels, and the line used to take it apart for that.
void TestWordIsNotSplitForOneCharacter() {
    const std::string name = "Logo CoderBox with text.png";
    const int width = Measure("x with text.png");        // 75 px
    CHECK(Measure("Logo CoderBo") <= width);
    CHECK(Measure("Logo CoderBox") > width);             // 79 px: 4 px over

    bool truncated = true;
    std::vector<std::string> lines =
            TW::Wrap(Measure, name, CaptionOptions(width), &truncated);
    Same(lines, {"Logo CoderBox", "with text.png"},
         "the word is kept whole inside the slack");
    CHECK(!truncated);
    // The caption band is unchanged: still two lines, as before the fix.
    CHECK(TW::LineCount(Measure, name, CaptionOptions(width)) == 2);
}

// The same name with no slack to lend: keeping the word whole would push
// "CoderBox" off the caption entirely, so the mid-word break comes back —
// characters of the name are worth more than a tidy break.
void TestContentWinsOverTheWordBreak() {
    const std::string name = "Logo CoderBox with text.png";
    TW::Options o = CaptionOptions(Measure("x with text.png"));
    o.overflowSlack = 0;

    bool truncated = true;
    std::vector<std::string> lines = TW::Wrap(Measure, name, o, &truncated);
    Same(lines, {"Logo CoderBo", "x with text.png"},
         "a name that would otherwise be cut short still splits the word");
    CHECK(!truncated);
}

// A break that would leave a stub of up to three characters is not taken when
// the word can move down instead — the old rule only backed off to a separator
// sitting in the back half of the line, which "Logo " is not.
void TestShortStubsAreNeverLeftBehind() {
    const std::string name = "Logo CoderBox.png";
    TW::Options o = CaptionOptions(Measure("Logo CoderBo"), 2);
    o.overflowSlack = 0;                      // no room to pull the "x" up

    std::vector<std::string> lines = TW::WrapGreedy(Measure, name, o, nullptr);
    Same(lines, {"Logo", "CoderBox.png"},
         "the word moves down rather than losing its last character");

    // The same wrap with the tolerance switched off is the reported break.
    o.breakTolerance = 0;
    lines = TW::WrapGreedy(Measure, name, o, nullptr);
    Same(lines, {"Logo CoderBo", "x.png"},
         "tolerance 0 keeps the exact-fit break");
}

// One long word with no separator to fall back on: the break moves left inside
// the word so the next line opens with something readable.
void TestNoOrphanCharacterInAWordOnlyName() {
    const std::string name = "abcdefghijklmnop";       // 16 x 6 px
    TW::Options o = CaptionOptions(Measure("abcdefghijklmno"), 2);
    o.overflowSlack = 0;                      // no room to pull the "p" up

    std::vector<std::string> lines = TW::WrapGreedy(Measure, name, o, nullptr);
    CHECK(lines.size() == 2);
    if (lines.size() == 2) {
        CHECK(lines[0].size() + lines[1].size() == name.size());
        CHECK(lines[1].size() > 3);           // never a single trailing letter
    }
}

// A caption too narrow for the name in either mode keeps whichever break
// shows the most of it: a tidy break is never worth dropped characters.
void TestTruncatedNameKeepsTheMostText() {
    const std::string name = "Logo CoderBox with text.png";
    TW::Options o = CaptionOptions(70, 2);        // too narrow for the name
    TW::Options relaxed = o;
    relaxed.breakTolerance = 0;

    bool truncated = false;
    std::vector<std::string> lines = TW::Wrap(Measure, name, o, &truncated);
    CHECK(truncated);
    CHECK(TW::RetainedCount(lines) >=
          TW::RetainedCount(TW::Wrap(Measure, name, relaxed, nullptr)));
}

// ===== UNCHANGED BEHAVIOUR =====
// Balancing still evens the lines out instead of front-loading the first.
void TestLinesStayBalanced() {
    const std::string name = "CoderBox compiler.png";
    std::vector<std::string> lines =
            TW::Wrap(Measure, name, CaptionOptions(Measure(name) - 6), nullptr);
    Same(lines, {"CoderBox", "compiler.png"}, "balanced break");
}

// A name too long even for its lines keeps its tail: the front of the last
// line is dropped behind an "…" so the extension stays readable.
void TestOverlongNameKeepsItsExtension() {
    const std::string name =
            "A very long holiday photo taken in Rome in 2024 with friends.jpeg";
    bool truncated = false;
    std::vector<std::string> lines =
            TW::Wrap(Measure, name, CaptionOptions(90), &truncated);
    CHECK(truncated);
    CHECK(lines.size() == 2);
    if (lines.size() == 2) {
        CHECK(lines[1].rfind("…", 0) == 0);
        CHECK(lines[1].size() >= 5 &&
              lines[1].compare(lines[1].size() - 5, 5, ".jpeg") == 0);
    }
}

// A name that fits stays one line; one that fits only within the slack does
// too, rather than opening a second line for four pixels.
void TestSlackKeepsAShortOverflowOnOneLine() {
    const std::string name = "Notes.txt";
    Same(TW::Wrap(Measure, name, CaptionOptions(Measure(name)), nullptr),
         {name}, "a name that fits is one line");
    Same(TW::Wrap(Measure, name, CaptionOptions(Measure(name) - 4), nullptr),
         {name}, "a four-pixel overflow stays on one line");
    CHECK(TW::LineCount(Measure, name, CaptionOptions(Measure(name) - 4)) == 1);
}

// Breaks never land inside a multi-byte code point.
void TestUtf8IsNeverSplit() {
    const std::string name = "Ölçüm über größe δοκιμή.txt";
    for (int width = 20; width <= 200; width += 7) {
        std::vector<std::string> lines =
                TW::Wrap(Measure, name, CaptionOptions(width, 3), nullptr);
        for (const std::string& ln : lines) {
            ++checks;
            const unsigned char first = static_cast<unsigned char>(ln[0]);
            const bool valid = ln.empty() || (first & 0xC0) != 0x80;
            if (!valid) {
                ++failures;
                std::printf("FAIL width %d: line starts mid code point: %s\n",
                            width, ln.c_str());
            }
        }
    }
}

// Whatever the width, a wrapped name never loses characters unless it was
// reported as truncated, and no line runs further past its width than the
// slack allows.
void TestWidthSweepInvariants() {
    const char* names[] = {
        "Logo CoderBox with text.png",
        "UltraCanvas-Filer_Widget (final).tar.gz",
        "IMG_20240817_121314.HEIC",
        "a.b",
        "singleverylongwordwithoutanyseparators.bin",
    };
    for (const char* raw : names) {
        const std::string name = raw;
        for (int width = 24; width <= Measure(name) + 20; width += 5) {
            TW::Options o = CaptionOptions(width, 3);
            bool truncated = false;
            std::vector<std::string> lines =
                    TW::Wrap(Measure, name, o, &truncated);
            ++checks;
            if (lines.empty() || static_cast<int>(lines.size()) > o.maxLines) {
                ++failures;
                std::printf("FAIL %s at %d: %zu lines\n", raw, width, lines.size());
                continue;
            }
            std::string joined;
            for (const std::string& ln : lines) {
                joined += ln;
                ++checks;
                if (Measure(ln) > width + o.overflowSlack) {
                    ++failures;
                    std::printf("FAIL %s at %d: line over width: %s\n",
                                raw, width, ln.c_str());
                }
            }
            if (!truncated) {
                // Only the spaces at the break points are dropped.
                std::string want, got;
                for (char c : name) if (c != ' ') want += c;
                for (char c : joined) if (c != ' ') got += c;
                ++checks;
                if (want != got) {
                    ++failures;
                    std::printf("FAIL %s at %d: lost text: %s\n",
                                raw, width, Join(lines).c_str());
                }
            }
        }
    }
}

// ===== SINGLE LINE =====
void TestEllipsizeAndTruncate() {
    const std::string name = "Logo CoderBox with text.png";
    const std::string cut = TW::Ellipsize(Measure, name, 60);
    CHECK(Measure(cut) <= 60);
    CHECK(cut.size() >= 3 && cut.compare(cut.size() - 3, 3, "…") == 0);
    CHECK(TW::Ellipsize(Measure, name, Measure(name)) == name);

    const std::string plain = TW::Truncate(Measure, name, 60);
    CHECK(Measure(plain) <= 60);
    CHECK(plain.find("…") == std::string::npos);
    CHECK(name.rfind(plain, 0) == 0);
}

} // namespace

int main() {
    TestWordIsNotSplitForOneCharacter();
    TestContentWinsOverTheWordBreak();
    TestShortStubsAreNeverLeftBehind();
    TestNoOrphanCharacterInAWordOnlyName();
    TestTruncatedNameKeepsTheMostText();
    TestLinesStayBalanced();
    TestOverlongNameKeepsItsExtension();
    TestSlackKeepsAShortOverflowOnOneLine();
    TestUtf8IsNeverSplit();
    TestWidthSweepInvariants();
    TestEllipsizeAndTruncate();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
