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
// strings, R"delim( ... )delim"; Lua's long brackets at any level,
// --[==[ ... ]==] and [==[ ... ]==]; string prefixes (Python's r"..", C's
// L".."), coloured with their string; Rust raw strings (r#".."#) and C#
// verbatim strings (@".."); C# 11 raw strings (""".."""); preprocessor
// lines that keep their strings and comments; and the whole-text Tokenize, which
// must give the same tokens.
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
        case TokenType::Preprocessor: return "Preprocessor";
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

    std::cout << "Lua long brackets at any level\n";
    {
        tk.SetLanguage("Lua");
        auto c = Highlight(tk, {{"--[==[ a comment", "]] is not its end", "]==] return x"}, {}});
        Expect(c[1], "]] is not its end", TokenType::Comment, "--[==[: a plain ]] does not close it");
        Expect(c[2], "]==]", TokenType::Comment, "--[==[: closed by ]==]");
        Expect(c[2], "return", TokenType::Keyword, "--[==[: code after it");
        auto one = Highlight(tk, {{"--[=[ inline ]=] return y"}, {}});
        Expect(one[0], "--[=[ inline ]=]", TokenType::Comment, "--[=[ ]=] closed on its own line");
        Expect(one[0], "return", TokenType::Keyword, "code after it");
        auto s = Highlight(tk, {{"local s = [[first", "return\\n", "last]] .. t"}, {}});
        Expect(s[1], "return\\n", TokenType::String, "[[ string over three lines, no escapes");
        Expect(s[2], "last]]", TokenType::String, "[[ closed by ]]");
        Expect(s[2], "t", TokenType::Identifier, "code after the long string");
        auto lvl = Highlight(tk, {{"local s = [=[ a ]] b", "]=] + 1"}, {}});
        Expect(lvl[1], "]=]", TokenType::String, "[=[ closed only by ]=]");
        Expect(lvl[1], "1", TokenType::Number, "code after it");
        auto space = Highlight(tk, {{"-- [[ a line comment, not a block", "return z"}, {}});
        Expect(space[1], "return", TokenType::Keyword, "-- [[ with a space is a line comment");
        auto half = Highlight(tk, {{"--[= not a bracket", "return w"}, {}});
        Expect(half[1], "return", TokenType::Keyword, "--[= without a second [ is a line comment");
        auto index = Highlight(tk, {{"x = a[b] return x"}, {}});
        Expect(index[0], "b", TokenType::Identifier, "a[b] is an index, not a long bracket");
    }

    std::cout << "String prefixes\n";
    {
        tk.SetLanguage("Python");
        auto r = Highlight(tk, {{"x = r\"\\d+\\.\\w\" + y"}, {}});
        Expect(r[0], "r\"\\d+\\.\\w\"", TokenType::String, "Python: r\"..\" is one string, prefix included");
        Expect(r[0], "y", TokenType::Identifier, "Python: code after it");
        auto q = Highlight(tk, {{"x = r\"\\\"\" + y"}, {}});
        Expect(q[0], "r\"\\\"\"", TokenType::String, "Python: in a raw string \\\" still does not close it");
        Expect(q[0], "y", TokenType::Identifier, "Python: ...and the string ends at the next quote");
        auto open = Highlight(tk, {{"p = r\"C:\\\"", "return p"}, {}});
        Expect(open[1], "return", TokenType::Keyword, "Python: r\"C:\\\" is unterminated, as Python reads it");
        auto rb = Highlight(tk, {{"x = Rb'\\x00' + f\"{a}\""}, {}});
        Expect(rb[0], "Rb'\\x00'", TokenType::String, "Python: Rb'..' in any case");
        Expect(rb[0], "f\"{a}\"", TokenType::String, "Python: f\"..\"");
        auto doc = Highlight(tk, {{"pattern = r\"\"\"a", "b\"\"\" + z"}, {}});
        Expect(doc[0], "r\"\"\"a", TokenType::String, "Python: r\"\"\" opens a long string with its prefix");
        Expect(doc[1], "b\"\"\"", TokenType::String, "Python: ...closed on the next line");
        auto word = Highlight(tk, {{"x = name\"s\""}, {}});
        Expect(word[0], "name", TokenType::Identifier, "Python: a name that is not a prefix stays a name");
        auto spaced = Highlight(tk, {{"x = r \"s\""}, {}});
        Expect(spaced[0], "r", TokenType::Identifier, "Python: r then a space then a string is a name");

        auto names = Highlight(tk, {{"e = f + L + u + e1"}, {}});
        Expect(names[0], "e", TokenType::Identifier, "a variable named e is a name, not a number");
        Expect(names[0], "f", TokenType::Identifier, "...nor is f");
        Expect(names[0], "e1", TokenType::Identifier, "...nor e1");
        auto nums = Highlight(tk, {{"x = 1e5 + 2.5 + .5"}, {}});
        Expect(nums[0], "1e5", TokenType::Number, "1e5 is still a number");
        Expect(nums[0], ".5", TokenType::Number, ".5 is still a number");

        tk.SetLanguage("C++");
        auto cnum = Highlight(tk, {{"float x = 1.5f + L;"}, {}});
        Expect(cnum[0], "1.5f", TokenType::Number, "C++: 1.5f is a number");
        Expect(cnum[0], "L", TokenType::Identifier, "C++: a lone L is a name");
        auto c = Highlight(tk, {{"auto a = L\"wide\"; auto b = u8\"utf\";"}, {}});
        Expect(c[0], "L\"wide\"", TokenType::String, "C++: L\"..\"");
        Expect(c[0], "u8\"utf\"", TokenType::String, "C++: u8\"..\"");
    }

    std::cout << "Rust raw strings and C# verbatim strings\n";
    {
        tk.SetLanguage("Rust");
        auto r = Highlight(tk, {{"let p = r\"C:\\\"; return p;"}, {}});
        Expect(r[0], "r\"C:\\\"", TokenType::String, "Rust: r\"C:\\\" is complete - no escapes");
        Expect(r[0], "return", TokenType::Keyword, "Rust: code after it");
        auto h = Highlight(tk, {{"let s = r#\"say \"hi\"\"# + x;"}, {}});
        Expect(h[0], "r#\"say \"hi\"\"#", TokenType::String, "Rust: r#\"..\"# holds quotes");
        Expect(h[0], "x", TokenType::Identifier, "Rust: code after it");
        auto two = Highlight(tk, {{"let s = r##\"a \"# b", "c\"## ; return s;"}, {}});
        Expect(two[0], "r##\"a \"# b", TokenType::String, "Rust: \"# does not close r##\"");
        Expect(two[1], "c\"##", TokenType::String, "Rust: ...\"## closes it on the next line");
        Expect(two[1], "return", TokenType::Keyword, "Rust: code after it");
        auto br = Highlight(tk, {{"let b = br\"\\x\"; return b;"}, {}});
        Expect(br[0], "br\"\\x\"", TokenType::String, "Rust: br\"..\"");
        auto ident = Highlight(tk, {{"let r#match = 1;"}, {}});
        Expect(ident[0], "match", TokenType::Keyword, "Rust: r#match is a raw identifier, not a string");
        auto plain = Highlight(tk, {{"let s = \"a\\\"b\"; return s;"}, {}});
        Expect(plain[0], "\"a\\\"b\"", TokenType::String, "Rust: an ordinary string still has escapes");

        tk.SetLanguage("C#");
        auto v = Highlight(tk, {{"var p = @\"C:\\\"; return p;"}, {}});
        Expect(v[0], "@\"C:\\\"", TokenType::String, "C#: @\"C:\\\" is complete - no escapes");
        Expect(v[0], "return", TokenType::Keyword, "C#: code after it");
        auto q = Highlight(tk, {{"var s = @\"say \"\"hi\"\"\" + x;"}, {}});
        Expect(q[0], "@\"say \"\"hi\"\"\"", TokenType::String, "C#: \"\" inside is a quote");
        Expect(q[0], "x", TokenType::Identifier, "C#: code after it");
        auto ml = Highlight(tk, {{"var s = $@\"first", "return \"\"x\"\"", "last\"; return s;"}, {}});
        Expect(ml[1], "return \"\"x\"\"", TokenType::String, "C#: $@\"..\" spans lines");
        Expect(ml[2], "last\"", TokenType::String, "C#: ...closed on the third line");
        Expect(ml[2], "return", TokenType::Keyword, "C#: code after it");
        auto at = Highlight(tk, {{"var @class = 1; return @class;"}, {}});
        Expect(at[0], "return", TokenType::Keyword, "C#: @class is a name, not a string");
        auto ord = Highlight(tk, {{"var s = \"unclosed", "return s;"}, {}});
        Expect(ord[1], "return", TokenType::Keyword, "C#: an ordinary string ends at the line");
    }

    std::cout << "C# raw strings\n";
    {
        tk.SetLanguage("C#");
        auto one = Highlight(tk, {{"var p = \"\"\"C:\\\"\"\" + x;"}, {}});
        Expect(one[0], "\"\"\"C:\\\"\"\"", TokenType::String, "\"\"\"C:\\\"\"\" - no escapes, ends at \"\"\"");
        Expect(one[0], "x", TokenType::Identifier, "code after it");
        auto ml = Highlight(tk, {{"var s = \"\"\"", "  say \"hi\" and \"\"quoted\"\"", "  \"\"\"; return s;"}, {}});
        Expect(ml[1], "  say \"hi\" and \"\"quoted\"\"", TokenType::String, "a raw string over lines holds \" and \"\"");
        Expect(ml[2], "return", TokenType::Keyword, "code after the closing \"\"\"");
        auto four = Highlight(tk, {{"var s = \"\"\"\"has \"\"\" inside\"\"\"\" + y;"}, {}});
        Expect(four[0], "\"\"\"\"has \"\"\" inside\"\"\"\"", TokenType::String, "\"\"\"\" ends only at \"\"\"\"");
        Expect(four[0], "y", TokenType::Identifier, "code after it");
        auto interp = Highlight(tk, {{"var s = $$\"\"\"{{x}}\"\"\" + z;"}, {}});
        Expect(interp[0], "$$\"\"\"{{x}}\"\"\"", TokenType::String, "$$\"\"\"..\"\"\" interpolated raw string");
        auto empty = Highlight(tk, {{"var e = \"\" + w;"}, {}});
        Expect(empty[0], "\"\"", TokenType::String, "\"\" is an empty string, not a raw one");
        Expect(empty[0], "w", TokenType::Identifier, "code after it");
        auto verb = Highlight(tk, {{"var v = @\"a\"\"b\" + q;"}, {}});
        Expect(verb[0], "@\"a\"\"b\"", TokenType::String, "a verbatim string is unchanged");
    }

    std::cout << "Preprocessor lines\n";
    {
        tk.SetLanguage("C++");
        auto raw = Highlight(tk, {{"#define SQL R\"(SELECT", "  *  FROM t)\"", "int x = 0;"}, {}});
        Expect(raw[0], "define", TokenType::Preprocessor, "#define is a directive");
        Expect(raw[0], "R\"(SELECT", TokenType::String, "a raw string in a #define is a string");
        Expect(raw[1], "  *  FROM t)\"", TokenType::String, "...carried to the next line");
        Expect(raw[2], "int", TokenType::Keyword, "code after the directive");
        auto cont = Highlight(tk, {{"#define MAX(a, b) \\", "  ((a) > (b) ? (a) : (b))", "int y = MAX(1, 2);"}, {}});
        Expect(cont[1], "a", TokenType::Preprocessor, "a \\-continued #define's next line is the directive");
        Expect(cont[2], "int", TokenType::Keyword, "the directive ends without a \\");
        auto inc = Highlight(tk, {{"#include \"config.h\" // local"}, {}});
        Expect(inc[0], "\"config.h\"", TokenType::String, "#include \"..\": the path is a string");
        Expect(inc[0], "// local", TokenType::Comment, "a comment on a directive line is a comment");
        Expect(inc[0], "include", TokenType::Preprocessor, "...and include is the directive");
        auto ind = Highlight(tk, {{"  #if DEBUG", "int z;"}, {}});
        Expect(ind[0], "if", TokenType::Preprocessor, "an indented #if is a directive");
        Expect(ind[1], "int", TokenType::Keyword, "code after it");
        auto blk = Highlight(tk, {{"#define A 1 /* starts", "still comment */ int w;"}, {}});
        Expect(blk[1], "still comment */", TokenType::Comment, "a block comment opened in a #define carries on");
        Expect(blk[1], "int", TokenType::Keyword, "code after it, not the directive");
        auto inside = Highlight(tk, {{"/* a comment", "#define X */ int v;"}, {}});
        Expect(inside[1], "int", TokenType::Keyword, "a '#' inside a comment does not start a directive");

        tk.SetLanguage("Python");
        auto py = Highlight(tk, {{"# a comment", "return 1"}, {}});
        Expect(py[0], "# a comment", TokenType::Comment, "Python: # is a comment, not a directive");
    }

    std::cout << "Tokenize: the whole text, as the text area sees it\n";
    {
        // Every language and construct the line state covers, through
        // Tokenize, must match TokenizeLine line by line with the state.
        struct Case { const char* language; std::string text; };
        const std::vector<Case> cases = {
            {"Lua", "--[==[ a\n]] still\n]==] return x\nlocal s = [=[ a\n]=] .. y"},
            {"Python", "doc = \"\"\"a\nreturn\n\"\"\" + x\ns = \"abc\\\ndef\""},
            {"C++", "auto s = R\"x(a\n)\" )x\";\n/* c\nd */ int y;"},
            {"CSS", "a {\n  color: red; }\n/* x\ny */ b { top: 0 }"},
            {"JavaScript", "const t = `a\r\n\r\nb`; return t;"},
            {"Rust", "let s = r#\"a\n\"b\"#; return s;"},
            {"C#", "var s = @\"a\n\"\"b\"; return s;"},
        };
        for (const Case& c : cases) {
            tk.SetLanguage(c.language);
            auto whole = tk.Tokenize(c.text);
            std::vector<SyntaxTokenizer::Token> expected;
            SyntaxLineState state;
            size_t start = 0;
            while (true) {
                size_t end = c.text.find('\n', start);
                std::string line = c.text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                auto lt = tk.TokenizeLine(line, state);
                expected.insert(expected.end(), lt.begin(), lt.end());
                if (end == std::string::npos) break;
                expected.push_back({TokenType::Newline, "\n", 1});
                state = state.AtLineBreak();
                start = end + 1;
            }
            bool same = whole.size() == expected.size();
            for (size_t i = 0; same && i < whole.size(); ++i)
                same = whole[i].type == expected[i].type && whole[i].text == expected[i].text;
            Check(same, std::string(c.language) + ": Tokenize matches line-by-line highlighting");
        }
        tk.SetLanguage("Lua");
        auto lua = tk.Tokenize("--[==[ a\n]] still\n]==] return x");
        size_t newlines = 0;
        bool middleComment = false, returnKeyword = false;
        for (const auto& t : lua) {
            if (t.type == TokenType::Newline) newlines++;
            if (t.text == "]] still" && t.type == TokenType::Comment) middleComment = true;
            if (t.text == "return" && t.type == TokenType::Keyword) returnKeyword = true;
        }
        Check(newlines == 2, "Tokenize: one Newline token per line break");
        Check(middleComment && returnKeyword, "Tokenize: a levelled Lua comment over three lines");
    }

    std::cout << (g_failures ? "FAILED: " + std::to_string(g_failures) : std::string("PASSED")) << "\n";
    return g_failures ? 1 : 0;
}
