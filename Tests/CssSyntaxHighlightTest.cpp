// Tests/CssSyntaxHighlightTest.cpp
// CSS syntax highlighting (SyntaxTokenizer::TokenizeCssLine): the token type
// each piece of a stylesheet line gets, and that the tokens of a line always
// add up to the line - the text area colours byte ranges from their lengths,
// so a missing or extra byte shifts every colour after it.
// Version: 1.0.0
// Last Modified: 2026-09-25
// Author: UltraCanvas Framework

#include "UltraCanvasSyntaxTokenizer.h"

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

const char* Name(TokenType t) {
    switch (t) {
        case TokenType::Keyword: return "Keyword";
        case TokenType::Identifier: return "Identifier";
        case TokenType::Constant: return "Constant";
        case TokenType::Builtin: return "Builtin";
        case TokenType::Number: return "Number";
        case TokenType::String: return "String";
        case TokenType::Comment: return "Comment";
        case TokenType::Function: return "Function";
        case TokenType::Preprocessor: return "Preprocessor";
        case TokenType::Operator: return "Operator";
        case TokenType::Punctuation: return "Punctuation";
        case TokenType::Whitespace: return "Whitespace";
        default: return "Other";
    }
}

// The type of the n-th token whose text is exactly `text` on `line`.
void Expect(SyntaxTokenizer& tk, const std::string& line, const std::string& text,
            TokenType expected, int occurrence = 0) {
    auto tokens = tk.TokenizeLine(line);
    size_t total = 0;
    for (const auto& t : tokens) total += t.text.size();
    bool found = false;
    TokenType got = TokenType::Unknown;
    for (const auto& t : tokens) {
        if (t.text == text && occurrence-- == 0) { found = true; got = t.type; break; }
    }
    Check(total == line.size() && found && got == expected,
          "\"" + text + "\" in \"" + line.substr(0, 60) + "\" is " + Name(expected) +
          (found ? std::string(" (got ") + Name(got) + ")" : std::string(" (token not found)")) +
          (total == line.size() ? "" : " [token lengths do not cover the line]"));
}

} // namespace

int main() {
    SyntaxTokenizer tk;
    Check(tk.SetLanguageByExtension("css"), ".css selects a language");
    Check(tk.GetCurrentProgrammingLanguage() == "CSS", "...and it is CSS");

    std::cout << "Minified stylesheet (one line)\n";
    const std::string min =
        "*,:before{box-sizing:border-box}@media (prefers-reduced-motion:no-preference){:root{scroll-behavior:smooth}}"
        "[type=search]{-webkit-appearance:textfield;outline-offset:-2px}.g-1{--bs-gutter-x:.25rem}"
        ".x{height:calc(1.5em + var(--bs-border-width) * 2);background:url(\"data:image/svg+xml,%3csvg xmlns='http://www.w3.org/2000/svg'\");color:#fff!important}";
    Expect(tk, min, "*", TokenType::Operator);
    Expect(tk, min, ":before", TokenType::Builtin);
    Expect(tk, min, "box-sizing", TokenType::Keyword);
    Expect(tk, min, "border-box", TokenType::Constant);
    Expect(tk, min, "@media", TokenType::Preprocessor);
    Expect(tk, min, "prefers-reduced-motion", TokenType::Keyword);
    Expect(tk, min, ":root", TokenType::Builtin);            // selector inside @media
    Expect(tk, min, "type", TokenType::Identifier);
    Expect(tk, min, "search", TokenType::String);
    Expect(tk, min, "-webkit-appearance", TokenType::Keyword);
    Expect(tk, min, "-2px", TokenType::Number);
    Expect(tk, min, ".g-1", TokenType::Identifier);
    Expect(tk, min, "--bs-gutter-x", TokenType::Keyword);
    Expect(tk, min, ".25rem", TokenType::Number);
    Expect(tk, min, "calc", TokenType::Function);
    Expect(tk, min, "1.5em", TokenType::Number);
    Expect(tk, min, "--bs-border-width", TokenType::Identifier);
    Expect(tk, min, "url", TokenType::Function);
    Expect(tk, min, "#fff", TokenType::Number);
    Expect(tk, min, "!important", TokenType::Preprocessor);
    // "//" inside a url string is not a comment that eats the rest of the line.
    Expect(tk, min, "color", TokenType::Keyword);

    std::cout << "Multi-line stylesheet\n";
    Expect(tk, "a:hover > span, #main .nav-link:not(.active)::before {", "a", TokenType::Keyword);
    Expect(tk, "a:hover > span, #main .nav-link:not(.active)::before {", "#main", TokenType::Constant);
    Expect(tk, "a:hover > span, #main .nav-link:not(.active)::before {", "::before", TokenType::Builtin);
    Expect(tk, "  color: rgba(0, 0, 0, .5) !important; /* x */", "color", TokenType::Keyword);
    Expect(tk, "  color: rgba(0, 0, 0, .5) !important; /* x */", "rgba", TokenType::Function);
    Expect(tk, "  color: rgba(0, 0, 0, .5) !important; /* x */", "/* x */", TokenType::Comment);
    Expect(tk, "  margin: -.25rem 0 50%;", "50%", TokenType::Number);
    Expect(tk, " * the middle of a comment", " * the middle of a comment", TokenType::Comment);
    Expect(tk, "   end */ body { top: 0 }", "   end */", TokenType::Comment);
    Expect(tk, "   end */ body { top: 0 }", "body", TokenType::Keyword);
    Expect(tk, "@import url(https://example.com/a.css);", "https://example.com/a.css", TokenType::String);
    Expect(tk, "@font-face { font-family: \"X\"; src: url(x.woff2) }", "font-family", TokenType::Keyword);
    Expect(tk, "@keyframes spin { from { opacity: 0 } 50% { opacity: .5 } }", "from", TokenType::Keyword);
    Expect(tk, "@keyframes spin { from { opacity: 0 } 50% { opacity: .5 } }", "opacity", TokenType::Keyword);
    Expect(tk, "li:nth-child(2n+1) { display: none }", "2n", TokenType::Number);
    Expect(tk, "li:nth-child(2n+1) { display: none }", "+", TokenType::Operator);
    Expect(tk, "li:nth-child(2n+1) { display: none }", "none", TokenType::Constant);

    std::cout << (g_failures ? "FAILED: " : "PASSED") << (g_failures ? std::to_string(g_failures) : "") << "\n";
    return g_failures ? 1 : 0;
}
