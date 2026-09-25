// Tests/SyntaxLineStateTest.cpp
// Syntax highlighting that carries state from one line to the next
// (SyntaxTokenizer::TokenizeLine(line, SyntaxLineState&)).
//
// The text area highlights one line at a time. Without the state the line
// above leaves open, the middle and last lines of a /* block comment */ were
// coloured as code, and a string or comment cut where an over-long line is
// split into segments lost its colour at the cut - which, for a minified
// stylesheet split every 8000 characters, restarted the CSS scanner from a
// guess on every segment.
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
        case TokenType::Type: return "Type";
        case TokenType::Identifier: return "Identifier";
        case TokenType::Constant: return "Constant";
        case TokenType::Builtin: return "Builtin";
        case TokenType::Number: return "Number";
        case TokenType::String: return "String";
        case TokenType::Comment: return "Comment";
        case TokenType::Function: return "Function";
        case TokenType::Operator: return "Operator";
        case TokenType::Punctuation: return "Punctuation";
        default: return "Other";
    }
}

// A run of lines as the text area sees them. `breakAfter[i]` is false when
// line i and line i+1 are two segments of one line split for length.
struct Lines {
    std::vector<std::string> text;
    std::vector<bool> breakAfter;
};

std::vector<std::vector<SyntaxTokenizer::Token>> Highlight(SyntaxTokenizer& tk, const Lines& lines) {
    std::vector<std::vector<SyntaxTokenizer::Token>> out;
    SyntaxLineState state;
    for (size_t i = 0; i < lines.text.size(); ++i) {
        out.push_back(tk.TokenizeLine(lines.text[i], state));
        size_t total = 0;
        for (const auto& t : out.back()) total += t.text.size();
        Check(total == lines.text[i].size(), "tokens cover line " + std::to_string(i) + " byte for byte");
        bool realBreak = i >= lines.breakAfter.size() || lines.breakAfter[i];
        if (realBreak) state = state.AtLineBreak();
    }
    return out;
}

// The type of the first token on `tokens` whose text is exactly `text`.
void Expect(const std::vector<SyntaxTokenizer::Token>& tokens, const std::string& text,
            TokenType expected, const std::string& where) {
    for (const auto& t : tokens) {
        if (t.text == text) {
            Check(t.type == expected, where + ": \"" + text + "\" is " + Name(expected) +
                                      " (got " + Name(t.type) + ")");
            return;
        }
    }
    Check(false, where + ": \"" + text + "\" not found as a token");
}

} // namespace

int main() {
    SyntaxTokenizer tk;

    std::cout << "C++ block comment over three lines\n";
    tk.SetLanguage("C++");
    {
        auto t = Highlight(tk, {{"size_t a; /* starts here", "   return while if", "   ends */ uint8_t b;"}, {}});
        Expect(t[0], "size_t", TokenType::Type, "line 1");
        Expect(t[1], "   return while if", TokenType::Comment, "middle line");
        Expect(t[2], "   ends */", TokenType::Comment, "closing line");
        Expect(t[2], "uint8_t", TokenType::Type, "after the close");
        // The same middle line on its own is code - the reason for the state.
        auto alone = tk.TokenizeLine(std::string("   return while if"));
        Expect(alone, "return", TokenType::Keyword, "middle line without state");
    }

    std::cout << "Pascal: the right closing delimiter\n";
    tk.SetLanguage("Pascal");
    {
        auto t = Highlight(tk, {{"begin { brace comment", "  still *) inside", "  closes } end"}, {}});
        Expect(t[1], "  still *) inside", TokenType::Comment, "'*)' does not close a '{' comment");
        Expect(t[2], "  closes }", TokenType::Comment, "'}' closes it");
        Expect(t[2], "end", TokenType::Keyword, "after the close");
    }

    std::cout << "Segments of one long line\n";
    tk.SetLanguage("C++");
    {
        // String cut at a segment boundary, then a real line break.
        auto t = Highlight(tk, {{"auto s = \"first half ", "second half\"; size_t x;", "return 0;"},
                                {false, true}});
        Expect(t[1], "second half\"", TokenType::String, "string continues into the next segment");
        Expect(t[1], "size_t", TokenType::Type, "code after the string");
        Expect(t[2], "return", TokenType::Keyword, "next real line");
        // Line comment cut at a segment boundary ends at the real break.
        auto c = Highlight(tk, {{"x = 1; // a long ", "comment tail", "return x;"}, {false, true}});
        Expect(c[1], "comment tail", TokenType::Comment, "line comment continues into the next segment");
        Expect(c[2], "return", TokenType::Keyword, "line comment ends at the real line break");
        // An unclosed string does not run past a real line break.
        auto u = Highlight(tk, {{"char* s = \"oops", "return 1;"}, {}});
        Expect(u[1], "return", TokenType::Keyword, "unclosed string stops at the line break");
    }

    std::cout << "CSS across lines\n";
    tk.SetLanguage("CSS");
    {
        auto t = Highlight(tk, {{"@media screen {", "  a:hover", "  { color: red }", "}", "b { top: 0 }"}, {}});
        // On its own "  a:hover" looks like a declaration ("a: hover").
        Expect(t[1], ":hover", TokenType::Builtin, "selector line inside @media");
        Expect(t[2], "color", TokenType::Keyword, "property");
        Expect(t[2], "red", TokenType::Constant, "value");
        Expect(t[4], "b", TokenType::Keyword, "top-level selector after the block closes");

        auto m = Highlight(tk, {{"/* commented out:", "a { color: red; }", "*/ p { margin: 0 }"}, {}});
        Expect(m[1], "a { color: red; }", TokenType::Comment, "rule inside a block comment");
        Expect(m[2], "p", TokenType::Keyword, "selector after the comment closes");

        auto v = Highlight(tk, {{"a {", "  box-shadow: 0 0 1px red,", "    inset 0 1px blue;", "}"}, {}});
        Expect(v[2], "inset", TokenType::Constant, "value continued on the next line");
        Expect(v[2], "blue", TokenType::Constant, "...still a value");
    }

    std::cout << "Minified CSS split into segments\n";
    {
        auto t = Highlight(tk, {{".x{background:url(\"data:image/svg+xml,%3csvg ",
                                 "fill='%23fff'%3e\")}.y{top:0}", ".z{margin:",
                                 "0 auto}a:hover{color:#fff}"},
                                {false, false, false, true}});
        Expect(t[1], "fill='%23fff'%3e\"", TokenType::String, "string continues across the cut");
        Expect(t[1], ".y", TokenType::Identifier, "class after the string");
        Expect(t[1], "top", TokenType::Keyword, "property after the string");
        Expect(t[3], "auto", TokenType::Constant, "value continues across the cut");
        Expect(t[3], ":hover", TokenType::Builtin, "selector after the value");
        Expect(t[3], "#fff", TokenType::Number, "colour");
    }

    std::cout << (g_failures ? "FAILED: " + std::to_string(g_failures) : std::string("PASSED")) << "\n";
    return g_failures ? 1 : 0;
}
