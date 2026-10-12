// Tests/FilerTextPreviewTest.cpp
// The page a document's preview card shows (UltraCanvasFilerWidget::
// TextPreviewLines): an .html file as a browser lays it out - a line per
// paragraph, list item and table row, the head, scripts and hidden text left
// out, every entity decoded - and a plain text file line for line. Writes its
// files into a temporary folder.
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

std::string Joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) out += "[" + line + "]";
    return out;
}

bool Contains(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& line : lines)
        if (line.find(text) != std::string::npos) return true;
    return false;
}

std::string Write(const std::filesystem::path& folder, const char* name, const std::string& content) {
    const std::filesystem::path path = folder / name;
    std::ofstream(path, std::ios::binary) << content;
    return PathToUtf8(path);
}

} // namespace

int main() {
    std::cout << "FilerTextPreviewTest\n";
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / "ultracanvas-filer-text-preview";
    std::filesystem::create_directories(folder);

    // ===== An HTML page =====
    const std::string page = Write(folder, "page.html",
        "<!DOCTYPE html><html><head><title>Page title</title>"
        "<style>p { color: red }</style>"
        "<script>var s = \"<p>not text</p>\";</script></head><body>"
        "<div style=\"display:none\">Hidden preheader</div>"
        "<h1>Caf&eacute; menu</h1>"
        "<p>First paragraph\n   wrapped in the source.</p>"
        "<p>Second &#8364;&nbsp;5</p>"
        "<ul><li>one</li><li>two</li></ul>"
        "<table><tr><td>Name</td><td>Price</td></tr></table>"
        "</body></html>");
    std::vector<std::string> lines;
    bool tabular = true;
    Check(UltraCanvasFilerWidget::TextPreviewLines(page, lines, tabular), "an .html file has a preview");
    std::cout << "    " << Joined(lines) << "\n";
    Check(!tabular, "an .html file is a page, not rows of cells");
    Check(!lines.empty() && lines.front() == "Caf\xC3\xA9 menu",
          "the page starts with its first heading, entities decoded");
    Check(Contains(lines, "First paragraph wrapped in the source."),
          "a paragraph is one line, however the source wraps it");
    Check(Contains(lines, "Second \xE2\x82\xAC 5"), "a numeric reference and a no-break space read as text");
    Check(Contains(lines, "- one") && Contains(lines, "- two"), "list items are lines with their marker");
    Check(Contains(lines, "Name Price"), "the cells of a table row stay apart");
    Check(!Contains(lines, "Page title"), "the head's title is not page text");
    Check(!Contains(lines, "not text") && !Contains(lines, "color"), "scripts and style sheets are left out");
    Check(!Contains(lines, "Hidden preheader"), "hidden text is left out");

    // ===== Plain text, as it is =====
    const std::string text = Write(folder, "notes.txt", "\n\nfirst line\nsecond  line\n");
    Check(UltraCanvasFilerWidget::TextPreviewLines(text, lines, tabular), "a .txt file has a preview");
    Check(lines.size() == 2 && lines[0] == "first line" && lines[1] == "second line",
          "plain text keeps its lines, the blank top skipped (" + Joined(lines) + ")");

    // ===== CSV: rows of cells =====
    const std::string csv = Write(folder, "table.csv", "a,b\n1,2\n");
    Check(UltraCanvasFilerWidget::TextPreviewLines(csv, lines, tabular) && tabular,
          "a .csv file is rows of cells");

    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);

    if (g_failures == 0) {
        std::cout << "All checks passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
