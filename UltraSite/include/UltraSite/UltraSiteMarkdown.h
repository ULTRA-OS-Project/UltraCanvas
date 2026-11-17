// UltraSite/include/UltraSite/UltraSiteMarkdown.h
// Markdown as a web page's HTML, the way a documentation site needs it. The
// Markdown itself is read by the framework (UCRichDocument::FromMarkdown)
// and written by ToHTML; this adds what a site needs around it:
//   - front matter: "---" lines at the top with key: value pairs;
//   - an id on every heading, spelled as GitHub spells it, so the links a
//     repository's Markdown already has (other.md#some-heading) still work;
//     the h2 and h3 become the table of contents;
//   - code highlighted when the site is built (UltraCanvasSyntaxTokenizer),
//     so the browser runs no highlighter, with a copy button if asked;
//   - admonitions - ":::note Title" ... ":::", also tip, info, warning,
//     danger, and GitHub's "> [!NOTE]" - and tabs - ":::tabs", one
//     "::tab[Label]" line per tab, ":::" - each readable without script;
//   - every link and picture passed through the caller, which turns
//     "other.md" into a page's address and a picture into an asset.
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSite/UltraSite.h"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace UltraSite {

struct FrontMatter {
    bool present = false;
    std::map<std::string, std::string> values;   // quotes around a value removed
    std::string body;                            // the Markdown after it
};

// The front matter of a Markdown text: "---" on the first line, key: value
// lines, "---". Without it, `body` is the whole text.
FrontMatter ParseFrontMatter(const std::string& text);

// A heading's id as GitHub writes it: lower case, ASCII punctuation
// removed, each space a '-' ("Second: one & two" -> "second-one--two").
std::string Slugify(const std::string& text);

// Ids for the headings of one page: a repeated one gets -1, -2, ...
class HeadingIds {
public:
    std::string Next(const std::string& text);

private:
    std::map<std::string, int> seen;
};

// A code block as HTML: <pre class="us-code"><code class="language-x">
// with each token in a <span class="tok-..."> (k keyword, t type, f
// function, n number, s string, c comment, o operator, p punctuation, pp
// preprocessor, cn constant, b builtin). An unknown language is plain.
// With a copy label, the <pre> asks for the copy behaviour.
std::string HighlightCode(const std::string& language, const std::string& code,
                          const std::string& copyLabel = std::string(), const std::string& copiedLabel = std::string());

struct MarkdownOptions {
    std::string baseDirectory;               // pictures resolve against it
    // A link's target as the page should have it; unset: as written. Not
    // called for a picture, an absolute URL, a site path ("/...") or a
    // "#anchor".
    std::function<std::string(const std::string& target)> link;
    // A picture's URL, given its file name and bytes; unset: a data: URI.
    std::function<std::string(const std::string& name, const std::vector<uint8_t>& data)> image;
    std::string copyLabel;                   // the code blocks' copy button; empty: none
    std::string copiedLabel;
};

struct MarkdownResult {
    std::string html;
    std::vector<Heading> toc;                // the h2 and h3, with their ids
    std::string title;                       // the first h1's text; empty when there is none
};

MarkdownResult MarkdownToHtml(const std::string& markdown, const MarkdownOptions& options = {});

} // namespace UltraSite
