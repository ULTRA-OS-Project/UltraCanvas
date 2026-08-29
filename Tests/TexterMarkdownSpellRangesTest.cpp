// TexterMarkdownSpellRangesTest.cpp
// Test suite for UltraTexter's markdown "do not spell check this" scanner
// Version: 1.0.0
// Last Modified: 2026-08-28
// Author: UltraCanvas Framework
//
// The scanner is what keeps fenced code, link targets and math out of the
// squiggles in a markdown document. It has no framework or editor dependency,
// so the test builds from the one source file.

#include "UltraCanvasMarkdownSpellRanges.h"

#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                               \
    do {                                                                    \
        bool passed = (condition);                                          \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                           \
        testCount++;                                                        \
    } while (0)

namespace {

// True when every byte of `word` (its first occurrence in `text`) is skipped.
bool IsSkipped(const std::string& text, const std::string& word) {
    const size_t at = text.find(word);
    if (at == std::string::npos) {
        std::cerr << "  (test bug: \"" << word << "\" is not in the sample)" << std::endl;
        return false;
    }
    return SpanCoversRange(ScanMarkdownNoSpellRanges(text), at, word.size());
}

bool IsChecked(const std::string& text, const std::string& word) {
    const size_t at = text.find(word);
    if (at == std::string::npos) {
        std::cerr << "  (test bug: \"" << word << "\" is not in the sample)" << std::endl;
        return false;
    }
    return !SpanCoversRange(ScanMarkdownNoSpellRanges(text), at, word.size());
}

bool SpansAreSortedAndDisjoint(const std::vector<TextByteSpan>& spans) {
    for (size_t i = 1; i < spans.size(); ++i) {
        if (spans[i].startByte < spans[i - 1].EndByte()) return false;
    }
    return true;
}

} // namespace

int main() {
    std::cerr << "=== UltraTexter markdown spell-range scanner ===" << std::endl;

    // ----- fenced code blocks -----
    std::cerr << "\n--- fenced code blocks ---" << std::endl;
    {
        const std::string text =
            "Prose befor the fence.\n"
            "```cpp\n"
            "int mispeled = 0;\n"
            "```\n"
            "Prose aftre the fence.\n";

        TEST("prose before a fence is checked", IsChecked(text, "befor"));
        TEST("prose after a fence is checked", IsChecked(text, "aftre"));
        TEST("fenced code is skipped", IsSkipped(text, "mispeled"));
        TEST("the info string is skipped", IsSkipped(text, "cpp"));
    }
    {
        // Tilde fences, and a backtick run inside a tilde fence that must not
        // close it.
        const std::string text =
            "~~~\n"
            "``` stil insde the tilde fence\n"
            "~~~\n"
            "Outsid again.\n";
        TEST("tilde fence content is skipped", IsSkipped(text, "insde"));
        TEST("text after a tilde fence is checked", IsChecked(text, "Outsid"));
    }
    {
        // An unclosed fence swallows the rest of the document, which is what a
        // markdown renderer does too.
        const std::string text = "```\nnever clsed\n";
        TEST("an unclosed fence runs to the end", IsSkipped(text, "clsed"));
    }
    {
        // A longer fence inside a shorter one is content, not a close.
        const std::string text =
            "````\n"
            "``` stil insde\n"
            "````\n"
            "Outsid.\n";
        TEST("a nested shorter fence does not close", IsSkipped(text, "insde"));
        TEST("the longer fence does close", IsChecked(text, "Outsid"));
    }

    // ----- indented code -----
    std::cerr << "\n--- indented code ---" << std::endl;
    {
        const std::string text =
            "Some prose lien.\n"
            "\n"
            "    indentd code here\n"
            "\n"
            "More prse.\n";
        TEST("indented code after a blank line is skipped", IsSkipped(text, "indentd"));
        TEST("prose around it is checked", IsChecked(text, "lien"));
        TEST("prose after it is checked", IsChecked(text, "prse"));
    }
    {
        // A wrapped list item is indented but is still prose.
        const std::string text =
            "- a list item that runs on\n"
            "    contineud on the next line\n";
        TEST("a wrapped list line stays prose", IsChecked(text, "contineud"));
    }

    // ----- inline code spans -----
    std::cerr << "\n--- inline code ---" << std::endl;
    {
        const std::string text = "Call `mispeled_fn()` from yuor code.\n";
        TEST("an inline code span is skipped", IsSkipped(text, "mispeled_fn"));
        TEST("prose around it is checked", IsChecked(text, "yuor"));
    }
    {
        const std::string text = "Use ``a ` tick`` inside, then prse.\n";
        TEST("a double-backtick span is skipped", IsSkipped(text, "tick"));
        TEST("text after it is checked", IsChecked(text, "prse"));
    }
    {
        // An unclosed backtick is literal text, so the rest of the line is
        // still prose.
        const std::string text = "An unmatched ` and som words.\n";
        TEST("an unclosed backtick leaves the line prose", IsChecked(text, "som"));
    }

    // ----- links, images, autolinks and HTML -----
    std::cerr << "\n--- links and tags ---" << std::endl;
    {
        const std::string text = "See [the linkk text](https://exampel.test/path) here.\n";
        TEST("link text is checked", IsChecked(text, "linkk"));
        TEST("the link target is skipped", IsSkipped(text, "exampel"));
    }
    {
        const std::string text = "![alt txt](imgs/pcture.png)\n";
        TEST("image alt text is checked", IsChecked(text, "txt"));
        TEST("the image target is skipped", IsSkipped(text, "pcture"));
    }
    {
        const std::string text = "Bare <https://exampel.test/x> autolink.\n";
        TEST("an autolink is skipped", IsSkipped(text, "exampel"));
        TEST("the word after it is checked", IsChecked(text, "autolink"));
    }
    {
        const std::string text = "Inline <span class=\"noteclass\">visble</span> markup.\n";
        TEST("an HTML tag is skipped", IsSkipped(text, "noteclass"));
        TEST("the text between tags is checked", IsChecked(text, "visble"));
    }
    {
        const std::string text = "[refr]: https://exampel.test/target \"Titel\"\n";
        TEST("a reference definition target is skipped", IsSkipped(text, "exampel"));
        TEST("the reference label is checked", IsChecked(text, "refr"));
    }

    // ----- math -----
    std::cerr << "\n--- math ---" << std::endl;
    {
        const std::string text = "Inline $x_i \\alpah$ and prse.\n";
        TEST("an inline math span is skipped", IsSkipped(text, "alpah"));
        TEST("prose after math is checked", IsChecked(text, "prse"));
    }
    {
        const std::string text = "Display $$\\sigam^2$$ done.\n";
        TEST("a display math span is skipped", IsSkipped(text, "sigam"));
    }
    {
        // Currency is not math: "$5 and $10" must leave the words between the
        // dollars checkable.
        const std::string text = "It cost $5 and thn $10 total.\n";
        TEST("currency does not open a math span", IsChecked(text, "thn"));
    }

    // ----- front matter -----
    std::cerr << "\n--- front matter ---" << std::endl;
    {
        const std::string text =
            "---\n"
            "titel: Somthing\n"
            "---\n"
            "Real prse here.\n";
        TEST("front matter is skipped", IsSkipped(text, "Somthing"));
        TEST("the body after front matter is checked", IsChecked(text, "prse"));
    }
    {
        // A thematic break mid-document is not front matter.
        const std::string text = "Intro prse.\n\n---\n\nMore prse.\n";
        TEST("a thematic break is not front matter", IsChecked(text, "Intro"));
    }

    // ----- result shape and lookup -----
    std::cerr << "\n--- result shape ---" << std::endl;
    {
        const std::string text =
            "Text `code` and [a](b) and <i>x</i> and $m$ done.\n"
            "```\nfence\n```\n";
        const std::vector<TextByteSpan> spans = ScanMarkdownNoSpellRanges(text);
        TEST("spans are sorted and disjoint", SpansAreSortedAndDisjoint(spans));
        TEST("spans stay inside the text",
             spans.empty() || spans.back().EndByte() <= text.size());
    }
    {
        const std::vector<TextByteSpan> spans = { {10, 5} };   // [10, 15)
        TEST("a range before a span is not covered", !SpanCoversRange(spans, 0, 10));
        TEST("a range touching the start is covered", SpanCoversRange(spans, 9, 2));
        TEST("a range inside is covered", SpanCoversRange(spans, 11, 1));
        TEST("a range touching the end is covered", SpanCoversRange(spans, 14, 4));
        TEST("a range after a span is not covered", !SpanCoversRange(spans, 15, 4));
        TEST("an empty range is never covered", !SpanCoversRange(spans, 11, 0));
        TEST("no spans means nothing is covered",
             !SpanCoversRange(std::vector<TextByteSpan>(), 0, 100));
    }
    {
        TEST("empty text yields no spans", ScanMarkdownNoSpellRanges("").empty());
        TEST("plain prose yields no spans",
             ScanMarkdownNoSpellRanges("Just some ordinary words.\n").empty());
    }

    // ----- CRLF -----
    std::cerr << "\n--- CRLF line endings ---" << std::endl;
    {
        const std::string text =
            "Prose lien.\r\n"
            "```\r\n"
            "mispeled\r\n"
            "```\r\n"
            "Aftre.\r\n";
        TEST("CRLF fenced code is skipped", IsSkipped(text, "mispeled"));
        TEST("CRLF prose after a fence is checked", IsChecked(text, "Aftre"));
    }

    std::cerr << "\n=== " << (testCount - failCount) << "/" << testCount
              << " checks passed ===" << std::endl;
    return failCount == 0 ? 0 : 1;
}
