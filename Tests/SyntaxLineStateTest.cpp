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
//
// Also: strings each language lets run across lines (Python's """, the
// JavaScript template literal, a C string continued with a backslash), and
// SetLanguageByTag, which names a Markdown fence's language; block comments
// whose opener starts with the line-comment marker (Lua's --[[); C++ raw
// strings, R"delim( ... )delim".
// Version: 1.1.0
// Last Modified: 2026-11-11
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

    std::cout << "Strings that span lines, per language\n";
    {
        tk.SetLanguage("Python");
        auto t = Highlight(tk, {{"doc = \"\"\"first line", "return while if", "end\"\"\" + name", "return x"}, {}});
        Expect(t[1], "return while if", TokenType::String, "Python: inside a \"\"\" string");
        Expect(t[2], "end\"\"\"", TokenType::String, "Python: the closing \"\"\"");
        Expect(t[2], "name", TokenType::Identifier, "Python: code after it");
        Expect(t[3], "return", TokenType::Keyword, "Python: next line is code");
        auto q = Highlight(tk, {{"x = '''a", "b''' + c"}, {}});
        Expect(q[1], "b'''", TokenType::String, "Python: ''' too");
        auto one = Highlight(tk, {{"x = \"\"\"inline\"\"\" + y"}, {}});
        Expect(one[0], "\"\"\"inline\"\"\"", TokenType::String, "Python: \"\"\" closed on its own line");
        Expect(one[0], "y", TokenType::Identifier, "Python: code after it");
        auto plain = Highlight(tk, {{"s = \"unclosed", "return 1"}, {}});
        Expect(plain[1], "return", TokenType::Keyword, "Python: an ordinary string ends at the line");
        auto cont = Highlight(tk, {{"s = \"abc\\", "def\" + x"}, {}});
        Expect(cont[1], "def\"", TokenType::String, "Python: a backslash continues the string");
        Expect(cont[1], "x", TokenType::Identifier, "Python: code after the continued string");

        tk.SetLanguage("C");
        auto c = Highlight(tk, {{"char* s = \"abc\\", "def\"; return 0;"}, {}});
        Expect(c[1], "def\"", TokenType::String, "C: a backslash continues the string");
        Expect(c[1], "return", TokenType::Keyword, "C: code after it");
        auto even = Highlight(tk, {{"char* s = \"abc\\\\", "return 0;"}, {}});
        Expect(even[1], "return", TokenType::Keyword, "C: an escaped backslash does not continue");

        tk.SetLanguage("JavaScript");
        auto js = Highlight(tk, {{"const t = `first", "", "last` + x; return t;"}, {}});
        Expect(js[2], "last`", TokenType::String, "JavaScript: a template literal spans lines, empty ones too");
        Expect(js[2], "return", TokenType::Keyword, "JavaScript: code after it");
        auto jsq = Highlight(tk, {{"const s = 'unclosed", "return s;"}, {}});
        Expect(jsq[1], "return", TokenType::Keyword, "JavaScript: a quoted string ends at the line");

        tk.SetLanguage("Go");
        auto go = Highlight(tk, {{"s := `raw", "text` + x", "return"}, {}});
        Expect(go[1], "text`", TokenType::String, "Go: a raw string spans lines");
        Expect(go[2], "return", TokenType::Keyword, "Go: code after it");

        tk.SetLanguage("Kotlin");
        auto kt = Highlight(tk, {{"val s = \"\"\"", "  return", "\"\"\""}, {}});
        Expect(kt[1], "  return", TokenType::String, "Kotlin: inside a \"\"\" string");

        tk.SetLanguage("Shell Script");
        auto sh = Highlight(tk, {{"echo \"one", "two\"; exit"}, {}});
        Expect(sh[1], "two\"", TokenType::String, "Shell: a quoted string spans lines");

        tk.SetLanguage("Java");
        auto java = Highlight(tk, {{"String s = \"unclosed", "return s;"}, {}});
        Expect(java[1], "return", TokenType::Keyword, "Java: a quoted string ends at the line");
    }

    std::cout << "Languages named by a fence tag\n";
    {
        auto expectLang = [&](const std::string& tag, const std::string& expected) {
            tk.SetLanguage("Python");            // something set before
            tk.SetLanguageByTag(tag);
            const std::string got = tk.GetCurrentProgrammingLanguage();
            Check(got == expected, "\"" + tag + "\" -> " + expected + " (got " + got + ")");
        };
        expectLang("cpp", "C++");
        expectLang("C++", "C++");
        expectLang("javascript", "JavaScript");
        expectLang("js", "JavaScript");
        expectLang("Rust", "Rust");
        expectLang("{.rust}", "Rust");
        expectLang("golang", "Go");
        expectLang("shell", "Shell Script");
        expectLang("bash", "Shell Script");
        expectLang("csharp", "C#");
        expectLang("json copy", "JSON");
        expectLang("foo", "Plain Text");
        expectLang("text", "Plain Text");
        expectLang("", "Plain Text");
        tk.SetLanguageByTag("foo");
        Check(tk.TokenizeLine(std::string("int x = 0;")).empty(),
              "an unknown tag highlights nothing");
    }

    std::cout << "Block comments that start like a line comment\n";
    {
        tk.SetLanguage("Lua");
        auto t = Highlight(tk, {{"--[[ a block", "local x = 1", "]] return x", "-- a line comment", "return y"}, {}});
        Expect(t[1], "local x = 1", TokenType::Comment, "Lua: inside --[[ ]]");
        Expect(t[2], "]]", TokenType::Comment, "Lua: the closing ]]");
        Expect(t[2], "return", TokenType::Keyword, "Lua: code after the block");
        Expect(t[3], "-- a line comment", TokenType::Comment, "Lua: -- is still a line comment");
        Expect(t[4], "return", TokenType::Keyword, "Lua: ...that ends at the line");
        auto one = Highlight(tk, {{"--[[ inline ]] return z"}, {}});
        Expect(one[0], "return", TokenType::Keyword, "Lua: --[[ ]] closed on its own line");

        tk.SetLanguage("MATLAB");
        auto m = Highlight(tk, {{"%{", "x = 1", "%}", "x = 2"}, {}});
        Expect(m[1], "x = 1", TokenType::Comment, "MATLAB: inside %{ %}");
        Expect(m[3], "2", TokenType::Number, "MATLAB: code after the block");
    }

    std::cout << "C++ raw strings\n";
    {
        tk.SetLanguage("C++");
        auto t = Highlight(tk, {{"auto s = R\"(first \"line\"", "return \\n while", ")\" + y; return 0;"}, {}});
        Expect(t[1], "return \\n while", TokenType::String, "inside a raw string over three lines");
        Expect(t[2], ")\"", TokenType::String, "the closing )\"");
        Expect(t[2], "return", TokenType::Keyword, "code after it");
        auto d = Highlight(tk, {{"auto s = R\"xy(a )\" still )xy\" + z;"}, {}});
        Expect(d[0], "R\"xy(a )\" still )xy\"", TokenType::String, "a delimiter: )\" does not close R\"xy(");
        Expect(d[0], "z", TokenType::Identifier, "code after the delimited raw string");
        auto u8 = Highlight(tk, {{"auto s = u8R\"(x)\"; return s;"}, {}});
        Expect(u8[0], "u8R\"(x)\"", TokenType::String, "a u8R prefix");
        Expect(u8[0], "return", TokenType::Keyword, "code after it");
        auto word = Highlight(tk, {{"myR\"(x)\" + 1"}, {}});
        Expect(word[0], "myR", TokenType::Identifier, "myR\"...\" is a name and a string, not a raw string");
        auto esc = Highlight(tk, {{"auto p = R\"(C:\\dir\\)\"; return p;"}, {}});
        Expect(esc[0], "return", TokenType::Keyword, "a backslash before )\" does not escape it");

        tk.SetLanguage("C");
        auto c = Highlight(tk, {{"char* s = R\"(not raw in C", "return 0;"}, {}});
        Expect(c[1], "return", TokenType::Keyword, "C has no raw strings");
    }

    std::cout << (g_failures ? "FAILED: " + std::to_string(g_failures) : std::string("PASSED")) << "\n";
    return g_failures ? 1 : 0;
}
