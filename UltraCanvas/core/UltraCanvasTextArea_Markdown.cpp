// UltraCanvas/core/UltraCanvasTextArea_Markdown.cpp
// Markdown hybrid rendering enhancement for TextArea
// Shows current line as plain text, all other lines as formatted markdown
// Version: 2.4.3
// Last Modified: 2026-04-06
// Author: UltraCanvas Framework

#include "UltraCanvasTextArea.h"
#include "UltraCanvasSyntaxTokenizer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasUtilsUtf8.h"
#include "UltraCanvasTooltipManager.h"
#include "Plugins/Text/UltraCanvasMarkdown.h"
#include "UltraCanvasConfig.h"
#include <algorithm>
#include <sstream>
#include <cmath>
#include <regex>
#include <functional>
#include <memory>
#include <unordered_map>

namespace UltraCanvas {

// ===== MARKDOWN HYBRID STYLE CONFIGURATION =====
// Configurable styling for markdown rendering within TextArea
// Mirrors relevant properties from MarkdownStyle in UltraCanvasMarkdown.h

struct MarkdownHybridStyle {
    // Header colors per level (H1-H6)
    std::array<Color, 6> headerColors = {
            Color(20, 20, 20), Color(30, 30, 30), Color(40, 40, 40),
            Color(50, 50, 50), Color(60, 60, 60), Color(70, 70, 70)
    };

    // Header font size multipliers (relative to base font size)
    std::array<float, 6> headerSizeMultipliers = {2.0f, 1.5f, 1.3f, 1.2f, 1.1f, 1.0f};

    // Code styling
    Color codeTextColor = Color(200, 50, 50);
    Color codeBackgroundColor = Color(245, 245, 245);
    Color codeBlockBackgroundColor = Color(248, 248, 248);
    Color codeBlockBorderColor = Color(120, 120, 120);
    Color codeBlockTextColor = Color(50, 50, 50);       // Default text color inside code blocks
    Color codeBlockCommentColor = Color(106, 153, 85);   // Green for comments in code blocks
    Color codeBlockKeywordColor = Color(0, 0, 200);      // Blue for keywords in code blocks
    Color codeBlockStringColor = Color(163, 21, 21);     // Red for strings in code blocks
    Color codeBlockNumberColor = Color(9, 134, 88);      // Green for numbers in code blocks
    std::string codeFont = "Courier New";

    // Link styling
    Color linkColor = Color(0, 102, 204);
    Color linkHoverColor = Color(0, 80, 160);
    bool linkUnderline = true;

    // List styling
    int listIndent = 20;
    std::string bulletCharacter = "\xe2\x80\xa2"; // UTF-8 bullet • (kept for compatibility)
    // Nested bullet characters per nesting level (0=•, 1=◦, 2=▪, deeper levels repeat ▪)
    std::array<std::string, 3> nestedBulletCharacters = {
        "\xe2\x80\xa2",         // level 0: • (U+2022 BULLET)
        "\xe2\x97\xa6",         // level 1: ◦ (U+25E6 WHITE BULLET)
        "\xe2\x96\xaa"          // level 2: ▪ (U+25AA BLACK SMALL SQUARE)
    };
    Color bulletColor = Color(80, 80, 80);

    // Quote styling
    Color quoteBarColor = Color(200, 200, 200);
    Color quoteBackgroundColor = Color(250, 250, 250);
    Color quoteTextColor = Color(100, 100, 100);
    int quoteBarWidth = 3;          // Thin bar — matches reference visual
    int quoteBarGap = 10;           // Gap between bar and text (new)
    int quoteIndent = 26;           // Total indent from bar left edge to text
    int quoteNestingStep = 20;      // Horizontal step per nesting level
    
    // Horizontal rule
    Color horizontalRuleColor = Color(200, 200, 200);
    float horizontalRuleHeight = 2.0f;
    // Vertical inset for block backgrounds (code blocks, blockquotes)
    // Top border starts this many pixels lower on the first line of a block,
    // bottom border ends this many pixels earlier on the last line.
    int blockVerticalInset = 5;

    // Table styling
    Color tableBorderColor = Color(200, 200, 200);
    Color tableHeaderBackground = Color(240, 240, 240);
    float tableCellPadding = 4.0f;

    // Strikethrough
    Color strikethroughColor = Color(120, 120, 120);

    // Highlight
    Color highlightBackground = Color(255, 255, 0, 100);

    // Abbreviation styling
    Color abbreviationBackground = Color(230, 230, 230, 120);
    Color abbreviationUnderlineColor = Color(150, 150, 150);
    float abbreviationUnderlineDashLength = 2.0f;
    float abbreviationUnderlineGapLength = 2.0f;

    // Footnote reference styling
    Color footnoteRefColor = Color(0, 102, 204);

    // Task list checkbox
    Color checkboxBorderColor = Color(150, 150, 150);
    Color checkboxCheckedColor = Color(0, 120, 215);
    Color checkboxCheckmarkColor = Color(255, 255, 255);
    int checkboxSize = 12;

    // Image placeholder
    Color imagePlaceholderBackground = Color(230, 230, 240);
    Color imagePlaceholderBorderColor = Color(180, 180, 200);
    Color imagePlaceholderTextColor = Color(100, 100, 140);

    // Math formula styling
    Color mathTextColor = Color(0, 120, 60);
    Color mathBackgroundColor = Color(240, 248, 240, 180);

    static MarkdownHybridStyle Default() {
        return MarkdownHybridStyle();
    }

    static MarkdownHybridStyle DarkMode() {
        MarkdownHybridStyle s;
        s.headerColors = {
                Color(240, 240, 240), Color(220, 220, 240), Color(200, 200, 220),
                Color(190, 190, 210), Color(180, 180, 200), Color(170, 170, 190)
        };
        s.codeTextColor = Color(255, 150, 150);
        s.codeBackgroundColor = Color(50, 50, 50);
        s.codeBlockBackgroundColor = Color(40, 40, 40);
        s.codeBlockBorderColor = Color(80, 80, 80);
        s.codeBlockTextColor = Color(210, 210, 210);
        s.codeBlockCommentColor = Color(106, 153, 85);
        s.codeBlockKeywordColor = Color(86, 156, 214);
        s.codeBlockStringColor = Color(206, 145, 120);
        s.codeBlockNumberColor = Color(181, 206, 168);
        s.linkColor = Color(100, 180, 255);
        s.linkHoverColor = Color(150, 200, 255);
        s.quoteBarColor = Color(100, 100, 100);
        s.quoteBackgroundColor = Color(40, 40, 40);
        s.quoteTextColor = Color(170, 170, 170);
        s.horizontalRuleColor = Color(100, 100, 100);
        s.tableBorderColor = Color(80, 80, 80);
        s.tableHeaderBackground = Color(50, 50, 50);
        s.strikethroughColor = Color(170, 170, 170);
        s.highlightBackground = Color(100, 100, 0, 100);
        s.bulletColor = Color(170, 170, 170);
        s.checkboxBorderColor = Color(120, 120, 120);
        s.imagePlaceholderBackground = Color(50, 50, 60);
        s.imagePlaceholderBorderColor = Color(80, 80, 100);
        s.imagePlaceholderTextColor = Color(140, 140, 170);
        s.mathTextColor = Color(100, 220, 140);
        s.mathBackgroundColor = Color(30, 50, 35, 150);
        s.abbreviationBackground = Color(70, 70, 80, 120);
        s.abbreviationUnderlineColor = Color(140, 140, 160);
        s.footnoteRefColor = Color(100, 180, 255);
        s.nestedBulletCharacters = {
            "\xe2\x80\xa2",     // level 0: •
            "\xe2\x97\xa6",     // level 1: ◦
            "\xe2\x96\xaa"      // level 2: ▪
        };        
        return s;
    }
};


// ===== MARKDOWN INLINE ELEMENT =====
// Parsed inline element with formatting state

struct MarkdownInlineElement {
    std::string text;
    bool isBold = false;
    bool isItalic = false;
    bool isCode = false;
    bool isStrikethrough = false;
    bool isHighlight = false;
    bool isLink = false;
    bool isImage = false;
    bool isMath = false;
    bool isMathBlock = false; // $$ block math vs $ inline math
    bool isSubscript = false;   // ~x~ single tilde
    bool isSuperscript = false; // ^x^ single caret
    bool isAutoLink = false;    // bare http:// or https:// URL
    bool isEmoji = false;       // :shortcode: emoji
    bool isAbbreviation = false; // matched abbreviation with tooltip
    bool isFootnote = false;     // footnote reference or inline footnote
    std::string url;
    std::string altText;
    std::string abbreviationExpansion; // tooltip text for abbreviations
    std::string footnoteContent;      // tooltip text for footnotes
};

// ===== HEADING ANCHOR UTILITIES =====
// Generate a URL-friendly slug from heading text (strips inline formatting)

static std::string StripInlineFormatting(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        // Skip formatting markers: **, *, ~~, ==, ^^, ~~, ``
        if (i + 1 < text.size()) {
            std::string two = text.substr(i, 2);
            if (two == "**" || two == "~~" || two == "==" || two == "__") {
                i += 2;
                continue;
            }
        }
        char c = text[i];
        if (c == '*' || c == '_' || c == '`' || c == '^' || c == '~') {
            i++;
            continue;
        }
        result += c;
        i++;
    }
    return result;
}

static std::string GenerateHeadingSlug(const std::string& headingText) {
    std::string stripped = StripInlineFormatting(headingText);
    std::string slug;
    slug.reserve(stripped.size());
    for (unsigned char c : stripped) {
        if (std::isalnum(c)) {
            slug += static_cast<char>(std::tolower(c));
        } else if (c == ' ' || c == '-' || c == '_') {
            if (!slug.empty() && slug.back() != '-') {
                slug += '-';
            }
        }
        // skip other characters
    }
    // Trim trailing dash
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug;
}

// Strip explicit anchor {#id} from end of heading text, return the anchor ID (or empty)
static std::string StripExplicitAnchor(std::string& headerText) {
    // Look for {#...} at end of text (possibly with trailing whitespace)
    size_t braceOpen = headerText.rfind('{');
    if (braceOpen != std::string::npos && braceOpen + 2 < headerText.size() && headerText[braceOpen + 1] == '#') {
        size_t braceClose = headerText.find('}', braceOpen);
        if (braceClose != std::string::npos) {
            // Check that everything after braceClose is whitespace
            bool trailingOk = true;
            for (size_t k = braceClose + 1; k < headerText.size(); k++) {
                if (headerText[k] != ' ' && headerText[k] != '\t') {
                    trailingOk = false;
                    break;
                }
            }
            if (trailingOk) {
                std::string anchorId = headerText.substr(braceOpen + 2, braceClose - braceOpen - 2);
                headerText = headerText.substr(0, braceOpen);
                // Trim trailing whitespace from header text
                while (!headerText.empty() && (headerText.back() == ' ' || headerText.back() == '\t'))
                    headerText.pop_back();
                return anchorId;
            }
        }
    }
    return {};
}

// ===== MATH / GREEK LETTER SUBSTITUTION =====
// Converts LaTeX-style commands to Unicode characters for display

static std::string SubstituteGreekLetters(const std::string& input) {
    // Map of LaTeX commands to UTF-8 Unicode characters.
    // Sorted by pattern length (longest first) at init time to prevent
    // shorter patterns from false-matching as prefixes of longer ones.
    static const std::vector<std::pair<std::string, std::string>> greekMap = []() {
        std::vector<std::pair<std::string, std::string>> map = {
            // Lowercase Greek
            {"\\alpha",    "\xCE\xB1"},   // α
            {"\\beta",     "\xCE\xB2"},   // β
            {"\\gamma",    "\xCE\xB3"},   // γ
            {"\\delta",    "\xCE\xB4"},   // δ
            {"\\epsilon",  "\xCE\xB5"},   // ε
            {"\\varepsilon","\xCE\xB5"},  // ε (variant)
            {"\\zeta",     "\xCE\xB6"},   // ζ
            {"\\eta",      "\xCE\xB7"},   // η
            {"\\theta",    "\xCE\xB8"},   // θ
            {"\\vartheta", "\xCF\x91"},   // ϑ (variant)
            {"\\iota",     "\xCE\xB9"},   // ι
            {"\\kappa",    "\xCE\xBA"},   // κ
            {"\\lambda",   "\xCE\xBB"},   // λ
            {"\\mu",       "\xCE\xBC"},   // μ
            {"\\nu",       "\xCE\xBD"},   // ν
            {"\\xi",       "\xCE\xBE"},   // ξ
            {"\\pi",       "\xCF\x80"},   // π
            {"\\rho",      "\xCF\x81"},   // ρ
            {"\\sigma",    "\xCF\x83"},   // σ
            {"\\tau",      "\xCF\x84"},   // τ
            {"\\upsilon",  "\xCF\x85"},   // υ
            {"\\phi",      "\xCF\x86"},   // φ
            {"\\varphi",   "\xCF\x86"},   // φ (variant)
            {"\\chi",      "\xCF\x87"},   // χ
            {"\\psi",      "\xCF\x88"},   // ψ
            {"\\omega",    "\xCF\x89"},   // ω
            // Uppercase Greek
            {"\\Alpha",    "\xCE\x91"},   // Α
            {"\\Beta",     "\xCE\x92"},   // Β
            {"\\Gamma",    "\xCE\x93"},   // Γ
            {"\\Delta",    "\xCE\x94"},   // Δ
            {"\\Epsilon",  "\xCE\x95"},   // Ε
            {"\\Zeta",     "\xCE\x96"},   // Ζ
            {"\\Eta",      "\xCE\x97"},   // Η
            {"\\Theta",    "\xCE\x98"},   // Θ
            {"\\Iota",     "\xCE\x99"},   // Ι
            {"\\Kappa",    "\xCE\x9A"},   // Κ
            {"\\Lambda",   "\xCE\x9B"},   // Λ
            {"\\Mu",       "\xCE\x9C"},   // Μ
            {"\\Nu",       "\xCE\x9D"},   // Ν
            {"\\Xi",       "\xCE\x9E"},   // Ξ
            {"\\Pi",       "\xCE\xA0"},   // Π
            {"\\Rho",      "\xCE\xA1"},   // Ρ
            {"\\Sigma",    "\xCE\xA3"},   // Σ
            {"\\Tau",      "\xCE\xA4"},   // Τ
            {"\\Upsilon",  "\xCE\xA5"},   // Υ
            {"\\Phi",      "\xCE\xA6"},   // Φ
            {"\\Chi",      "\xCE\xA7"},   // Χ
            {"\\Psi",      "\xCE\xA8"},   // Ψ
            {"\\Omega",    "\xCE\xA9"},   // Ω
            // Math symbols
            {"\\infty",    "\xE2\x88\x9E"},   // ∞
            {"\\pm",       "\xC2\xB1"},       // ±
            {"\\mp",       "\xE2\x88\x93"},   // ∓
            {"\\times",    "\xC3\x97"},       // ×
            {"\\div",      "\xC3\xB7"},       // ÷
            {"\\cdot",     "\xC2\xB7"},       // ·
            {"\\cdots",    "\xE2\x8B\xAF"},   // ⋯
            {"\\ldots",    "\xE2\x80\xA6"},   // …
            {"\\leq",      "\xE2\x89\xA4"},   // ≤
            {"\\geq",      "\xE2\x89\xA5"},   // ≥
            {"\\neq",      "\xE2\x89\xA0"},   // ≠
            {"\\le",       "\xE2\x89\xA4"},   // ≤ (short alias)
            {"\\ge",       "\xE2\x89\xA5"},   // ≥ (short alias)
            {"\\ne",       "\xE2\x89\xA0"},   // ≠ (short alias)
            {"\\approx",   "\xE2\x89\x88"},   // ≈
            {"\\equiv",    "\xE2\x89\xA1"},   // ≡
            {"\\sum",      "\xE2\x88\x91"},   // ∑
            {"\\prod",     "\xE2\x88\x8F"},   // ∏
            {"\\int",      "\xE2\x88\xAB"},   // ∫
            {"\\partial",  "\xE2\x88\x82"},   // ∂
            {"\\nabla",    "\xE2\x88\x87"},   // ∇
            {"\\forall",   "\xE2\x88\x80"},   // ∀
            {"\\exists",   "\xE2\x88\x83"},   // ∃
            {"\\in",       "\xE2\x88\x88"},   // ∈
            {"\\notin",    "\xE2\x88\x89"},   // ∉
            {"\\subset",   "\xE2\x8A\x82"},   // ⊂
            {"\\supset",   "\xE2\x8A\x83"},   // ⊃
            {"\\cup",      "\xE2\x88\xAA"},   // ∪
            {"\\cap",      "\xE2\x88\xA9"},   // ∩
            {"\\emptyset", "\xE2\x88\x85"},   // ∅
            {"\\sqrt",     "\xE2\x88\x9A"},   // √
            {"\\langle",   "\xE2\x9F\xA8"},   // ⟨
            {"\\rangle",   "\xE2\x9F\xA9"},   // ⟩
            {"\\to",       "\xE2\x86\x92"},   // →
            {"\\gets",     "\xE2\x86\x90"},   // ←
            {"\\leftarrow","\xE2\x86\x90"},   // ←
            {"\\Rightarrow","\xE2\x87\x92"},  // ⇒
            {"\\Leftarrow", "\xE2\x87\x90"},  // ⇐
            {"\\iff",      "\xE2\x9F\xBA"},   // ⟺
            {"\\implies",  "\xE2\x9F\xB9"},   // ⟹
            // Named math functions (render as upright text without backslash)
            {"\\sin",  "sin"},
            {"\\cos",  "cos"},
            {"\\tan",  "tan"},
            {"\\log",  "log"},
            {"\\ln",   "ln"},
            {"\\lim",  "lim"},
            {"\\min",  "min"},
            {"\\max",  "max"},
            {"\\exp",  "exp"},
            {"\\det",  "det"},
            {"\\dim",  "dim"},
            {"\\ker",  "ker"},
            {"\\deg",  "deg"},
            // Superscripts and subscripts
            {"^{0}",  "\xE2\x81\xB0"},   // ⁰
            {"^{1}",  "\xC2\xB9"},       // ¹
            {"^{2}",  "\xC2\xB2"},       // ²
            {"^{3}",  "\xC2\xB3"},       // ³
            {"^{4}",  "\xE2\x81\xB4"},   // ⁴
            {"^{5}",  "\xE2\x81\xB5"},   // ⁵
            {"^{6}",  "\xE2\x81\xB6"},   // ⁶
            {"^{7}",  "\xE2\x81\xB7"},   // ⁷
            {"^{8}",  "\xE2\x81\xB8"},   // ⁸
            {"^{9}",  "\xE2\x81\xB9"},   // ⁹
            {"^{n}",  "\xE2\x81\xBF"},   // ⁿ
            {"^{i}",  "\xE2\x81\xB1"},   // ⁱ
            {"_{0}",  "\xE2\x82\x80"},   // ₀
            {"_{1}",  "\xE2\x82\x81"},   // ₁
            {"_{2}",  "\xE2\x82\x82"},   // ₂
            {"_{3}",  "\xE2\x82\x83"},   // ₃
            {"_{4}",  "\xE2\x82\x84"},   // ₄
            {"_{5}",  "\xE2\x82\x85"},   // ₅
            {"_{6}",  "\xE2\x82\x86"},   // ₆
            {"_{7}",  "\xE2\x82\x87"},   // ₇
            {"_{8}",  "\xE2\x82\x88"},   // ₈
            {"_{9}",  "\xE2\x82\x89"},   // ₉
            // Fractions (known Unicode fraction chars)
            {"\\frac{1}{2}", "\xC2\xBD"},       // ½
            {"\\frac{1}{3}", "\xE2\x85\x93"},   // ⅓
            {"\\frac{2}{3}", "\xE2\x85\x94"},   // ⅔
            {"\\frac{1}{4}", "\xC2\xBC"},       // ¼
            {"\\frac{3}{4}", "\xC2\xBE"},       // ¾
        };
        // Sort by pattern length descending so longer patterns match first
        std::stable_sort(map.begin(), map.end(), [](const auto& a, const auto& b) {
            return a.first.length() > b.first.length();
        });
        return map;
    }();

    std::string result = input;

    // Replace patterns longest-first with word-boundary guard for backslash commands
    for (const auto& [pattern, replacement] : greekMap) {
        bool isBackslashCmd = (!pattern.empty() && pattern[0] == '\\');
        size_t searchPos = 0;
        while ((searchPos = result.find(pattern, searchPos)) != std::string::npos) {
            size_t afterMatch = searchPos + pattern.length();
            // Word boundary: for \cmd patterns, next char must not be a letter
            // (prevents e.g. \in matching inside \integral)
            if (isBackslashCmd && afterMatch < result.length() &&
                std::isalpha(static_cast<unsigned char>(result[afterMatch]))) {
                searchPos++;
                continue;
            }
            result.replace(searchPos, pattern.length(), replacement);
            searchPos += replacement.length();
        }
    }

    // Clean up remaining LaTeX formatting that we can simplify:

    // \frac{X}{Y} → X/Y (general fractions not in the known-fraction table)
    {
        size_t searchPos = 0;
        while ((searchPos = result.find("\\frac{", searchPos)) != std::string::npos) {
            size_t numEnd = result.find('}', searchPos + 6);
            if (numEnd != std::string::npos && numEnd + 1 < result.length() && result[numEnd + 1] == '{') {
                size_t denEnd = result.find('}', numEnd + 2);
                if (denEnd != std::string::npos) {
                    std::string num = result.substr(searchPos + 6, numEnd - searchPos - 6);
                    std::string den = result.substr(numEnd + 2, denEnd - numEnd - 2);
                    std::string frac = num + "/" + den;
                    result.replace(searchPos, denEnd - searchPos + 1, frac);
                    searchPos += frac.length();
                    continue;
                }
            }
            break;
        }
    }

    // \text{...} → just the text
    {
        size_t searchPos = 0;
        while ((searchPos = result.find("\\text{", searchPos)) != std::string::npos) {
            size_t braceEnd = result.find('}', searchPos + 6);
            if (braceEnd != std::string::npos) {
                std::string content = result.substr(searchPos + 6, braceEnd - searchPos - 6);
                result.replace(searchPos, braceEnd - searchPos + 1, content);
                searchPos += content.length();
            } else {
                break;
            }
        }
    }

    // \mathbf{...} → just the text (bold will be handled by renderer)
    {
        size_t searchPos = 0;
        while ((searchPos = result.find("\\mathbf{", searchPos)) != std::string::npos) {
            size_t braceEnd = result.find('}', searchPos + 8);
            if (braceEnd != std::string::npos) {
                std::string content = result.substr(searchPos + 8, braceEnd - searchPos - 8);
                result.replace(searchPos, braceEnd - searchPos + 1, content);
                searchPos += content.length();
            } else {
                break;
            }
        }
    }

    // \mathrm{...} → just the text
    {
        size_t searchPos = 0;
        while ((searchPos = result.find("\\mathrm{", searchPos)) != std::string::npos) {
            size_t braceEnd = result.find('}', searchPos + 8);
            if (braceEnd != std::string::npos) {
                std::string content = result.substr(searchPos + 8, braceEnd - searchPos - 8);
                result.replace(searchPos, braceEnd - searchPos + 1, content);
                searchPos += content.length();
            } else {
                break;
            }
        }
    }

    // \sqrt{...} → √(...)
    {
        size_t searchPos = 0;
        while ((searchPos = result.find("\xE2\x88\x9A{", searchPos)) != std::string::npos) {
            // \sqrt was already replaced with √, now handle the {content}
            size_t braceEnd = result.find('}', searchPos + 4); // 3 bytes for √ + 1 for {
            if (braceEnd != std::string::npos) {
                std::string content = result.substr(searchPos + 4, braceEnd - searchPos - 4);
                std::string replacement = "\xE2\x88\x9A(" + content + ")";
                result.replace(searchPos, braceEnd - searchPos + 1, replacement);
                searchPos += replacement.length();
            } else {
                break;
            }
        }
    }

    // \left and \right delimiters — just remove them
    {
        size_t searchPos = 0;
        while ((searchPos = result.find("\\left", searchPos)) != std::string::npos) {
            result.erase(searchPos, 5);
        }
    }
    {
        size_t searchPos = 0;
        while ((searchPos = result.find("\\right", searchPos)) != std::string::npos) {
            result.erase(searchPos, 6);
        }
    }

    return result;
}

// ===== EMOJI SHORTCODE LOOKUP =====
// Maps common :shortcode: names to their UTF-8 Unicode emoji characters

static std::string LookupEmojiShortcode(const std::string& code) {
    // Common emoji shortcodes — covers the most frequently used set
    static const std::unordered_map<std::string, std::string> emojiMap = {
        // Smileys & People
        {"smile",           "\xF0\x9F\x98\x84"},   // 😄
        {"laughing",        "\xF0\x9F\x98\x86"},   // 😆
        {"blush",           "\xF0\x9F\x98\x8A"},   // 😊
        {"smiley",          "\xF0\x9F\x98\x83"},   // 😃
        {"relaxed",         "\xE2\x98\xBA"},        // ☺
        {"smirk",           "\xF0\x9F\x98\x8F"},   // 😏
        {"heart_eyes",      "\xF0\x9F\x98\x8D"},   // 😍
        {"kissing_heart",   "\xF0\x9F\x98\x98"},   // 😘
        {"wink",            "\xF0\x9F\x98\x89"},   // 😉
        {"grinning",        "\xF0\x9F\x98\x80"},   // 😀
        {"grin",            "\xF0\x9F\x98\x81"},   // 😁
        {"thinking",        "\xF0\x9F\xA4\x94"},   // 🤔
        {"joy",             "\xF0\x9F\x98\x82"},   // 😂
        {"rofl",            "\xF0\x9F\xA4\xA3"},   // 🤣
        {"sweat_smile",     "\xF0\x9F\x98\x85"},   // 😅
        {"sob",             "\xF0\x9F\x98\xAD"},   // 😭
        {"cry",             "\xF0\x9F\x98\xA2"},   // 😢
        {"angry",           "\xF0\x9F\x98\xA0"},   // 😠
        {"rage",            "\xF0\x9F\x98\xA1"},   // 😡
        {"scream",          "\xF0\x9F\x98\xB1"},   // 😱
        {"sunglasses",      "\xF0\x9F\x98\x8E"},   // 😎
        {"confused",        "\xF0\x9F\x98\x95"},   // 😕
        {"disappointed",    "\xF0\x9F\x98\x9E"},   // 😞
        {"worried",         "\xF0\x9F\x98\x9F"},   // 😟
        {"hushed",          "\xF0\x9F\x98\xAF"},   // 😯
        {"astonished",      "\xF0\x9F\x98\xB2"},   // 😲
        {"sleeping",        "\xF0\x9F\x98\xB4"},   // 😴
        {"mask",            "\xF0\x9F\x98\xB7"},   // 😷
        {"nerd_face",       "\xF0\x9F\xA4\x93"},   // 🤓
        {"stuck_out_tongue","\xF0\x9F\x98\x9B"},   // 😛
        {"yum",             "\xF0\x9F\x98\x8B"},   // 😋
        {"clown_face",      "\xF0\x9F\xA4\xA1"},   // 🤡
        {"skull",           "\xF0\x9F\x92\x80"},   // 💀
        {"ghost",           "\xF0\x9F\x91\xBB"},   // 👻
        {"alien",           "\xF0\x9F\x91\xBD"},   // 👽
        {"robot",           "\xF0\x9F\xA4\x96"},   // 🤖
        // Gestures & Body
        {"thumbsup",        "\xF0\x9F\x91\x8D"},   // 👍
        {"+1",              "\xF0\x9F\x91\x8D"},   // 👍
        {"thumbsdown",      "\xF0\x9F\x91\x8E"},   // 👎
        {"-1",              "\xF0\x9F\x91\x8E"},   // 👎
        {"wave",            "\xF0\x9F\x91\x8B"},   // 👋
        {"clap",            "\xF0\x9F\x91\x8F"},   // 👏
        {"raised_hands",    "\xF0\x9F\x99\x8C"},   // 🙌
        {"pray",            "\xF0\x9F\x99\x8F"},   // 🙏
        {"handshake",       "\xF0\x9F\xA4\x9D"},   // 🤝
        {"muscle",          "\xF0\x9F\x92\xAA"},   // 💪
        {"point_up",        "\xE2\x98\x9D"},        // ☝
        {"point_down",      "\xF0\x9F\x91\x87"},   // 👇
        {"point_left",      "\xF0\x9F\x91\x88"},   // 👈
        {"point_right",     "\xF0\x9F\x91\x89"},   // 👉
        {"ok_hand",         "\xF0\x9F\x91\x8C"},   // 👌
        {"v",               "\xE2\x9C\x8C"},        // ✌
        {"eyes",            "\xF0\x9F\x91\x80"},   // 👀
        // Hearts & Symbols
        {"heart",           "\xE2\x9D\xA4"},        // ❤
        {"broken_heart",    "\xF0\x9F\x92\x94"},   // 💔
        {"star",            "\xE2\xAD\x90"},        // ⭐
        {"sparkles",        "\xE2\x9C\xA8"},        // ✨
        {"fire",            "\xF0\x9F\x94\xA5"},   // 🔥
        {"100",             "\xF0\x9F\x92\xAF"},   // 💯
        {"boom",            "\xF0\x9F\x92\xA5"},   // 💥
        {"zap",             "\xE2\x9A\xA1"},        // ⚡
        {"warning",         "\xE2\x9A\xA0"},        // ⚠
        {"x",               "\xE2\x9D\x8C"},        // ❌
        {"white_check_mark","\xE2\x9C\x85"},       // ✅
        {"heavy_check_mark","\xE2\x9C\x94"},       // ✔
        {"ballot_box_with_check", "\xE2\x98\x91"},  // ☑
        {"question",        "\xE2\x9D\x93"},        // ❓
        {"exclamation",     "\xE2\x9D\x97"},        // ❗
        {"bulb",            "\xF0\x9F\x92\xA1"},   // 💡
        {"mega",            "\xF0\x9F\x93\xA3"},   // 📣
        {"bell",            "\xF0\x9F\x94\x94"},   // 🔔
        {"bookmark",        "\xF0\x9F\x94\x96"},   // 🔖
        {"link",            "\xF0\x9F\x94\x97"},   // 🔗
        {"key",             "\xF0\x9F\x94\x91"},   // 🔑
        {"lock",            "\xF0\x9F\x94\x92"},   // 🔒
        // Nature & Weather
        {"sunny",           "\xE2\x98\x80"},        // ☀
        {"cloud",           "\xE2\x98\x81"},        // ☁
        {"umbrella",        "\xE2\x98\x82"},        // ☂
        {"snowflake",       "\xE2\x9D\x84"},        // ❄
        {"rainbow",         "\xF0\x9F\x8C\x88"},   // 🌈
        {"earth_americas",  "\xF0\x9F\x8C\x8E"},   // 🌎
        {"seedling",        "\xF0\x9F\x8C\xB1"},   // 🌱
        {"evergreen_tree",  "\xF0\x9F\x8C\xB2"},   // 🌲
        {"fallen_leaf",     "\xF0\x9F\x8D\x82"},   // 🍂
        {"rose",            "\xF0\x9F\x8C\xB9"},   // 🌹
        {"sunflower",       "\xF0\x9F\x8C\xBB"},   // 🌻
        // Food & Drink
        {"coffee",          "\xE2\x98\x95"},        // ☕
        {"beer",            "\xF0\x9F\x8D\xBA"},   // 🍺
        {"wine_glass",      "\xF0\x9F\x8D\xB7"},   // 🍷
        {"pizza",           "\xF0\x9F\x8D\x95"},   // 🍕
        {"hamburger",       "\xF0\x9F\x8D\x94"},   // 🍔
        {"cake",            "\xF0\x9F\x8E\x82"},   // 🎂
        {"apple",           "\xF0\x9F\x8D\x8E"},   // 🍎
        // Activities & Objects
        {"tent",            "\xE2\x9B\xBA"},        // ⛺
        {"camping",         "\xF0\x9F\x8F\x95"},   // 🏕
        {"rocket",          "\xF0\x9F\x9A\x80"},   // 🚀
        {"trophy",          "\xF0\x9F\x8F\x86"},   // 🏆
        {"medal",           "\xF0\x9F\x8F\x85"},   // 🏅
        {"tada",            "\xF0\x9F\x8E\x89"},   // 🎉
        {"confetti_ball",   "\xF0\x9F\x8E\x8A"},   // 🎊
        {"gift",            "\xF0\x9F\x8E\x81"},   // 🎁
        {"balloon",         "\xF0\x9F\x8E\x88"},   // 🎈
        {"art",             "\xF0\x9F\x8E\xA8"},   // 🎨
        {"musical_note",    "\xF0\x9F\x8E\xB5"},   // 🎵
        {"guitar",          "\xF0\x9F\x8E\xB8"},   // 🎸
        {"video_game",      "\xF0\x9F\x8E\xAE"},   // 🎮
        {"soccer",          "\xE2\x9A\xBD"},        // ⚽
        {"basketball",      "\xF0\x9F\x8F\x80"},   // 🏀
        // Objects & Tech
        {"computer",        "\xF0\x9F\x92\xBB"},   // 💻
        {"phone",           "\xF0\x9F\x93\xB1"},   // 📱
        {"email",           "\xF0\x9F\x93\xA7"},   // 📧
        {"memo",            "\xF0\x9F\x93\x9D"},   // 📝
        {"book",            "\xF0\x9F\x93\x96"},   // 📖
        {"pencil",          "\xE2\x9C\x8F"},        // ✏
        {"pencil2",         "\xE2\x9C\x8F"},        // ✏
        {"wrench",          "\xF0\x9F\x94\xA7"},   // 🔧
        {"hammer",          "\xF0\x9F\x94\xA8"},   // 🔨
        {"gear",            "\xE2\x9A\x99"},        // ⚙
        {"package",         "\xF0\x9F\x93\xA6"},   // 📦
        {"chart_with_upwards_trend", "\xF0\x9F\x93\x88"}, // 📈
        {"mag",             "\xF0\x9F\x94\x8D"},   // 🔍
        {"clipboard",       "\xF0\x9F\x93\x8B"},   // 📋
        {"pushpin",         "\xF0\x9F\x93\x8C"},   // 📌
        // Arrows & Misc
        {"arrow_right",     "\xE2\x9E\xA1"},        // ➡
        {"arrow_left",      "\xE2\xAC\x85"},        // ⬅
        {"arrow_up",        "\xE2\xAC\x86"},        // ⬆
        {"arrow_down",      "\xE2\xAC\x87"},        // ⬇
        {"recycle",         "\xE2\x99\xBB"},        // ♻
        {"copyright",       "\xC2\xA9"},            // ©
        {"registered",      "\xC2\xAE"},            // ®
        {"tm",              "\xE2\x84\xA2"},        // ™
        {"info",            "\xE2\x84\xB9"},        // ℹ
    };

    auto it = emojiMap.find(code);
    if (it != emojiMap.end()) {
        return it->second;
    }
    return ""; // Unknown shortcode — return empty
}
// Parsed table column alignment and widths

enum class TableColumnAlignment {
    Left,
    Center,
    Right
};

struct TableParseResult {
    std::vector<std::string> cells;
    bool isValid = false;
};

// ===== MARKDOWN INLINE RENDERER =====
// Renders markdown inline formatting directly in the text area

struct MarkdownInlineRenderer {

    // ---------------------------------------------------------------
    // BRACKET / PAREN MATCHING HELPERS
    // ---------------------------------------------------------------

    // Find the matching ']' for a '[' at openPos, respecting nesting and escapes.
    static size_t FindMatchingBracket(const std::string& line, size_t openPos) {
        int depth = 1;
        for (size_t i = openPos + 1; i < line.length(); i++) {
            if (line[i] == '\\' && i + 1 < line.length()) { i++; continue; }
            if (line[i] == '[') depth++;
            if (line[i] == ']') { depth--; if (depth == 0) return i; }
        }
        return std::string::npos;
    }

    // Find the matching ')' for a '(' at openPos, respecting nesting and escapes.
    static size_t FindMatchingParen(const std::string& line, size_t openPos) {
        int depth = 1;
        for (size_t i = openPos + 1; i < line.length(); i++) {
            if (line[i] == '\\' && i + 1 < line.length()) { i++; continue; }
            if (line[i] == '(') depth++;
            if (line[i] == ')') { depth--; if (depth == 0) return i; }
        }
        return std::string::npos;
    }

    // ---------------------------------------------------------------
    // INLINE PARSER — handles bold, italic, bold+italic, code,
    //                 strikethrough, highlight, links, images
    // ---------------------------------------------------------------

    static std::vector<MarkdownInlineElement> ParseInlineMarkdown(const std::string& lineRaw) {
        std::vector<MarkdownInlineElement> elements;

        // --- Typographic & emoticon pre-pass ---
        static const std::vector<std::pair<std::string, std::string>> typographicMap = {
                {"(c)",  "\xC2\xA9"},          // ©
                {"(C)",  "\xC2\xA9"},          // ©
                {"(r)",  "\xC2\xAE"},          // ®
                {"(R)",  "\xC2\xAE"},          // ®
                {"(tm)", "\xE2\x84\xA2"},      // ™
                {"(TM)", "\xE2\x84\xA2"},      // ™
                {"(p)",  "\xE2\x84\x97"},      // ℗
                {"(P)",  "\xE2\x84\x97"},      // ℗
                {":-)",  "\xF0\x9F\x98\x83"},  // 😃
                {":)",   "\xF0\x9F\x98\x83"},  // 😃
                {":-(",  "\xF0\x9F\x98\xA2"},  // 😢
                {":(",   "\xF0\x9F\x98\xA2"},  // 😢
                {":-D",  "\xF0\x9F\x98\x86"},  // 😆
                {":D",   "\xF0\x9F\x98\x86"},  // 😆
                {";-)",  "\xF0\x9F\x98\x89"},  // 😉
                {";)",   "\xF0\x9F\x98\x89"},  // 😉
                {"8-)",  "\xF0\x9F\x98\x8E"},  // 😎
                {":-P",  "\xF0\x9F\x98\x9B"},  // 😛
                {":P",   "\xF0\x9F\x98\x9B"},  // 😛
                {":-|",  "\xF0\x9F\x98\x90"},  // 😐
                {":-/",  "\xF0\x9F\x98\x95"},  // 😕
                {">:(",  "\xF0\x9F\x98\xA0"},  // 😠
                {":'(",  "\xF0\x9F\x98\xAD"},  // 😭
        };

        std::string processedLine = lineRaw;
        for (const auto& [pattern, replacement] : typographicMap) {
            size_t searchPos = 0;
            while ((searchPos = processedLine.find(pattern, searchPos)) != std::string::npos) {
                processedLine.replace(searchPos, pattern.length(), replacement);
                searchPos += replacement.length();
            }
        }

        const std::string& line = processedLine;  // loop body unchanged
        size_t pos = 0;
        size_t len = line.length();

        while (pos < len) {
            MarkdownInlineElement elem;
            bool parsed = false;

            // --- Backslash escaping: \* \_ \~ \` \[ \] \( \) \# \! \$ \^ etc. ---
            // When a backslash precedes a markdown special character, output the
            // character literally and skip any markdown interpretation
            if (line[pos] == '\\' && pos + 1 < len) {
                char next = line[pos + 1];
                // List of characters that can be escaped in markdown
                if (next == '*' || next == '_' || next == '~' || next == '`' ||
                    next == '[' || next == ']' || next == '(' || next == ')' ||
                    next == '#' || next == '!' || next == '$' || next == '^' ||
                    next == '|' || next == '=' || next == '-' || next == '.' ||
                    next == '+' || next == '{' || next == '}' || next == '\\' ||
                    next == '<' || next == '>' || next == ':') {
                    elem.text = std::string(1, next);
                    elements.push_back(elem);
                    pos += 2;
                    parsed = true;
                }
            }

            if (parsed) continue;

            // --- Block math: $$...$$ ---
            if (pos + 1 < len && line[pos] == '$' && line[pos + 1] == '$') {
                size_t end = line.find("$$", pos + 2);
                if (end != std::string::npos) {
                    elem.isMath = true;
                    elem.isMathBlock = true;
                    elem.text = SubstituteGreekLetters(line.substr(pos + 2, end - pos - 2));
                    elements.push_back(elem);
                    pos = end + 2;
                    parsed = true;
                }
            }

            if (parsed) continue;

            // --- Inline math: $...$ (single $, not $$) ---
            if (line[pos] == '$' && (pos + 1 >= len || line[pos + 1] != '$')) {
                size_t end = line.find('$', pos + 1);
                if (end != std::string::npos && end > pos + 1) {
                    // Make sure we don't match $$ (already handled above)
                    if (end + 1 >= len || line[end + 1] != '$') {
                        elem.isMath = true;
                        elem.isMathBlock = false;
                        elem.text = SubstituteGreekLetters(line.substr(pos + 1, end - pos - 1));
                        elements.push_back(elem);
                        pos = end + 1;
                        parsed = true;
                    }
                }
            }

            if (parsed) continue;

            // --- Image: ![alt](url) ---
            if (pos + 1 < len && line[pos] == '!' && line[pos + 1] == '[') {
                size_t altEnd = FindMatchingBracket(line, pos + 1);
                if (altEnd != std::string::npos && altEnd + 1 < len && line[altEnd + 1] == '(') {
                    size_t urlEnd = FindMatchingParen(line, altEnd + 1);
                    if (urlEnd != std::string::npos) {
                        elem.isImage = true;
                        elem.altText = line.substr(pos + 2, altEnd - pos - 2);
                        elem.url = line.substr(altEnd + 2, urlEnd - altEnd - 2);
                        elem.text = elem.altText.empty() ? "[image]" : elem.altText;
                        elements.push_back(elem);
                        pos = urlEnd + 1;
                        parsed = true;
                    }
                }
            }

            if (parsed) continue;

            // --- Link: [text](url) ---
            // Also handles footnote refs [^1] and reference links [text][ref]
            // by falling through gracefully
            if (line[pos] == '[') {
                size_t textEnd = FindMatchingBracket(line, pos);
                if (textEnd != std::string::npos && textEnd + 1 < len && line[textEnd + 1] == '(') {
                    size_t urlEnd = FindMatchingParen(line, textEnd + 1);
                    if (urlEnd != std::string::npos) {
                        elem.isLink = true;
                        elem.text = line.substr(pos + 1, textEnd - pos - 1);
                        elem.url = line.substr(textEnd + 2, urlEnd - textEnd - 2);
                        elements.push_back(elem);
                        pos = urlEnd + 1;
                        parsed = true;
                    }
                }

                // If [ was not part of a valid [text](url) link,
                // consume the entire [...] as plain text to avoid splitting
                if (!parsed) {
                    // Check if this is a footnote ref [^label] — mark as footnote for tooltip
                    if (textEnd != std::string::npos && pos + 1 < len && line[pos + 1] == '^') {
                        std::string label = line.substr(pos + 2, textEnd - pos - 2);
                        elem.isFootnote = true;
                        elem.text = "[" + label + "]";
                        // footnoteContent will be filled by ApplyFootnotes post-processor
                        elements.push_back(elem);
                        pos = textEnd + 1;
                        parsed = true;
                    } else if (textEnd != std::string::npos) {
                        // Non-link [text] — consume entire [...] as plain text
                        elem.text = line.substr(pos, textEnd - pos + 1);
                        elements.push_back(elem);
                        pos = textEnd + 1;
                        parsed = true;
                    } else {
                        // Unmatched [ — consume as plain text
                        elem.text = "[";
                        elements.push_back(elem);
                        pos++;
                        parsed = true;
                    }
                }
            }

            if (parsed) continue;

            // --- Inline code: `text` ---
            if (line[pos] == '`' && (pos + 1 >= len || line[pos + 1] != '`')) {
                size_t end = line.find('`', pos + 1);
                if (end != std::string::npos) {
                    elem.text = line.substr(pos + 1, end - pos - 1);
                    elem.isCode = true;
                    elements.push_back(elem);
                    pos = end + 1;
                    parsed = true;
                }
            }

            if (parsed) continue;

            // --- Bold+Italic: ***text*** or ___text___ ---
            if (pos + 2 < len) {
                std::string triple = line.substr(pos, 3);
                if (triple == "***" || triple == "___") {
                    char marker = triple[0];
                    std::string closeMarker(3, marker);
                    size_t end = line.find(closeMarker, pos + 3);
                    if (end != std::string::npos) {
                        std::string innerText = line.substr(pos + 3, end - pos - 3);
                        auto innerElements = ParseInlineMarkdown(innerText);
                        for (auto& inner : innerElements) {
                            inner.isBold = true;
                            inner.isItalic = true;
                            elements.push_back(inner);
                        }
                        pos = end + 3;
                        parsed = true;
                    }
                }
            }

            if (parsed) continue;

            // --- Strikethrough: ~~text~~ ---
            if (pos + 1 < len && line[pos] == '~' && line[pos + 1] == '~') {
                size_t end = line.find("~~", pos + 2);
                if (end != std::string::npos) {
                    elem.text = line.substr(pos + 2, end - pos - 2);
                    elem.isStrikethrough = true;
                    elements.push_back(elem);
                    pos = end + 2;
                    parsed = true;
                }
            }

            if (parsed) continue;

            // --- Highlight: ==text== ---
            if (pos + 1 < len && line[pos] == '=' && line[pos + 1] == '=') {
                size_t end = line.find("==", pos + 2);
                if (end != std::string::npos) {
                    elem.text = line.substr(pos + 2, end - pos - 2);
                    elem.isHighlight = true;
                    elements.push_back(elem);
                    pos = end + 2;
                    parsed = true;
                }
            }

            if (parsed) continue;

            // --- Subscript: ~text~ (single tilde, not ~~) ---
            if (line[pos] == '~' && (pos + 1 >= len || line[pos + 1] != '~')) {
                // Single tilde — look for closing single tilde
                size_t end = pos + 1;
                while (end < len && line[end] != '~' && line[end] != ' ' && line[end] != '\t') {
                    end++;
                }
                if (end < len && line[end] == '~' && end > pos + 1) {
                    // Make sure the closing ~ is not part of ~~
                    if (end + 1 >= len || line[end + 1] != '~') {
                        elem.text = line.substr(pos + 1, end - pos - 1);
                        elem.isSubscript = true;
                        elements.push_back(elem);
                        pos = end + 1;
                        parsed = true;
                    }
                }
            }

            if (parsed) continue;

            // --- Inline footnote: ^[text] ---
            if (line[pos] == '^' && pos + 1 < len && line[pos + 1] == '[') {
                size_t bracketEnd = FindMatchingBracket(line, pos + 1);
                if (bracketEnd != std::string::npos) {
                    std::string footnoteText = line.substr(pos + 2, bracketEnd - pos - 2);
                    elem.isFootnote = true;
                    elem.footnoteContent = footnoteText;
                    elem.text = "[*]";
                    elements.push_back(elem);
                    pos = bracketEnd + 1;
                    parsed = true;
                }
            }

            if (parsed) continue;

            // --- Superscript: ^text^ (single caret) ---
            if (line[pos] == '^') {
                size_t end = pos + 1;
                while (end < len && line[end] != '^' && line[end] != ' ' && line[end] != '\t') {
                    end++;
                }
                if (end < len && line[end] == '^' && end > pos + 1) {
                    elem.text = line.substr(pos + 1, end - pos - 1);
                    elem.isSuperscript = true;
                    elements.push_back(elem);
                    pos = end + 1;
                    parsed = true;
                }
            }

            if (parsed) continue;

            // --- Emoji shortcode: :name: ---
            if (line[pos] == ':' && pos + 2 < len) {
                // Look for closing : with only alphanumeric/underscore/+ /- between
                size_t end = pos + 1;
                while (end < len && line[end] != ':' && line[end] != ' ' &&
                       line[end] != '\t' && line[end] != '\n') {
                    end++;
                }
                if (end < len && line[end] == ':' && end > pos + 1) {
                    std::string shortcode = line.substr(pos + 1, end - pos - 1);
                    std::string emoji = LookupEmojiShortcode(shortcode);
                    if (!emoji.empty()) {
                        elem.text = emoji;
                        elem.isEmoji = true;
                        elements.push_back(elem);
                        pos = end + 1;
                        parsed = true;
                    }
                    // If emoji not found, fall through — treat : as plain text
                }
            }

            if (parsed) continue;

            // --- Auto-URL: http:// or https:// bare URLs ---
            if (pos + 7 < len && line[pos] == 'h' && line[pos + 1] == 't' &&
                line[pos + 2] == 't' && line[pos + 3] == 'p') {
                bool isHttps = (pos + 8 < len && line.substr(pos, 8) == "https://");
                bool isHttp = (!isHttps && line.substr(pos, 7) == "http://");

                if (isHttp || isHttps) {
                    // Find end of URL: consume until whitespace, ), ], or end of line
                    size_t urlStart = pos;
                    size_t end = pos + (isHttps ? 8 : 7);
                    while (end < len && line[end] != ' ' && line[end] != '\t' &&
                           line[end] != ')' && line[end] != ']' && line[end] != '>' &&
                           line[end] != '"' && line[end] != '\'' && line[end] != '\n') {
                        end++;
                    }
                    // Strip trailing punctuation that's likely not part of the URL
                    while (end > urlStart + 1) {
                        char last = line[end - 1];
                        if (last == '.' || last == ',' || last == ';' || last == '!' || last == '?') {
                            end--;
                        } else {
                            break;
                        }
                    }
                    std::string urlText = line.substr(urlStart, end - urlStart);
                    elem.text = urlText;
                    elem.url = urlText;
                    elem.isAutoLink = true;
                    elements.push_back(elem);
                    pos = end;
                    parsed = true;
                }
            }

            if (parsed) continue;

            // --- Bold: **text** or __text__ ---
            if (pos + 1 < len) {
                char c = line[pos];
                if ((c == '*' || c == '_') && line[pos + 1] == c) {
                    std::string closeMarker(2, c);
                    size_t end = line.find(closeMarker, pos + 2);
                    // In *** clusters, align closing ** to the right so inner * can close italic
                    while (end != std::string::npos && end + 2 < len && line[end + 2] == c) {
                        end++;
                    }
                    if (end != std::string::npos) {
                        std::string innerText = line.substr(pos + 2, end - pos - 2);
                        auto innerElements = ParseInlineMarkdown(innerText);
                        for (auto& inner : innerElements) {
                            inner.isBold = true;
                            elements.push_back(inner);
                        }
                        pos = end + 2;
                        parsed = true;
                    }
                }
            }

            if (parsed) continue;

            // --- Italic: *text* or _text_ ---
            if (line[pos] == '*' || line[pos] == '_') {
                char marker = line[pos];
                // Ensure not a double marker (already handled above)
                if (pos + 1 < len && line[pos + 1] != marker) {
                    size_t end = pos + 1;
                    while (end < len) {
                        if (line[end] == marker) {
                            // Check it's not a double marker
                            bool isDouble = (end + 1 < len && line[end + 1] == marker);
                            if (!isDouble) {
                                std::string innerText = line.substr(pos + 1, end - pos - 1);
                                auto innerElements = ParseInlineMarkdown(innerText);
                                for (auto& inner : innerElements) {
                                    inner.isItalic = true;
                                    elements.push_back(inner);
                                }
                                pos = end + 1;
                                parsed = true;
                                break;
                            } else {
                                end += 2;
                                continue;
                            }
                        }
                        end++;
                    }
                }
            }

            if (parsed) continue;

            // --- Plain text: consume until next markdown marker ---
            {
                size_t nextMarker = std::string::npos;
                // Search for the nearest markdown marker
                const char* markers[] = {"\\", "$$", "***", "___", "**", "__", "~~", "==", "`", "http", "^", "~", ":", "*", "_", "[", "![", "$"};
                for (const char* m : markers) {
                    size_t found = line.find(m, pos + 1);
                    if (found != std::string::npos && found < nextMarker) {
                        nextMarker = found;
                    }
                }

                if (nextMarker == std::string::npos || nextMarker <= pos) {
                    elem.text = line.substr(pos);
                    elements.push_back(elem);
                    break;
                } else {
                    elem.text = line.substr(pos, nextMarker - pos);
                    elements.push_back(elem);
                    pos = nextMarker;
                }
            }
        }

        return elements;
    }

    // ---------------------------------------------------------------
    // ABBREVIATION POST-PROCESSOR — splits plain text at abbreviation
    // boundaries so each occurrence becomes its own element
    // ---------------------------------------------------------------

    static bool IsAbbrWordBoundary(const std::string& text, size_t pos) {
        if (pos == 0 || pos >= text.size()) return true;
        unsigned char c = static_cast<unsigned char>(text[pos]);
        return !std::isalnum(c) && c != '_';
    }

    static std::vector<MarkdownInlineElement> ApplyAbbreviations(
        const std::vector<MarkdownInlineElement>& elements,
        const std::unordered_map<std::string, std::string>& abbreviations)
    {
        if (abbreviations.empty()) return elements;

        // Sort abbreviation keys by length descending (prefer longer matches)
        std::vector<std::pair<std::string, std::string>> sortedAbbrs(
            abbreviations.begin(), abbreviations.end());
        std::sort(sortedAbbrs.begin(), sortedAbbrs.end(),
            [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });

        std::vector<MarkdownInlineElement> result;

        for (const auto& elem : elements) {
            // Only process plain text elements (not code, link, math, image, etc.)
            if (elem.isCode || elem.isLink || elem.isImage || elem.isMath ||
                elem.isAutoLink || elem.isAbbreviation || elem.text.empty()) {
                result.push_back(elem);
                continue;
            }

            // Scan text for abbreviation occurrences
            const std::string& text = elem.text;
            size_t pos = 0;
            bool found = false;

            while (pos < text.size()) {
                bool matchedHere = false;

                for (const auto& [abbr, expansion] : sortedAbbrs) {
                    if (pos + abbr.size() > text.size()) continue;

                    // Check if the abbreviation matches at this position
                    if (text.compare(pos, abbr.size(), abbr) != 0) continue;

                    // Check word boundaries
                    bool leftOk = IsAbbrWordBoundary(text, pos);
                    bool rightOk = IsAbbrWordBoundary(text, pos + abbr.size());
                    if (!leftOk || !rightOk) continue;

                    // Found a match — emit text before it
                    if (pos > 0) {
                        // Find start of unprocessed text
                        size_t segStart = 0;
                        if (!result.empty() && found) {
                            segStart = 0; // already handled
                        }
                    }

                    found = true;
                    matchedHere = true;

                    // We need to re-scan from the beginning of this element's text
                    // Split: text-before, abbreviation, then continue with text-after
                    // Restart approach: collect all splits for this element
                    break;
                }

                if (matchedHere) break;
                pos++;
            }

            if (!found) {
                result.push_back(elem);
                continue;
            }

            // Re-scan the full text and split at all abbreviation boundaries
            pos = 0;
            size_t lastEnd = 0;

            while (pos < text.size()) {
                bool matchedHere = false;

                for (const auto& [abbr, expansion] : sortedAbbrs) {
                    if (pos + abbr.size() > text.size()) continue;
                    if (text.compare(pos, abbr.size(), abbr) != 0) continue;

                    bool leftOk = IsAbbrWordBoundary(text, pos);
                    bool rightOk = IsAbbrWordBoundary(text, pos + abbr.size());
                    if (!leftOk || !rightOk) continue;

                    // Emit text before the abbreviation
                    if (pos > lastEnd) {
                        MarkdownInlineElement before = elem;
                        before.text = text.substr(lastEnd, pos - lastEnd);
                        before.isAbbreviation = false;
                        before.abbreviationExpansion.clear();
                        result.push_back(before);
                    }

                    // Emit the abbreviation element
                    MarkdownInlineElement abbrElem = elem;
                    abbrElem.text = abbr;
                    abbrElem.isAbbreviation = true;
                    abbrElem.abbreviationExpansion = expansion;
                    result.push_back(abbrElem);

                    pos += abbr.size();
                    lastEnd = pos;
                    matchedHere = true;
                    break;
                }

                if (!matchedHere) pos++;
            }

            // Emit remaining text after last abbreviation
            if (lastEnd < text.size()) {
                MarkdownInlineElement after = elem;
                after.text = text.substr(lastEnd);
                after.isAbbreviation = false;
                after.abbreviationExpansion.clear();
                result.push_back(after);
            }
        }

        return result;
    }

    // ---------------------------------------------------------------
    // FOOTNOTE POST-PROCESSOR — fills footnote content from the
    // footnote map for reference-style footnotes [^label]
    // ---------------------------------------------------------------

    static std::vector<MarkdownInlineElement> ApplyFootnotes(
        const std::vector<MarkdownInlineElement>& elements,
        const std::unordered_map<std::string, std::string>& footnotes)
    {
        if (footnotes.empty()) return elements;

        std::vector<MarkdownInlineElement> result;
        result.reserve(elements.size());

        for (const auto& elem : elements) {
            if (elem.isFootnote && elem.footnoteContent.empty()) {
                // Reference footnote — look up content from map
                // elem.text is "[label]", extract label
                std::string label = elem.text;
                if (label.size() >= 2 && label.front() == '[' && label.back() == ']') {
                    label = label.substr(1, label.size() - 2);
                }
                auto it = footnotes.find(label);
                if (it != footnotes.end()) {
                    MarkdownInlineElement resolved = elem;
                    resolved.footnoteContent = it->second;
                    result.push_back(resolved);
                    continue;
                }
            }
            result.push_back(elem);
        }

        return result;
    }

    // ---------------------------------------------------------------
    // INLINE LINE RENDERER — renders parsed inline elements
    // ---------------------------------------------------------------

    static int RenderMarkdownLine(IRenderContext* ctx, const std::string& line,
                                  int x, int y, int lineHeight,
                                  const TextAreaStyle& style,
                                  const MarkdownHybridStyle& mdStyle,
                                  std::vector<MarkdownHitRect>* hitRects = nullptr,
                                  const std::unordered_map<std::string, std::string>* abbreviations = nullptr,
                                  const std::unordered_map<std::string, std::string>* footnotes = nullptr) {

        std::vector<MarkdownInlineElement> elements = ParseInlineMarkdown(line);
        if (abbreviations && !abbreviations->empty()) {
            elements = ApplyAbbreviations(elements, *abbreviations);
        }
        if (footnotes && !footnotes->empty()) {
            elements = ApplyFootnotes(elements, *footnotes);
        }
        int currentX = x;

        for (const auto& elem : elements) {
            if (elem.text.empty() && !elem.isImage) continue;

            // --- Image element ---
            if (elem.isImage) {
                int imgSize = lineHeight - 2;
                int imgY = y + 1;

                // Draw image placeholder/thumbnail
                ctx->SetFillPaint(mdStyle.imagePlaceholderBackground);
                ctx->FillRectangle(currentX, imgY, imgSize, imgSize);
                ctx->SetStrokePaint(mdStyle.imagePlaceholderBorderColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawRectangle(currentX, imgY, imgSize, imgSize);

                // Draw small icon indicator in center
                int iconCenterX = currentX + imgSize / 2;
                int iconCenterY = imgY + imgSize / 2;
                int iconR = std::max(2, imgSize / 6);
                ctx->SetFillPaint(mdStyle.imagePlaceholderTextColor);
                // Mountain icon: small triangle
                ctx->ClearPath();
                ctx->MoveTo(currentX + 2, imgY + imgSize - 2);
                ctx->LineTo(iconCenterX, imgY + 3);
                ctx->LineTo(currentX + imgSize - 2, imgY + imgSize - 2);
                ctx->ClosePath();
                ctx->Fill();

                // Store hit rect for click interaction
                if (hitRects) {
                    MarkdownHitRect hr;
                    hr.bounds = {currentX, imgY, imgSize, imgSize};
                    hr.url = elem.url;
                    hr.altText = elem.altText;
                    hr.isImage = true;
                    hitRects->push_back(hr);
                }

                currentX += imgSize + 4;

                // Draw alt text after image icon
                if (!elem.altText.empty()) {
                    ctx->SetFontWeight(FontWeight::Normal);
                    ctx->SetFontSlant(FontSlant::Italic);
                    ctx->SetTextPaint(mdStyle.imagePlaceholderTextColor);
                    ctx->DrawText(elem.altText, {currentX, y});
                    currentX += ctx->GetTextLineWidth(elem.altText) + 2;
                    ctx->SetFontSlant(FontSlant::Normal);
                }
                continue;
            }

            // --- Math element ($..$ or $$..$$) ---
            if (elem.isMath) {
                int textWidth = ctx->GetTextLineWidth(elem.text);

                // Draw subtle background
                ctx->SetFillPaint(mdStyle.mathBackgroundColor);
                ctx->FillRoundedRectangle(Rect2Df(currentX - 2, y + 1,
                                           textWidth + 4, lineHeight - 2), 3);

                // Draw math text in italic
                ctx->SetFontSlant(FontSlant::Italic);
                ctx->SetFontWeight(FontWeight::Normal);
                ctx->SetTextPaint(mdStyle.mathTextColor);
                ctx->DrawText(elem.text, {currentX, y});

                ctx->SetFontSlant(FontSlant::Normal);
                currentX += textWidth;
                continue;
            }

            // --- Link element ---
            if (elem.isLink) {
                ctx->SetFontWeight(FontWeight::Normal);
                ctx->SetFontSlant(FontSlant::Normal);
                ctx->SetTextPaint(mdStyle.linkColor);
                int textWidth = ctx->GetTextLineWidth(elem.text);

                ctx->DrawText(elem.text, {currentX, y});

                // Draw underline
                if (mdStyle.linkUnderline) {
                    int underlineY = y + lineHeight - 3;
                    ctx->SetStrokePaint(mdStyle.linkColor);
                    ctx->SetStrokeWidth(1.0f);
                    ctx->DrawLine({currentX, underlineY}, { currentX + textWidth, underlineY});
                }

                // Store hit rect
                if (hitRects) {
                    MarkdownHitRect hr;
                    hr.bounds = {currentX, y, textWidth, lineHeight};
                    hr.url = elem.url;
                    hr.isImage = false;
                    hitRects->push_back(hr);
                }

                currentX += textWidth;
                continue;
            }

            // --- Subscript element: rendered smaller and lower ---
            if (elem.isSubscript) {
                double origSize = style.fontStyle.fontSize;
                double subSize = origSize * 0.7;
                ctx->SetFontSize(subSize);
                ctx->SetFontWeight(FontWeight::Normal);
                ctx->SetFontSlant(FontSlant::Normal);
                ctx->SetTextPaint(style.fontColor);

                int textWidth = ctx->GetTextLineWidth(elem.text);
                double subY = y + lineHeight * 0.3; // Lower position
                ctx->DrawText(elem.text, {static_cast<double>(currentX), subY});

                ctx->SetFontSize(origSize);
                currentX += textWidth;
                continue;
            }

            // --- Superscript element: rendered smaller and higher ---
            if (elem.isSuperscript) {
                double origSize = style.fontStyle.fontSize;
                double supSize = origSize * 0.7;
                ctx->SetFontSize(supSize);
                ctx->SetFontWeight(FontWeight::Normal);
                ctx->SetFontSlant(FontSlant::Normal);
                ctx->SetTextPaint(style.fontColor);

                int textWidth = ctx->GetTextLineWidth(elem.text);
                double supY = y - lineHeight * 0.15f; // Higher position
                ctx->DrawText(elem.text, {static_cast<double>(currentX), supY});

                ctx->SetFontSize(origSize);
                currentX += textWidth;
                continue;
            }

            // --- Emoji element: rendered at normal size ---
            if (elem.isEmoji) {
                ctx->SetFontWeight(FontWeight::Normal);
                ctx->SetFontSlant(FontSlant::Normal);
                ctx->SetTextPaint(style.fontColor);
                ctx->DrawText(elem.text, {currentX, y});
                currentX += ctx->GetTextLineWidth(elem.text);
                continue;
            }

            // --- Auto-linked URL: rendered as a clickable link ---
            if (elem.isAutoLink) {
                ctx->SetFontWeight(FontWeight::Normal);
                ctx->SetFontSlant(FontSlant::Normal);
                ctx->SetTextPaint(mdStyle.linkColor);
                int textWidth = ctx->GetTextLineWidth(elem.text);

                ctx->DrawText(elem.text, {currentX, y});

                // Draw underline
                if (mdStyle.linkUnderline) {
                    int underlineY = y + lineHeight - 3;
                    ctx->SetStrokePaint(mdStyle.linkColor);
                    ctx->SetStrokeWidth(1.0f);
                    ctx->DrawLine({currentX, underlineY}, { currentX + textWidth, underlineY});
                }

                // Store hit rect for click
                if (hitRects) {
                    MarkdownHitRect hr;
                    hr.bounds = {currentX, y, textWidth, lineHeight};
                    hr.url = elem.url;
                    hr.isImage = false;
                    hitRects->push_back(hr);
                }

                currentX += textWidth;
                continue;
            }

            // --- Abbreviation element: gray background + dotted underline ---
            if (elem.isAbbreviation) {
                FontWeight weight = elem.isBold ? FontWeight::Bold : FontWeight::Normal;
                FontSlant slant = elem.isItalic ? FontSlant::Italic : FontSlant::Normal;
                ctx->SetFontWeight(weight);
                ctx->SetFontSlant(slant);

                int textWidth = ctx->GetTextLineWidth(elem.text);

                // Draw gray background
                ctx->SetFillPaint(mdStyle.abbreviationBackground);
                ctx->FillRoundedRectangle({currentX - 1, y + 1, textWidth + 2, lineHeight - 2}, 2);

                // Draw text
                ctx->SetTextPaint(style.fontColor);
                ctx->DrawText(elem.text, {currentX, y});

                // Draw dotted underline
                int underlineY = y + lineHeight - 2;
                ctx->SetStrokePaint(mdStyle.abbreviationUnderlineColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->SetLineDash(UCDashPattern({mdStyle.abbreviationUnderlineDashLength,mdStyle.abbreviationUnderlineGapLength}));
                ctx->DrawLine({currentX, underlineY}, {currentX + textWidth, underlineY});

                // Store hit rect for hover tooltip
                if (hitRects) {
                    MarkdownHitRect hr;
                    hr.bounds = {currentX, y, textWidth, lineHeight};
                    hr.altText = elem.abbreviationExpansion;
                    hr.isAbbreviation = true;
                    hitRects->push_back(hr);
                }

                currentX += textWidth;
                continue;
            }

            // --- Footnote element: superscript colored reference ---
            if (elem.isFootnote) {
                double origSize = style.fontStyle.fontSize;
                double supSize = origSize * 0.7f;
                ctx->SetFontSize(supSize);
                ctx->SetFontWeight(FontWeight::Normal);
                ctx->SetFontSlant(FontSlant::Normal);
                ctx->SetTextPaint(mdStyle.footnoteRefColor);

                int textWidth = ctx->GetTextLineWidth(elem.text);
                double supY = y - lineHeight * 0.15;
                ctx->DrawText(elem.text, {static_cast<double>(currentX), supY});

                // Store hit rect for hover tooltip
                if (hitRects && !elem.footnoteContent.empty()) {
                    MarkdownHitRect hr;
                    hr.bounds = {currentX, y, textWidth, lineHeight};
                    hr.altText = elem.footnoteContent;
                    hr.isFootnote = true;
                    hitRects->push_back(hr);
                }

                ctx->SetFontSize(origSize);
                currentX += textWidth;
                continue;
            }

            // --- Determine formatting ---
            FontWeight weight = elem.isBold ? FontWeight::Bold : FontWeight::Normal;
            FontSlant slant = elem.isItalic ? FontSlant::Italic : FontSlant::Normal;
            Color color = style.fontColor;

            if (elem.isCode) {
                color = mdStyle.codeTextColor;
            }

            ctx->SetFontWeight(weight);
            ctx->SetFontSlant(slant);

            // --- Set code font BEFORE measuring so width is correct ---
            if (elem.isCode) {
                ctx->SetFontFamily(mdStyle.codeFont);
            }

            int textWidth = ctx->GetTextLineWidth(elem.text);

            // --- Highlight background ---
            if (elem.isHighlight) {
                ctx->SetFillPaint(mdStyle.highlightBackground);
                ctx->FillRectangle(currentX - 1, y, textWidth + 2, lineHeight);
            }

            // --- Code background ---
            if (elem.isCode) {
                ctx->SetFillPaint(mdStyle.codeBackgroundColor);
                ctx->FillRoundedRectangle({currentX - 2, y + 1, textWidth + 4, lineHeight - 2}, 3);
            }

            // --- Draw the text ---
            ctx->SetTextPaint(elem.isStrikethrough ? mdStyle.strikethroughColor : color);
            ctx->DrawText(elem.text, {currentX, y});

            // --- Strikethrough line ---
            if (elem.isStrikethrough) {
                int strikeY = y + lineHeight / 2;
                ctx->SetStrokePaint(mdStyle.strikethroughColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawLine({currentX, strikeY}, { currentX + textWidth, strikeY});
            }

            // --- Restore font if code changed it ---
            if (elem.isCode) {
                ctx->SetFontFamily(style.fontStyle.fontFamily);
            }

            currentX += textWidth;
        }

        // Reset font state
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetFontSlant(FontSlant::Normal);

        return currentX - x; // Return total width rendered
    }

    // ---------------------------------------------------------------
    // HEADER RENDERER
    // ---------------------------------------------------------------

    static void RenderMarkdownHeader(IRenderContext* ctx, const std::string& line,
                                     int x, int y, int lineHeight,
                                     const TextAreaStyle& style,
                                     const MarkdownHybridStyle& mdStyle,
                                     std::vector<MarkdownHitRect>* hitRects = nullptr,
                                     const std::unordered_map<std::string, std::string>* abbreviations = nullptr,
                                     const std::unordered_map<std::string, std::string>* footnotes = nullptr,
                                     const std::unordered_map<std::string, int>* anchorBacklinks = nullptr,
                                     bool isDarkMode = false) {
        // Detect header level
        int level = 0;
        size_t pos = 0;
        while (pos < line.length() && line[pos] == '#') {
            level++;
            pos++;
        }

        if (level == 0 || level > 6) {
            RenderMarkdownLine(ctx, line, x, y, lineHeight, style, mdStyle, hitRects, abbreviations, footnotes);
            return;
        }

        // Skip whitespace after #
        while (pos < line.length() && line[pos] == ' ') pos++;

        std::string headerText = line.substr(pos);

        // Strip explicit anchor {#id} from display (capture the ID for backlink lookup)
        std::string explicitAnchorId = StripExplicitAnchor(headerText);

        int levelIndex = std::clamp(level - 1, 0, 5);

        // Set header font size — clamped to not exceed line height
        float baseFontSize = style.fontStyle.fontSize;
        float headerFontSize = baseFontSize * mdStyle.headerSizeMultipliers[levelIndex];

        // Clamp font size so it doesn't overflow the fixed row height.
        // Each heading level gets progressively more headroom so H1 always
        // renders visibly larger than H2 even at small base font sizes.
        // levelIndex 0=H1, 1=H2, ... 5=H6
        static constexpr float levelCeilMultipliers[6] = {
            1.05f,  // H1 — allowed to slightly exceed line height for distinction
            0.90f,  // H2
            0.85f,  // H3
            0.82f,  // H4
            0.80f,  // H5
            0.78f   // H6
        };
        float maxFontSize = static_cast<float>(lineHeight) * levelCeilMultipliers[levelIndex];
        headerFontSize = std::min(headerFontSize, maxFontSize);

        ctx->SetFontSize(headerFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(mdStyle.headerColors[levelIndex]);

        // Vertically center the header text within the fixed line height
        int textHeight = ctx->GetTextLineHeight(headerText.empty() ? "M" : headerText);
        int centeredY = y + (lineHeight - textHeight) / 2;

        // Render inline markdown within header text (links, bold, etc.)
        int renderedWidth = RenderMarkdownLine(ctx, headerText, x, centeredY, lineHeight, style, mdStyle, hitRects, abbreviations, footnotes);

        // Render backlink icon if exactly 1 internal link points to this header's anchor
        if (anchorBacklinks && !anchorBacklinks->empty()) {
            std::string slug = GenerateHeadingSlug(headerText);
            int backlinkSourceLine = -1;

            // Check explicit anchor first, then auto-slug
            if (!explicitAnchorId.empty()) {
                auto it = anchorBacklinks->find(explicitAnchorId);
                if (it != anchorBacklinks->end()) backlinkSourceLine = it->second;
            }
            if (backlinkSourceLine < 0 && !slug.empty()) {
                auto it = anchorBacklinks->find(slug);
                if (it != anchorBacklinks->end()) backlinkSourceLine = it->second;
            }

            if (backlinkSourceLine >= 0) {
                int iconSize = std::max(20, (lineHeight / 2) + 4);
                int iconX = x + renderedWidth + 6;
                int iconY = y + (lineHeight - iconSize) / 2;

                // Draw subtle rounded rectangle background for visibility in both modes
                if (isDarkMode) {
                    ctx->SetFillPaint(Color(180, 180, 190, 120));
                } else {
                    ctx->SetFillPaint(Color(200, 200, 210, 100));
                }
                ctx->FillRoundedRectangle({iconX - 2, iconY - 2, iconSize + 4, iconSize + 4}, 4);

                // Draw the undo SVG icon
                std::string iconPath = GetResourcesDir() + "media/icons/texter/undo.svg";
                ctx->DrawImage(iconPath, static_cast<float>(iconX), static_cast<float>(iconY),
                               static_cast<float>(iconSize), static_cast<float>(iconSize),
                               ImageFitMode::Contain);

                // Register hit rect for click/hover interaction
                if (hitRects) {
                    MarkdownHitRect hr;
                    hr.bounds = {iconX - 2, iconY - 2, iconSize + 4, iconSize + 4};
                    hr.url = std::to_string(backlinkSourceLine);
                    hr.altText = "Return back";
                    hr.isAnchorReturn = true;
                    hitRects->push_back(hr);
                }
            }
        }

        // Restore font
        ctx->SetFontSize(baseFontSize);
        ctx->SetFontWeight(FontWeight::Normal);
    }

    // ---------------------------------------------------------------
    // LIST ITEM RENDERER — unordered, ordered, and task lists
    // ---------------------------------------------------------------

    static void RenderMarkdownListItem(IRenderContext* ctx, const std::string& line,
                                       int x, int y, int lineHeight,
                                       const TextAreaStyle& style,
                                       const MarkdownHybridStyle& mdStyle,
                                       std::vector<MarkdownHitRect>* hitRects = nullptr,
                                       int orderNumberOverride = -1,
                                       const std::unordered_map<std::string, std::string>* abbreviations = nullptr,
                                       const std::unordered_map<std::string, std::string>* footnotes = nullptr) {
        size_t pos = 0;

        // Count leading whitespace for nesting depth
        int indent = 0;
        while (pos < line.length() && (line[pos] == ' ' || line[pos] == '\t')) {
            if (line[pos] == '\t') indent += 4;
            else indent++;
            pos++;
        }
        int nestingLevel = indent / 2;

        bool isOrdered = false;
        bool isTaskList = false;
        bool isTaskChecked = false;
        int orderNumber = 0;

        // Check for ordered list: digits followed by ". "
        if (pos < line.length() && std::isdigit(line[pos])) {
            size_t numStart = pos;
            while (pos < line.length() && std::isdigit(line[pos])) pos++;
            if (pos < line.length() && line[pos] == '.') {
                orderNumber = (orderNumberOverride >= 0)
                    ? orderNumberOverride
                    : std::stoi(line.substr(numStart, pos - numStart));
                isOrdered = true;
                pos++; // skip '.'
            } else {
                pos = numStart; // reset, not a valid ordered list
            }
        }

        // Check for unordered list: -, *, +
        if (!isOrdered && pos < line.length() &&
            (line[pos] == '-' || line[pos] == '*' || line[pos] == '+')) {
            pos++; // skip marker
        }

        // Skip whitespace after marker
        while (pos < line.length() && line[pos] == ' ') pos++;

        // Check for task list: [ ] or [x] or [X]
        if (pos + 2 < line.length() && line[pos] == '[') {
            char check = line[pos + 1];
            if (line[pos + 2] == ']' && (check == ' ' || check == 'x' || check == 'X')) {
                isTaskList = true;
                isTaskChecked = (check == 'x' || check == 'X');
                pos += 3;
                // Skip space after ]
                while (pos < line.length() && line[pos] == ' ') pos++;
            }
        }

        std::string itemText = (pos < line.length()) ? line.substr(pos) : "";

        // Calculate x offset based on nesting
        int bulletX = x + (nestingLevel * mdStyle.listIndent);

        // Reserve space for bullet/number (keeps consistent indentation)
        int bulletCharWidth = ctx->GetTextLineWidth(mdStyle.bulletCharacter);
        int bulletSlotWidth = bulletCharWidth + 4;

        // --- Draw bullet/number/checkbox ---
        if (isTaskList) {
            // Draw checkbox (no bullet point)
            int cbSize = mdStyle.checkboxSize;
            int cbX = bulletX;
            int cbY = y + (lineHeight - cbSize) / 2;

            ctx->SetStrokePaint(mdStyle.checkboxBorderColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRectangle(cbX, cbY, cbSize, cbSize);

            if (isTaskChecked) {
                // Fill checkbox
                ctx->SetFillPaint(mdStyle.checkboxCheckedColor);
                ctx->FillRectangle(cbX + 1, cbY + 1, cbSize - 2, cbSize - 2);

                // Draw checkmark
                ctx->SetStrokePaint(mdStyle.checkboxCheckmarkColor);
                ctx->SetStrokeWidth(2.0f);
                int cx = cbX + cbSize / 2;
                int cy = cbY + cbSize / 2;
                ctx->DrawLine({cbX + 3, cy}, { cx - 1, cbY + cbSize - 3});
                ctx->DrawLine({cx - 1, cbY + cbSize - 3}, { cbX + cbSize - 3, cbY + 3});
            }

            bulletX += cbSize + 6;
        } else if (isOrdered) {
            // Draw order number
            std::string numberStr = std::to_string(orderNumber) + ".";
            ctx->SetFontWeight(FontWeight::Normal);
            ctx->SetTextPaint(mdStyle.bulletColor);
            ctx->DrawText(numberStr, {bulletX, y});
            int numWidth = ctx->GetTextLineWidth(numberStr);
            bulletX += numWidth + 4;
        } else {
            // Unordered list: draw nesting-level bullet character (•, ◦, ▪)
            int clampedLevel = std::min(nestingLevel,
                static_cast<int>(mdStyle.nestedBulletCharacters.size()) - 1);
            const std::string& bulletChar = mdStyle.nestedBulletCharacters[clampedLevel];
            ctx->SetFontWeight(FontWeight::Normal);
            ctx->SetTextPaint(mdStyle.bulletColor);
            ctx->DrawText(bulletChar, {bulletX, y});
            bulletX += ctx->GetTextLineWidth(bulletChar) + 4;
        }
        // --- Draw item text with inline formatting ---
        // Checked and unchecked task items both render with normal inline formatting
        // The checkbox itself indicates completion — no strikethrough needed
        RenderMarkdownLine(ctx, itemText, bulletX, y, lineHeight, style, mdStyle, hitRects, abbreviations, footnotes);
    }

    // ---------------------------------------------------------------
    // BLOCKQUOTE RENDERER
    // ---------------------------------------------------------------

    static void RenderMarkdownBlockquote(IRenderContext* ctx, const std::string& line,
                                         int x, int y, int lineHeight, int width,
                                         const TextAreaStyle& style,
                                         const MarkdownHybridStyle& mdStyle,
                                         std::vector<MarkdownHitRect>* hitRects = nullptr,
                                         const std::unordered_map<std::string, std::string>* abbreviations = nullptr,
                                         const std::unordered_map<std::string, std::string>* footnotes = nullptr) {
        // Count nesting depth (>>)
        size_t pos = 0;
        int depth = 0;
        std::string trimmed = line;
        // Trim leading whitespace
        while (pos < trimmed.length() && trimmed[pos] == ' ') pos++;
        while (pos < trimmed.length() && trimmed[pos] == '>') {
            depth++;
            pos++;
            // Skip space after >
            if (pos < trimmed.length() && trimmed[pos] == ' ') pos++;
        }
        std::string quoteText = (pos < trimmed.length()) ? trimmed.substr(pos) : "";

        // Each nesting level adds one bar + a fixed horizontal step
        // barStride = distance from one bar's left edge to the next level's bar left edge
        int barStride = mdStyle.quoteNestingStep;

        // Full background covers from x to x+width for all nesting levels
        ctx->SetFillPaint(mdStyle.quoteBackgroundColor);
        ctx->FillRectangle(x, y, width, lineHeight);

        // Draw one vertical bar per nesting level, each offset by barStride
        for (int d = 0; d < depth; d++) {
            int barX = x + d * barStride;
            ctx->SetFillPaint(mdStyle.quoteBarColor);
            ctx->FillRectangle(barX, y, mdStyle.quoteBarWidth, lineHeight);
        }

        // Text starts after the outermost bar + gap + per-level indent
        int textX = x + (depth - 1) * barStride + mdStyle.quoteIndent;
        ctx->SetFontSlant(FontSlant::Italic);
        ctx->SetTextPaint(mdStyle.quoteTextColor);
        RenderMarkdownLine(ctx, quoteText, textX, y, lineHeight, style, mdStyle, hitRects, abbreviations, footnotes);
        ctx->SetFontSlant(FontSlant::Normal);
    }

    // ---------------------------------------------------------------
    // CODE BLOCK LINE RENDERER — for lines inside ``` blocks
    // ---------------------------------------------------------------

    static void RenderMarkdownCodeBlockLine(IRenderContext* ctx, const std::string& line,
                                            int x, int y, int lineHeight, int width,
                                            const TextAreaStyle& style,
                                            const MarkdownHybridStyle& mdStyle) {
        // Draw background for code block line
        ctx->SetFillPaint(mdStyle.codeBlockBackgroundColor);
        ctx->SetStrokePaint(mdStyle.codeBlockBorderColor);
        ctx->SetStrokeWidth(0.5);
        ctx->FillRectangle(x, y, width, lineHeight);

        // Draw left/right border accent
        ctx->DrawLine({x, y}, { x, y + lineHeight});
        ctx->DrawLine({x + width, y}, { x + width, y + lineHeight});

        // Draw code text in monospace
        ctx->SetFontFamily(mdStyle.codeFont);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetFontSlant(FontSlant::Normal);
        ctx->SetTextPaint(mdStyle.codeBlockTextColor);
        ctx->DrawText(line, {x + 4, y});

        // Restore font
        ctx->SetFontFamily(style.fontStyle.fontFamily);
    }

    // ---------------------------------------------------------------
    // CODE BLOCK SYNTAX-HIGHLIGHTED RENDERER — uses SyntaxTokenizer
    // ---------------------------------------------------------------

    static void RenderMarkdownCodeBlockHighlighted(
            IRenderContext* ctx, const std::string& line,
            int x, int y, int lineHeight, int width,
            const TextAreaStyle& style,
            const MarkdownHybridStyle& mdStyle,
            SyntaxTokenizer* tokenizer,
            std::function<TokenStyle(TokenType)> getStyleForType) {

        // Draw background for code block line
        ctx->SetFillPaint(mdStyle.codeBlockBackgroundColor);
        ctx->SetStrokePaint(mdStyle.codeBlockBorderColor);
        ctx->SetStrokeWidth(0.5);
        ctx->FillRectangle(x, y, width, lineHeight);

        // Draw left/right border accent
        ctx->DrawLine({x, y}, { x, y + lineHeight});
        ctx->DrawLine({x + width, y}, { x + width, y + lineHeight});

        // Set monospace font
        ctx->SetFontFamily(mdStyle.codeFont);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetFontSlant(FontSlant::Normal);

        if (tokenizer) {
            // Tokenize the line and render each token with its color
            // IMPORTANT: We use code-block-specific colors instead of the TextArea
            // theme colors, because the TextArea theme may be dark-on-light or
            // light-on-dark and the code block background is always a specific color
            auto tokens = tokenizer->TokenizeLine(line);
            int tokenX = x + 4;

            for (const auto& token : tokens) {
                // Map token types to code-block-specific colors
                Color tokenColor = mdStyle.codeBlockTextColor;
                bool tokenBold = false;

                switch (token.type) {
                    case TokenType::Keyword:
                    case TokenType::Preprocessor:
                        tokenColor = mdStyle.codeBlockKeywordColor;
                        tokenBold = true;
                        break;
                    case TokenType::String:
                    case TokenType::Character:
                        tokenColor = mdStyle.codeBlockStringColor;
                        break;
                    case TokenType::Comment:
                        tokenColor = mdStyle.codeBlockCommentColor;
                        break;
                    case TokenType::Number:
                        tokenColor = mdStyle.codeBlockNumberColor;
                        break;
                    case TokenType::Function:
                    case TokenType::Type:
                    case TokenType::Builtin:
                        tokenColor = mdStyle.codeBlockKeywordColor;
                        break;
                    case TokenType::Operator:
                        tokenColor = mdStyle.codeBlockTextColor;
                        break;
                    default:
                        tokenColor = mdStyle.codeBlockTextColor;
                        break;
                }

                ctx->SetFontWeight(tokenBold ? FontWeight::Bold : FontWeight::Normal);
                ctx->SetTextPaint(tokenColor);

                int tokenWidth = ctx->GetTextLineWidth(token.text);
                ctx->DrawText(token.text, {tokenX, y});
                tokenX += tokenWidth;
            }
        } else {
            // Fallback: render as plain monospace
            ctx->SetTextPaint(mdStyle.codeBlockTextColor);
            ctx->DrawText(line, {x + 4, y});
        }

        // Restore font
        ctx->SetFontFamily(style.fontStyle.fontFamily);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetFontSlant(FontSlant::Normal);
    }

    // ---------------------------------------------------------------
    // CODE BLOCK DELIMITER RENDERER — for ``` or ~~~ lines themselves
    // ---------------------------------------------------------------

    static void RenderMarkdownCodeBlockDelimiter(IRenderContext* ctx, const std::string& line,
                                                 int x, int y, int lineHeight, int width,
                                                 const TextAreaStyle& style,
                                                 const MarkdownHybridStyle& mdStyle,
                                                 bool isOpeningFence) {
        // Apply vertical inset so the background rectangle does not bleed into
        // the surrounding text rows:
        //   Opening fence (```cpp)  → inset at the top only
        //   Closing fence (```)     → inset at the bottom only
        int inset = mdStyle.blockVerticalInset;
        int bgY      = isOpeningFence ? y + lineHeight - inset : y;
        int bgHeight = inset;   // same reduction regardless of which end

        // Draw background
        ctx->SetFillPaint(mdStyle.codeBlockBackgroundColor);
        ctx->SetStrokePaint(mdStyle.codeBlockBorderColor);
        ctx->SetStrokeWidth(0.5);
        ctx->FillRectangle(x, bgY, width, bgHeight);
        ctx->DrawLine({x, isOpeningFence ? bgY : bgY + bgHeight}, { x + width, isOpeningFence ? bgY : bgY + bgHeight});
        ctx->DrawLine({x, bgY}, { x, bgY + bgHeight});
        ctx->DrawLine({x + width, bgY}, { x + width, bgY + bgHeight});
    }

    // ---------------------------------------------------------------
    // HORIZONTAL RULE RENDERER
    // ---------------------------------------------------------------

    static void RenderMarkdownHorizontalRule(IRenderContext* ctx,
                                             int x, int y, int lineHeight, int width,
                                             const MarkdownHybridStyle& mdStyle) {
        int ruleY = y + lineHeight / 2;
        ctx->SetStrokePaint(mdStyle.horizontalRuleColor);
        ctx->SetStrokeWidth(mdStyle.horizontalRuleHeight);
        ctx->DrawLine({x, ruleY}, { x + width, ruleY});
    }

    // ---------------------------------------------------------------
    // TABLE ROW RENDERER
    // ---------------------------------------------------------------

    static TableParseResult ParseTableRow(const std::string& line) {
        TableParseResult result;
        std::string trimmed = line;

        // Trim whitespace
        size_t start = trimmed.find_first_not_of(' ');
        if (start != std::string::npos) trimmed = trimmed.substr(start);
        size_t end = trimmed.find_last_not_of(' ');
        if (end != std::string::npos) trimmed = trimmed.substr(0, end + 1);

        // Must start and end with |
        if (trimmed.empty() || trimmed[0] != '|') return result;

        // Remove leading/trailing |
        if (trimmed.front() == '|') trimmed = trimmed.substr(1);
        if (!trimmed.empty() && trimmed.back() == '|') trimmed = trimmed.substr(0, trimmed.length() - 1);

        // Split by |
        std::istringstream ss(trimmed);
        std::string cell;
        while (std::getline(ss, cell, '|')) {
            // Trim cell whitespace
            size_t cs = cell.find_first_not_of(' ');
            size_t ce = cell.find_last_not_of(' ');
            if (cs != std::string::npos && ce != std::string::npos) {
                result.cells.push_back(cell.substr(cs, ce - cs + 1));
            } else {
                result.cells.push_back("");
            }
        }

        result.isValid = !result.cells.empty();
        return result;
    }

    static bool IsTableSeparatorRow(const std::string& line) {
        std::string trimmed = line;
        size_t start = trimmed.find_first_not_of(' ');
        if (start != std::string::npos) trimmed = trimmed.substr(start);

        if (trimmed.empty() || trimmed[0] != '|') return false;

        // Check if all cells contain only -, :, |, and spaces
        for (char c : trimmed) {
            if (c != '|' && c != '-' && c != ':' && c != ' ') return false;
        }
        // Must contain at least one -
        return trimmed.find('-') != std::string::npos;
    }

    static std::vector<TableColumnAlignment> ParseTableAlignments(const std::string& separatorLine) {
        std::vector<TableColumnAlignment> alignments;
        auto parsed = ParseTableRow(separatorLine);
        for (const auto& cell : parsed.cells) {
            bool leftColon = (!cell.empty() && cell.front() == ':');
            bool rightColon = (!cell.empty() && cell.back() == ':');

            if (leftColon && rightColon) {
                alignments.push_back(TableColumnAlignment::Center);
            } else if (rightColon) {
                alignments.push_back(TableColumnAlignment::Right);
            } else {
                alignments.push_back(TableColumnAlignment::Left);
            }
        }
        return alignments;
    }


    static std::vector<int> CalculateTableColumnWidths(
            IRenderContext* ctx,
            const std::vector<std::string>& allLines,
            size_t headerLineIndex,
            int columnCount,
            int availableWidth,
            const TextAreaStyle& style,
            const MarkdownHybridStyle& mdStyle) {

        int padding = static_cast<int>(mdStyle.tableCellPadding);
        int minColumnWidth = padding * 2 + 20; // Minimum: padding + some space

        // Collect max content width per column across all table rows
        std::vector<int> maxContentWidths(columnCount, 0);

        // Measure header row
        {
            auto headerParsed = ParseTableRow(allLines[headerLineIndex]);
            ctx->SetFontWeight(FontWeight::Bold);
            for (size_t col = 0; col < headerParsed.cells.size() &&
                                 col < static_cast<size_t>(columnCount); col++) {
                int textWidth = ctx->GetTextLineWidth(headerParsed.cells[col]);
                maxContentWidths[col] = std::max(maxContentWidths[col], textWidth + padding * 2);
            }
            ctx->SetFontWeight(FontWeight::Normal);
        }

        // Measure all data rows (skip separator at headerLineIndex + 1)
        for (size_t j = headerLineIndex + 2; j < allLines.size(); j++) {
            std::string trimmed = allLines[j];
            // Trim whitespace
            size_t start = trimmed.find_first_not_of(' ');
            if (start != std::string::npos) trimmed = trimmed.substr(start);
            size_t end = trimmed.find_last_not_of(' ');
            if (end != std::string::npos) trimmed = trimmed.substr(0, end + 1);

            if (trimmed.empty() || trimmed[0] != '|') break; // End of table

            auto rowParsed = ParseTableRow(allLines[j]);
            if (!rowParsed.isValid) break;

            for (size_t col = 0; col < rowParsed.cells.size() &&
                                 col < static_cast<size_t>(columnCount); col++) {
                int textWidth = ctx->GetTextLineWidth(rowParsed.cells[col]);
                maxContentWidths[col] = std::max(maxContentWidths[col], textWidth + padding * 2);
            }
        }

        // Enforce minimum column width
        for (int col = 0; col < columnCount; col++) {
            maxContentWidths[col] = std::max(maxContentWidths[col], minColumnWidth);
        }

        // Calculate total content width needed
        int totalContentWidth = 0;
        for (int col = 0; col < columnCount; col++) {
            totalContentWidth += maxContentWidths[col];
        }

        std::vector<int> columnWidths(columnCount);

        if (totalContentWidth <= availableWidth) {
            // Content fits — distribute remaining space proportionally
            int remainingSpace = availableWidth - totalContentWidth;

            for (int col = 0; col < columnCount; col++) {
                // Proportional share of remaining space based on content ratio
                float ratio = static_cast<float>(maxContentWidths[col]) /
                              static_cast<float>(totalContentWidth);
                int extraSpace = static_cast<int>(remainingSpace * ratio);
                columnWidths[col] = maxContentWidths[col] + extraSpace;
            }

            // Fix rounding: assign leftover pixels to the last column
            int assigned = 0;
            for (int col = 0; col < columnCount; col++) {
                assigned += columnWidths[col];
            }
            if (assigned < availableWidth) {
                columnWidths[columnCount - 1] += (availableWidth - assigned);
            }
        } else {
            // Content exceeds available width — shrink proportionally (browser-like behavior)
            // First, calculate total minimum width needed
            int totalMinWidth = minColumnWidth * columnCount;

            if (availableWidth <= totalMinWidth) {
                // Not enough space even for minimums — use minimum widths
                for (int col = 0; col < columnCount; col++) {
                    columnWidths[col] = minColumnWidth;
                }
            } else {
                // Distribute availableWidth proportionally based on content widths
                int assigned = 0;
                for (int col = 0; col < columnCount; col++) {
                    float ratio = static_cast<float>(maxContentWidths[col]) /
                                  static_cast<float>(totalContentWidth);
                    int colWidth = std::max(minColumnWidth,
                                            static_cast<int>(availableWidth * ratio));
                    columnWidths[col] = colWidth;
                    assigned += colWidth;
                }

                // Fix rounding: adjust last column to match exactly
                if (assigned != availableWidth) {
                    columnWidths[columnCount - 1] += (availableWidth - assigned);
                    columnWidths[columnCount - 1] = std::max(columnWidths[columnCount - 1], minColumnWidth);
                }
            }
        }

        return columnWidths;
    }

    // A "word token" for markdown-aware wrapping: tracks whether the token is
    // an inline code span so we can measure it with the correct font.
    struct WrapToken {
        std::string text;  // includes backticks for code spans
        bool isCode;
    };

    // Split cell text into tokens that are either plain words (split by spaces)
    // or complete backtick code spans kept as single tokens.
    static std::vector<WrapToken> TokenizeCellText(const std::string& text) {
        std::vector<WrapToken> tokens;
        size_t i = 0;
        size_t len = text.size();
        std::string accumPlain;

        auto flushPlain = [&]() {
            if (accumPlain.empty()) return;
            // Split accumulated plain text by spaces into word tokens
            std::istringstream ss(accumPlain);
            std::string word;
            while (ss >> word) {
                tokens.push_back({word, false});
            }
            accumPlain.clear();
        };

        while (i < len) {
            if (text[i] == '`') {
                // Found a backtick — look for the closing backtick
                size_t end = text.find('`', i + 1);
                if (end != std::string::npos) {
                    // Flush any accumulated plain text first
                    flushPlain();
                    // Extract content between backticks
                    std::string codeContent = text.substr(i + 1, end - i - 1);
                    // Split code content by spaces into per-word code tokens,
                    // each wrapped with backticks so ParseInlineMarkdown recognizes them
                    std::istringstream css(codeContent);
                    std::string codeWord;
                    while (css >> codeWord) {
                        tokens.push_back({"`" + codeWord + "`", true});
                    }
                    if (codeContent.empty()) {
                        tokens.push_back({"``", true});
                    }
                    i = end + 1;
                } else {
                    // No closing backtick — treat as plain text
                    accumPlain += text[i];
                    i++;
                }
            } else {
                accumPlain += text[i];
                i++;
            }
        }
        flushPlain();
        return tokens;
    }

    // Measure a token's width using the appropriate font (code font for code spans)
    static int MeasureTokenWidth(IRenderContext* ctx, const WrapToken& token,
                                 const std::string& codeFont, const std::string& regularFont) {
        if (token.isCode) {
            ctx->SetFontFamily(codeFont);
            int w = ctx->GetTextLineWidth(token.text);
            ctx->SetFontFamily(regularFont);
            return w;
        }
        return ctx->GetTextLineWidth(token.text);
    }

    // Measure a composed line's width accounting for mixed fonts in code spans.
    // This re-tokenizes the line to measure each segment with the correct font.
    static int MeasureMixedLineWidth(IRenderContext* ctx, const std::string& line,
                                     const std::string& codeFont, const std::string& regularFont) {
        auto tokens = TokenizeCellText(line);
        if (tokens.empty()) return 0;

        // Rebuild and measure: for accuracy, measure the whole line but with
        // code spans measured in code font. Since fonts differ in width,
        // sum individual token widths plus space widths.
        int totalWidth = 0;
        int spaceWidth = ctx->GetTextLineWidth(" ");
        for (size_t i = 0; i < tokens.size(); i++) {
            if (i > 0) totalWidth += spaceWidth;
            totalWidth += MeasureTokenWidth(ctx, tokens[i], codeFont, regularFont);
        }
        return totalWidth;
    }

    // Word-wrap cell text to fit within maxWidth, breaking long words at character boundaries.
    // Markdown-aware: measures backtick code spans with the code font for correct widths.
    static std::vector<std::string> WrapCellText(IRenderContext* ctx, const std::string& text,
                                                  int maxWidth,
                                                  const std::string& codeFont = "",
                                                  const std::string& regularFont = "") {
        std::vector<std::string> wrappedLines;
        if (text.empty() || maxWidth <= 0) {
            wrappedLines.push_back(text);
            return wrappedLines;
        }

        bool hasCodeFont = !codeFont.empty() && !regularFont.empty();

        // Tokenize with markdown awareness
        auto tokens = TokenizeCellText(text);

        if (tokens.empty()) {
            wrappedLines.push_back("");
            return wrappedLines;
        }

        // Helper to measure a token width with the correct font
        auto measureToken = [&](const WrapToken& tok) -> int {
            if (hasCodeFont && tok.isCode) {
                ctx->SetFontFamily(codeFont);
                int w = ctx->GetTextLineWidth(tok.text);
                ctx->SetFontFamily(regularFont);
                return w;
            }
            return ctx->GetTextLineWidth(tok.text);
        };

        // Helper to measure a composed line width (may contain mixed fonts)
        auto measureLine = [&](const std::string& line) -> int {
            if (hasCodeFont) {
                return MeasureMixedLineWidth(ctx, line, codeFont, regularFont);
            }
            return ctx->GetTextLineWidth(line);
        };

        // Helper to break a single token character-by-character when it exceeds maxWidth.
        // For code tokens: strips backticks before breaking, re-wraps each fragment with backticks.
        auto breakToken = [&](const WrapToken& tok, std::vector<std::string>& lines) -> std::string {
            bool isCode = tok.isCode;
            std::string content = tok.text;

            // For code tokens, strip enclosing backticks and measure/break the inner content
            if (isCode && content.size() >= 2 && content.front() == '`' && content.back() == '`') {
                content = content.substr(1, content.size() - 2);
            }

            if (isCode && hasCodeFont) ctx->SetFontFamily(codeFont);

            // Account for backtick overhead when measuring code fragments
            int backtickOverhead = 0;
            if (isCode) {
                backtickOverhead = ctx->GetTextLineWidth("``");
            }
            int effectiveMax = maxWidth - backtickOverhead;
            if (effectiveMax <= 0) effectiveMax = 1;

            int wordLen = static_cast<int>(utf8_length(content));
            std::string chunk;
            for (int ci = 0; ci < wordLen; ci++) {
                std::string ch = utf8_substr(content, ci, 1);
                std::string testChunk = chunk + ch;
                int chunkWidth = ctx->GetTextLineWidth(testChunk);
                if (chunkWidth > effectiveMax && !chunk.empty()) {
                    lines.push_back(isCode ? ("`" + chunk + "`") : chunk);
                    chunk = ch;
                } else {
                    chunk = testChunk;
                }
            }

            if (isCode && hasCodeFont) ctx->SetFontFamily(regularFont);
            // Return remainder wrapped with backticks if code
            return isCode ? ("`" + chunk + "`") : chunk;
        };

        std::string currentLine;
        int currentLineWidth = 0;
        int spaceWidth = ctx->GetTextLineWidth(" ");

        for (const auto& token : tokens) {
            int tokenWidth = measureToken(token);
            int testWidth = currentLine.empty() ? tokenWidth : currentLineWidth + spaceWidth + tokenWidth;

            if (testWidth <= maxWidth || currentLine.empty()) {
                if (currentLine.empty() && tokenWidth > maxWidth) {
                    // Single token exceeds maxWidth — break it character by character
                    std::string remainder = breakToken(token, wrappedLines);
                    currentLine = remainder;
                    currentLineWidth = measureLine(currentLine);
                } else {
                    currentLine = currentLine.empty() ? token.text : currentLine + " " + token.text;
                    currentLineWidth = testWidth;
                }
            } else {
                // Token doesn't fit — flush current line, start new one
                wrappedLines.push_back(currentLine);

                if (tokenWidth > maxWidth) {
                    std::string remainder = breakToken(token, wrappedLines);
                    currentLine = remainder;
                    currentLineWidth = measureLine(currentLine);
                } else {
                    currentLine = token.text;
                    currentLineWidth = tokenWidth;
                }
            }
        }

        if (!currentLine.empty()) {
            wrappedLines.push_back(currentLine);
        }

        if (wrappedLines.empty()) {
            wrappedLines.push_back("");
        }

        return wrappedLines;
    }

    // Calculate the height of a table row based on wrapped cell content
    static int CalculateTableRowHeight(IRenderContext* ctx, const std::string& line,
                                       int lineHeight, int columnCount,
                                       const std::vector<int>& columnWidths,
                                       const MarkdownHybridStyle& mdStyle,
                                       bool isBold,
                                       const std::string& regularFont = "") {
        auto parsed = ParseTableRow(line);
        if (!parsed.isValid) return lineHeight;

        int padding = static_cast<int>(mdStyle.tableCellPadding);
        int maxLines = 1;

        if (isBold) ctx->SetFontWeight(FontWeight::Bold);

        for (size_t col = 0; col < parsed.cells.size() &&
                             col < static_cast<size_t>(columnCount); col++) {
            int cellWidth = (col < columnWidths.size()) ? columnWidths[col] : 0;
            int contentWidth = cellWidth - padding * 2;
            if (contentWidth <= 0) contentWidth = 1;

            auto wrapped = WrapCellText(ctx, parsed.cells[col], contentWidth,
                                        mdStyle.codeFont, regularFont);
            maxLines = std::max(maxLines, static_cast<int>(wrapped.size()));
        }

        if (isBold) ctx->SetFontWeight(FontWeight::Normal);

        return maxLines * lineHeight;
    }

    static void RenderMarkdownTableRow(IRenderContext* ctx, const std::string& line,
                                       int x, int y, int lineHeight, int rowHeight, int width,
                                       bool isHeaderRow,
                                       const std::vector<TableColumnAlignment>& alignments,
                                       int columnCount,
                                       const TextAreaStyle& style,
                                       const MarkdownHybridStyle& mdStyle,
                                       std::vector<MarkdownHitRect>* hitRects = nullptr,
                                       const std::vector<int>* columnWidths = nullptr,
                                       const std::unordered_map<std::string, std::string>* abbreviations = nullptr,
                                       const std::unordered_map<std::string, std::string>* footnotes = nullptr) {
        auto parsed = ParseTableRow(line);
        if (!parsed.isValid) return;

        int padding = static_cast<int>(mdStyle.tableCellPadding);

        // Calculate total table width from column widths
        int totalTableWidth = 0;
        if (columnWidths && !columnWidths->empty()) {
            for (int w : *columnWidths) {
                totalTableWidth += w;
            }
        } else {
            // Fallback: equal distribution (legacy behavior)
            totalTableWidth = width;
        }

        // Draw header background
        if (isHeaderRow) {
            ctx->SetFillPaint(mdStyle.tableHeaderBackground);
            ctx->FillRectangle(x, y, totalTableWidth, rowHeight);
        }

        // Draw top border line (for all rows)
        ctx->SetStrokePaint(mdStyle.tableBorderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine({x, y}, { x + totalTableWidth, y});

        // Draw bottom border line
        // For header rows: DON'T draw bottom border — the separator line handles it
        // For data rows: draw bottom border
        if (!isHeaderRow) {
            ctx->DrawLine({x, y + rowHeight}, { x + totalTableWidth, y + rowHeight});
        }

        // Draw cells
        int cellX = x;
        for (size_t col = 0; col < parsed.cells.size() && col < static_cast<size_t>(columnCount); col++) {
            // Get column width: from pre-computed widths or equal fallback
            int cellWidth = 0;
            if (columnWidths && col < columnWidths->size()) {
                cellWidth = (*columnWidths)[col];
            } else {
                cellWidth = (columnCount > 0) ? width / columnCount : width;
            }

            // Draw vertical separator
            ctx->SetStrokePaint(mdStyle.tableBorderColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine({cellX, y}, { cellX, y + rowHeight});

            // Determine text alignment for this column
            TextAlignment align = TextAlignment::Left;
            if (col < alignments.size()) {
                switch (alignments[col]) {
                    case TableColumnAlignment::Center: align = TextAlignment::Center; break;
                    case TableColumnAlignment::Right: align = TextAlignment::Right; break;
                    default: align = TextAlignment::Left; break;
                }
            }

            // Set font weight for header
            ctx->SetFontWeight(isHeaderRow ?
                               FontWeight::Bold : FontWeight::Normal);
            ctx->SetTextPaint(style.fontColor);

            // Wrap cell text and render each wrapped line
            const std::string& cellText = parsed.cells[col];
            int contentWidth = cellWidth - padding * 2;
            if (contentWidth <= 0) contentWidth = 1;

            auto wrappedLines = WrapCellText(ctx, cellText, contentWidth,
                                               mdStyle.codeFont, style.fontStyle.fontFamily);

            for (size_t wl = 0; wl < wrappedLines.size(); wl++) {
                int lineY = y + static_cast<int>(wl) * lineHeight;
                int textX = cellX + padding;

                if (align == TextAlignment::Center) {
                    int wlWidth = MeasureMixedLineWidth(ctx, wrappedLines[wl],
                                      mdStyle.codeFont, style.fontStyle.fontFamily);
                    textX = cellX + (cellWidth - wlWidth) / 2;
                } else if (align == TextAlignment::Right) {
                    int wlWidth = MeasureMixedLineWidth(ctx, wrappedLines[wl],
                                      mdStyle.codeFont, style.fontStyle.fontFamily);
                    textX = cellX + cellWidth - padding - wlWidth;
                }

                // Render cell content with inline markdown
                RenderMarkdownLine(ctx, wrappedLines[wl], textX, lineY, lineHeight, style, mdStyle, hitRects, abbreviations, footnotes);
            }

            cellX += cellWidth;
        }

        // Draw right border of last column
        ctx->SetStrokePaint(mdStyle.tableBorderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine({cellX, y}, { cellX, y + rowHeight});

        // Reset font
        ctx->SetFontWeight(FontWeight::Normal);
    }

    static void RenderMarkdownTableSeparator(IRenderContext* ctx,
                                             int x, int y, int lineHeight, int width,
                                             int columnCount,
                                             const MarkdownHybridStyle& mdStyle,
                                             const std::vector<int>* columnWidths = nullptr) {
        // The separator source line (|---|---|) is rendered as an extension
        // of the header row — same background, vertical cell borders, and
        // a bottom border that closes the unified header block.
        // This makes header + separator visually appear as one cell per column.

        // Calculate total table width from column widths
        int totalTableWidth = 0;
        if (columnWidths && !columnWidths->empty()) {
            for (int w : *columnWidths) {
                totalTableWidth += w;
            }
        } else {
            totalTableWidth = width;
        }

        // Fill separator area with header background color (visual continuation)
        ctx->SetFillPaint(mdStyle.tableHeaderBackground);
        ctx->FillRectangle(x, y, totalTableWidth, lineHeight);

        // Draw bottom border of the unified header block
        ctx->SetStrokePaint(mdStyle.tableBorderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine({x, y + lineHeight}, { x + totalTableWidth, y + lineHeight});

        // Draw vertical cell borders (extending header's vertical separators)
        int cellX = x;
        // Left border
        ctx->DrawLine({cellX, y}, { cellX, y + lineHeight});

        for (int col = 0; col < columnCount; col++) {
            int cellWidth = 0;
            if (columnWidths && col < static_cast<int>(columnWidths->size())) {
                cellWidth = (*columnWidths)[col];
            } else {
                cellWidth = (columnCount > 0) ? width / columnCount : width;
            }
            cellX += cellWidth;

            // Draw right border of each column
            ctx->DrawLine({cellX, y}, { cellX, y + lineHeight});
        }
    }
};

// ===== HELPER FUNCTIONS =====

// Check if a line is a horizontal rule (---, ***, ___)
static bool IsMarkdownHorizontalRule(const std::string& trimmed) {
    if (trimmed.length() < 3) return false;

    char ruleChar = trimmed[0];
    if (ruleChar != '-' && ruleChar != '*' && ruleChar != '_') return false;

    for (char c : trimmed) {
        if (c != ruleChar && c != ' ') return false;
    }

    // Count actual rule characters (excluding spaces)
    int count = 0;
    for (char c : trimmed) {
        if (c == ruleChar) count++;
    }
    return count >= 3;
}

// Check if a line is a table row (starts with |)
static bool IsMarkdownTableRow(const std::string& trimmed) {
    if (trimmed.empty()) return false;
    return trimmed[0] == '|';
}

// ===== MAIN DRAWING METHOD =====

/**
 * @brief Draw text with hybrid markdown rendering
 *
 * This method replaces the standard DrawHighlightedText when markdown mode is enabled.
 * Current line (with cursor) shows raw markdown with syntax highlighting.
 * All other lines show formatted markdown (bold, italic, headers, code, etc.)
 *
 * Multi-line state tracking:
 * - Code blocks (``` ... ```) are tracked by pre-scanning the lines array
 * - Table context (header/separator/data rows) is tracked during rendering
 */
void UltraCanvasTextArea::DrawMarkdownHybridText(IRenderContext* context) {
    if (!syntaxTokenizer) return;

    // --- Markdown hybrid style: pick dark/light based on background brightness ---
    bool isDarkBg = (style.backgroundColor.r + style.backgroundColor.g + style.backgroundColor.b) < 384;
    MarkdownHybridStyle mdStyle = isDarkBg ? MarkdownHybridStyle::DarkMode() : MarkdownHybridStyle::Default();
    // Reset cached code block tokenizer language tracking for this render pass
    // (the tokenizer itself persists as a class member to avoid expensive reconstruction)

    context->PushState();
    context->ClipRect(Rect2Df(visibleTextArea.x - 2, visibleTextArea.y, visibleTextArea.width + 2, visibleTextArea.height));
    context->SetFontStyle(style.fontStyle);

    // Get current line index where cursor is located
    auto [cursorLine, cursorCol] = GetLineColumnFromPosition(cursorGraphemePosition);

    // --- Pre-scan: build code block state map for entire document ---
    // Supports both ``` (backtick) and ~~~ (tilde) fenced code blocks
    // Also tracks language per code block for syntax highlighting
    // Also detects 4-space (or 1-tab) indented code blocks
    std::vector<bool> isInsideCodeBlock(lines.size(), false);
    std::vector<bool> isCodeBlockDelimiter(lines.size(), false);
    std::vector<std::string> codeBlockLanguage(lines.size()); // language for each line in a block
    {
        bool inFencedCode = false;
        std::string fenceType;     // "```" or "~~~"
        std::string currentLang;

        for (size_t i = 0; i < lines.size(); i++) {
            std::string trimmed = TrimWhitespace(lines[i]);

            // Check for fenced code block delimiters (``` or ~~~)
            bool isBacktickFence = (trimmed.find("```") == 0);
            bool isTildeFence = (trimmed.find("~~~") == 0);

            if (isBacktickFence || isTildeFence) {
                std::string thisFence = isBacktickFence ? "```" : "~~~";

                if (!inFencedCode) {
                    // Opening fence
                    inFencedCode = true;
                    fenceType = thisFence;
                    isInsideCodeBlock[i] = true;
                    isCodeBlockDelimiter[i] = true;

                    // Extract language from delimiter (e.g., ```python or ~~~python)
                    currentLang.clear();
                    if (trimmed.length() > 3) {
                        std::string langPart = trimmed.substr(3);
                        size_t start = langPart.find_first_not_of(' ');
                        if (start != std::string::npos) {
                            currentLang = langPart.substr(start);
                            // Trim trailing whitespace
                            size_t end = currentLang.find_last_not_of(' ');
                            if (end != std::string::npos) {
                                currentLang = currentLang.substr(0, end + 1);
                            }
                        }
                    }
                    codeBlockLanguage[i] = currentLang;
                } else if (thisFence == fenceType) {
                    // Closing fence (must match opening fence type)
                    isInsideCodeBlock[i] = true;
                    isCodeBlockDelimiter[i] = true;
                    codeBlockLanguage[i] = currentLang;
                    inFencedCode = false;
                    currentLang.clear();
                } else {
                    // Mismatched fence inside code block — treat as code content
                    isInsideCodeBlock[i] = true;
                    codeBlockLanguage[i] = currentLang;
                }
            } else if (inFencedCode) {
                isInsideCodeBlock[i] = true;
                codeBlockLanguage[i] = currentLang;
            }
        }

        // Second pass: detect 4-space / tab indented code blocks
        // (only lines NOT already inside fenced blocks)
        // Indented code blocks require: preceded by blank line, indented 4+ spaces or 1 tab
        for (size_t i = 0; i < lines.size(); i++) {
            if (isInsideCodeBlock[i]) continue;

            const std::string& rawLine = lines[i];
            if (rawLine.empty()) continue;

            // Check if line starts with 4 spaces or a tab
            bool indented = false;
            if (rawLine.length() >= 4 && rawLine.substr(0, 4) == "    ") {
                indented = true;
            } else if (!rawLine.empty() && rawLine[0] == '\t') {
                indented = true;
            }

            if (!indented) continue;

            // Must be preceded by a blank line or another indented code line
            bool validPredecessor = false;
            if (i == 0) {
                validPredecessor = true;
            } else {
                std::string prevTrimmed = TrimWhitespace(lines[i - 1]);
                validPredecessor = prevTrimmed.empty() || isInsideCodeBlock[i - 1];
            }

            if (validPredecessor) {
                isInsideCodeBlock[i] = true;
                // No language for indented code blocks
            }
        }
    }


    // --- Pre-scan: detect table context for visible range ---
    // For each visible line, determine if it's part of a table and its role
    enum class TableLineRole { NoneRole, Header, Separator, DataRow };
    std::vector<TableLineRole> tableRoles(lines.size(), TableLineRole::NoneRole);
    std::vector<int> tableColumnCounts(lines.size(), 0);
    std::vector<std::vector<TableColumnAlignment>> tableAlignments(lines.size());
    // NEW: Per-line storage of pre-computed column widths for the table this line belongs to
    std::vector<std::vector<int>> tableColumnWidths(lines.size());

    // Compute available width for tables from first principles.
    // Cannot rely on visibleTextArea.width because CalculateVisibleArea() checks
    // scrollbar state BEFORE RecalculateDisplayLines() runs, so on first render
    // the vertical scrollbar width may not be subtracted.
    // By the time DrawMarkdownHybridText() runs, displayLines IS populated,
    // so IsNeedVerticalScrollbar() returns the correct answer here.
    int tableAvailableWidth;
    {
        auto bounds = GetBounds();
        tableAvailableWidth = bounds.width - style.padding * 2;
        if (style.showLineNumbers) {
            tableAvailableWidth -= (computedLineNumbersWidth + 5);
        }
        if (IsNeedVerticalScrollbar()) {
            tableAvailableWidth -= 15;
        }
    }
    {
        for (size_t i = 0; i < lines.size(); i++) {
            std::string trimmed = TrimWhitespace(lines[i]);
            if (!IsMarkdownTableRow(trimmed)) continue;

            // Check if next line is a separator (this line is header)
            if (i + 1 < lines.size()) {
                std::string nextTrimmed = TrimWhitespace(lines[i + 1]);
                if (MarkdownInlineRenderer::IsTableSeparatorRow(nextTrimmed)) {
                    // This is a header row
                    auto headerParsed = MarkdownInlineRenderer::ParseTableRow(trimmed);
                    int colCount = static_cast<int>(headerParsed.cells.size());
                    tableRoles[i] = TableLineRole::Header;
                    tableColumnCounts[i] = colCount;

                    // Parse alignments from separator
                    tableAlignments[i] = MarkdownInlineRenderer::ParseTableAlignments(nextTrimmed);

                    // Mark separator
                    tableRoles[i + 1] = TableLineRole::Separator;
                    tableColumnCounts[i + 1] = colCount;
                    tableAlignments[i + 1] = tableAlignments[i];

                    // Mark subsequent data rows
                    for (size_t j = i + 2; j < lines.size(); j++) {
                        std::string dataTrimmed = TrimWhitespace(lines[j]);
                        if (IsMarkdownTableRow(dataTrimmed)) {
                            tableRoles[j] = TableLineRole::DataRow;
                            tableColumnCounts[j] = colCount;
                            tableAlignments[j] = tableAlignments[i];
                        } else {
                            break; // End of table
                        }
                    }

                    // NEW: Calculate content-aware column widths for this table
                    std::vector<int> colWidths =
                            MarkdownInlineRenderer::CalculateTableColumnWidths(
                                    context, lines, i, colCount,
                                    tableAvailableWidth, style, mdStyle);

                    // Store column widths for every line of this table
                    tableColumnWidths[i] = colWidths;          // header
                    tableColumnWidths[i + 1] = colWidths;      // separator
                    for (size_t j = i + 2; j < lines.size(); j++) {
                        std::string dataTrimmed = TrimWhitespace(lines[j]);
                        if (IsMarkdownTableRow(dataTrimmed)) {
                            tableColumnWidths[j] = colWidths;
                        } else {
                            break;
                        }
                    }

                    // Skip past the table we just processed
                    // (the outer loop will still iterate, but roles are already set)
                }
            }
        }
    }

    // --- Normalize table column widths: tables with the same column count
    //     share the MAX column widths for visual consistency ---
    {
        // Group tables by column count (collect header line indices)
        std::unordered_map<int, std::vector<size_t>> tablesByColCount;
        for (size_t i = 0; i < lines.size(); i++) {
            if (tableRoles[i] == TableLineRole::Header && !tableColumnWidths[i].empty()) {
                tablesByColCount[tableColumnCounts[i]].push_back(i);
            }
        }

        // For each group with multiple tables, normalize to MAX widths
        for (auto& [colCount, headerIndices] : tablesByColCount) {
            if (headerIndices.size() <= 1) continue;

            std::vector<int> maxWidths(colCount, 0);
            for (size_t hIdx : headerIndices) {
                for (int c = 0; c < colCount; c++) {
                    maxWidths[c] = std::max(maxWidths[c], tableColumnWidths[hIdx][c]);
                }
            }

            // Clamp normalized widths to available width — MAX across tables
            // can exceed the per-table budget when tables have different column ratios
            int totalMax = 0;
            for (int c = 0; c < colCount; c++) totalMax += maxWidths[c];
            if (totalMax > tableAvailableWidth && totalMax > 0) {
                int assigned = 0;
                for (int c = 0; c < colCount - 1; c++) {
                    maxWidths[c] = static_cast<int>(
                        static_cast<float>(maxWidths[c]) / totalMax * tableAvailableWidth);
                    assigned += maxWidths[c];
                }
                maxWidths[colCount - 1] = tableAvailableWidth - assigned;
            }

            // Apply normalized widths to all lines of every table in this group
            for (size_t hIdx : headerIndices) {
                tableColumnWidths[hIdx] = maxWidths;
                if (hIdx + 1 < lines.size()) tableColumnWidths[hIdx + 1] = maxWidths;
                for (size_t j = hIdx + 2; j < lines.size(); j++) {
                    if (tableRoles[j] == TableLineRole::DataRow) {
                        tableColumnWidths[j] = maxWidths;
                    } else {
                        break;
                    }
                }
            }
        }
    }

    // --- Compute per-row heights for table rows and populate Y offsets ---
    std::vector<int> tableRowHeights(lines.size(), computedLineHeight);
    {
        context->SetFontStyle(style.fontStyle);
        for (size_t i = 0; i < lines.size(); i++) {
            if (tableRoles[i] == TableLineRole::Header && !tableColumnWidths[i].empty()) {
                tableRowHeights[i] = MarkdownInlineRenderer::CalculateTableRowHeight(
                        context, lines[i], computedLineHeight,
                        tableColumnCounts[i], tableColumnWidths[i], mdStyle, true,
                        style.fontStyle.fontFamily);
            } else if (tableRoles[i] == TableLineRole::DataRow && !tableColumnWidths[i].empty()) {
                tableRowHeights[i] = MarkdownInlineRenderer::CalculateTableRowHeight(
                        context, lines[i], computedLineHeight,
                        tableColumnCounts[i], tableColumnWidths[i], mdStyle, false,
                        style.fontStyle.fontFamily);
            }
            // Separator rows keep default computedLineHeight
        }
    }

    // --- Pre-scan: compute ordered list numbering per CommonMark spec ---
    // First item's literal number sets the start; subsequent items increment by 1
    std::vector<int> orderedListNumber(lines.size(), -1);
    {
        int currentOrderNumber = 0;
        int currentLevel = -1;
        bool inOrderedList = false;

        for (size_t i = 0; i < lines.size(); i++) {
            if (isInsideCodeBlock[i] || isCodeBlockDelimiter[i]) {
                inOrderedList = false;
                currentLevel = -1;
                continue;
            }
            std::string trimmed = TrimWhitespace(lines[i]);
            if (trimmed.empty()) {
                inOrderedList = false;
                currentLevel = -1;
                continue;
            }

            // Check if this is an ordered list item
            size_t pos = 0;
            int indent = 0;
            while (pos < lines[i].length() && (lines[i][pos] == ' ' || lines[i][pos] == '\t')) {
                if (lines[i][pos] == '\t') indent += 4; else indent++;
                pos++;
            }
            int nestingLevel = indent / 2;

            if (pos < lines[i].length() && std::isdigit(lines[i][pos])) {
                size_t numStart = pos;
                while (pos < lines[i].length() && std::isdigit(lines[i][pos])) pos++;
                if (pos < lines[i].length() && lines[i][pos] == '.' &&
                    pos + 1 < lines[i].length() && lines[i][pos + 1] == ' ') {
                    int literalNumber = std::stoi(lines[i].substr(numStart, pos - numStart));
                    if (!inOrderedList || nestingLevel != currentLevel) {
                        currentOrderNumber = literalNumber;
                        currentLevel = nestingLevel;
                        inOrderedList = true;
                    } else {
                        currentOrderNumber++;
                    }
                    orderedListNumber[i] = currentOrderNumber;
                    continue;
                }
            }

            // Non-ordered-list line breaks the sequence
            // (unordered list items also break it per CommonMark)
            inOrderedList = false;
            currentLevel = -1;
        }
    }

    // --- Pre-scan: detect definition list structure ---
    // Identifies: term lines (bold), definition lines (": "), lazy continuations (merge),
    // and indented continuation paragraphs
    isDefinitionTermLine.assign(lines.size(), false);
    isDefinitionHiddenLine.assign(lines.size(), false);
    isDefinitionContinuationLine.assign(lines.size(), false);
    definitionMergedText.assign(lines.size(), "");
    {
        for (size_t i = 0; i < lines.size(); i++) {
            if (isInsideCodeBlock[i] || isCodeBlockDelimiter[i]) continue;
            std::string trimmed = TrimWhitespace(lines[i]);
            if (trimmed.length() < 2 || trimmed[0] != ':' || trimmed[1] != ' ') continue;

            // This is a definition line (": ...")
            // Mark the previous non-empty, non-code line as a term
            for (int k = static_cast<int>(i) - 1; k >= 0; k--) {
                if (isInsideCodeBlock[k] || isCodeBlockDelimiter[k]) break;
                std::string prevTrimmed = TrimWhitespace(lines[k]);
                if (prevTrimmed.empty()) continue;
                // Don't mark another definition line as a term
                if (prevTrimmed.length() >= 2 && prevTrimmed[0] == ':' && prevTrimmed[1] == ' ') break;
                isDefinitionTermLine[k] = true;
                break;
            }

            // Build merged text: strip ": " prefix and append lazy continuations
            std::string mergedText = trimmed.substr(2);
            // Strip additional leading whitespace after ": " (e.g., ":   Definition")
            size_t textStart = mergedText.find_first_not_of(" \t");
            if (textStart != std::string::npos && textStart > 0)
                mergedText = mergedText.substr(textStart);

            // Look ahead for lazy continuation lines (non-empty, non-indented, no blank line gap)
            for (size_t j = i + 1; j < lines.size(); j++) {
                if (isInsideCodeBlock[j] || isCodeBlockDelimiter[j]) break;
                const std::string& nextLine = lines[j];
                std::string nextTrimmed = TrimWhitespace(nextLine);
                if (nextTrimmed.empty()) break; // blank line ends lazy continuation
                // If it starts with ": " it's a new definition
                if (nextTrimmed.length() >= 2 && nextTrimmed[0] == ':' && nextTrimmed[1] == ' ') break;
                // If it has leading whitespace, it's not a lazy continuation
                if (!nextLine.empty() && (nextLine[0] == ' ' || nextLine[0] == '\t')) break;
                // This is a lazy continuation line
                mergedText += " " + nextTrimmed;
                isDefinitionHiddenLine[j] = true;
            }

            definitionMergedText[i] = mergedText;

            // Look ahead for indented continuation paragraphs after this definition block
            // Find the end of the current definition + lazy continuations
            size_t afterDef = i + 1;
            while (afterDef < lines.size() && isDefinitionHiddenLine[afterDef]) afterDef++;

            // Check for blank line followed by indented continuation paragraphs
            bool inContinuation = false;
            for (size_t j = afterDef; j < lines.size(); j++) {
                if (isInsideCodeBlock[j] || isCodeBlockDelimiter[j]) break;
                std::string nextTrimmed = TrimWhitespace(lines[j]);
                const std::string& nextLine = lines[j];

                if (nextTrimmed.empty()) {
                    // Blank line - could be between continuation paragraphs
                    if (inContinuation) continue;
                    // First blank line after definition - check if followed by indented content
                    inContinuation = false;
                    continue;
                }

                // Check if line has leading whitespace (indented)
                if (!nextLine.empty() && (nextLine[0] == ' ' || nextLine[0] == '\t')) {
                    isDefinitionContinuationLine[j] = true;
                    inContinuation = true;
                } else {
                    // Non-indented, non-empty line - end of definition block
                    break;
                }
            }
        }
    }

    // --- Pre-scan: extract abbreviation definitions (*[ABBR]: expansion) ---
    markdownAbbreviations.clear();
    isAbbreviationDefinitionLine.assign(lines.size(), false);
    {
        for (size_t i = 0; i < lines.size(); i++) {
            if (isInsideCodeBlock[i]) continue;
            const std::string& line = lines[i];
            size_t pos = 0;
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
            if (pos + 2 < line.size() && line[pos] == '*' && line[pos + 1] == '[') {
                size_t closeBracket = line.find("]:", pos + 2);
                if (closeBracket != std::string::npos) {
                    std::string abbr = line.substr(pos + 2, closeBracket - pos - 2);
                    std::string expansion = line.substr(closeBracket + 2);
                    size_t expStart = expansion.find_first_not_of(" \t");
                    if (expStart != std::string::npos)
                        expansion = expansion.substr(expStart);
                    if (!abbr.empty() && !expansion.empty()) {
                        markdownAbbreviations[abbr] = expansion;
                        isAbbreviationDefinitionLine[i] = true;
                    }
                }
            }
        }
    }

    // --- Pre-scan: extract footnote definitions ([^label]: content) ---
    markdownFootnotes.clear();
    isFootnoteDefinitionLine.assign(lines.size(), false);
    {
        std::string lastFootnoteLabel;
        for (size_t i = 0; i < lines.size(); i++) {
            if (isInsideCodeBlock[i]) { lastFootnoteLabel.clear(); continue; }
            const std::string& line = lines[i];

            // Check for continuation line (4+ spaces or tab indent) of previous footnote
            if (!lastFootnoteLabel.empty() && !line.empty() &&
                (line[0] == '\t' || (line.size() >= 4 && line[0] == ' ' && line[1] == ' ' && line[2] == ' ' && line[3] == ' '))) {
                // Append to previous footnote content
                size_t contentStart = (line[0] == '\t') ? 1 : 4;
                std::string continuation = line.substr(contentStart);
                size_t cs = continuation.find_first_not_of(" \t");
                if (cs != std::string::npos)
                    continuation = continuation.substr(cs);
                if (!continuation.empty()) {
                    markdownFootnotes[lastFootnoteLabel] += " " + continuation;
                }
                isFootnoteDefinitionLine[i] = true;
                continue;
            }

            lastFootnoteLabel.clear();

            // Check for [^label]: content pattern
            size_t pos = 0;
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
            if (pos + 3 < line.size() && line[pos] == '[' && line[pos + 1] == '^') {
                size_t closeBracket = line.find("]:", pos + 2);
                if (closeBracket != std::string::npos) {
                    std::string label = line.substr(pos + 2, closeBracket - pos - 2);
                    std::string content = line.substr(closeBracket + 2);
                    size_t cs = content.find_first_not_of(" \t");
                    if (cs != std::string::npos)
                        content = content.substr(cs);
                    if (!label.empty()) {
                        markdownFootnotes[label] = content;
                        isFootnoteDefinitionLine[i] = true;
                        lastFootnoteLabel = label;
                    }
                }
            }
        }
    }

    // Build anchor map from headings (explicit {#id} and auto-slugs)
    {
        markdownAnchors.clear();
        int lineCount = static_cast<int>(lines.size());
        for (int i = 0; i < lineCount; i++) {
            const std::string& line = lines[i];
            if (line.empty() || line[0] != '#') continue;
            // Skip lines inside code blocks
            if (i < static_cast<int>(isInsideCodeBlock.size()) && isInsideCodeBlock[i]) continue;

            // Count heading level
            size_t lvl = 0;
            while (lvl < line.size() && line[lvl] == '#') lvl++;
            if (lvl == 0 || lvl > 6) continue;
            // Must have space after #
            if (lvl >= line.size() || line[lvl] != ' ') continue;

            size_t textStart = lvl;
            while (textStart < line.size() && line[textStart] == ' ') textStart++;
            std::string headerText = line.substr(textStart);

            // Check for explicit anchor {#id}
            std::string anchorId = StripExplicitAnchor(headerText);
            if (!anchorId.empty()) {
                markdownAnchors[anchorId] = i;
            }

            // Always generate auto-slug as well (allows both forms to work)
            std::string slug = GenerateHeadingSlug(headerText);
            if (!slug.empty() && markdownAnchors.find(slug) == markdownAnchors.end()) {
                markdownAnchors[slug] = i;
            }
        }
    }

    // Build backlink map: for each anchor, find how many internal links point to it.
    // Only store anchors with exactly 1 reference (for single-backlink icon on headers).
    {
        markdownAnchorBacklinks.clear();
        std::unordered_map<std::string, std::pair<int,int>> anchorRefCounts; // anchor -> {count, lastSourceLine}
        int lineCount = static_cast<int>(lines.size());
        for (int i = 0; i < lineCount; i++) {
            if (i < static_cast<int>(isInsideCodeBlock.size()) && isInsideCodeBlock[i]) continue;
            const std::string& line = lines[i];
            size_t pos = 0;
            while ((pos = line.find("](#", pos)) != std::string::npos) {
                size_t anchorStart = pos + 3;
                size_t parenClose = line.find(')', anchorStart);
                if (parenClose != std::string::npos && parenClose > anchorStart) {
                    std::string anchorId = line.substr(anchorStart, parenClose - anchorStart);
                    auto& ref = anchorRefCounts[anchorId];
                    ref.first++;
                    ref.second = i;
                }
                pos = anchorStart;
            }
        }
        for (const auto& [anchorId, refInfo] : anchorRefCounts) {
            if (refInfo.first == 1) {
                markdownAnchorBacklinks[anchorId] = refInfo.second;
            }
        }
    }

    // Build cumulative Y offsets for display lines affected by multi-line table rows
    {
        int dlCount = GetDisplayLineCount();
        markdownLineYOffsets.assign(dlCount, 0);
        int cumulativeOffset = 0;
        for (int di = 0; di < dlCount; di++) {
            markdownLineYOffsets[di] = cumulativeOffset;
            int logLine = displayLines[di].logicalLine;
            if (logLine >= 0 && logLine < static_cast<int>(lines.size())) {
                int extraHeight = tableRowHeights[logLine] - computedLineHeight;
                if (extraHeight > 0 && displayLines[di].startGrapheme == 0) {
                    cumulativeOffset += extraHeight;
                }
            }
        }
    }

    // Clear previous hit rects
    markdownHitRects.clear();

    // Iterate over display lines (not logical lines) for correct word wrap support
    int dlCount = GetDisplayLineCount();
    int startDL = std::max(0, firstVisibleLine - 1);
    int endDL = std::min(dlCount, firstVisibleLine + maxVisibleLines + 1);
    int baseY = visibleTextArea.y - (firstVisibleLine - startDL) * computedLineHeight;

    // Compute the scroll-origin Y offset so table row offsets are relative to the scroll position
    int mdScrollBaseOffset = 0;
    if (editingMode == TextAreaEditingMode::MarkdownHybrid &&
        firstVisibleLine < static_cast<int>(markdownLineYOffsets.size())) {
        mdScrollBaseOffset = markdownLineYOffsets[firstVisibleLine];
    }

    // Cache tokenized lines to avoid re-tokenizing the same logical line
    int lastTokenizedLogicalLine = -1;
    std::vector<SyntaxTokenizer::Token> cachedTokens;

    for (int di = startDL; di < endDL; di++) {
        const auto& dl = displayLines[di];
        int logLine = dl.logicalLine;
        if (logLine < 0 || logLine >= static_cast<int>(lines.size())) continue;

        // Skip abbreviation definition lines (they are metadata, not visible content)
        if (logLine < static_cast<int>(isAbbreviationDefinitionLine.size()) &&
            isAbbreviationDefinitionLine[logLine] && logLine != cursorLine) {
            continue;
        }

        // Skip footnote definition lines (they are metadata, not visible content)
        if (logLine < static_cast<int>(isFootnoteDefinitionLine.size()) &&
            isFootnoteDefinitionLine[logLine] && logLine != cursorLine) {
            continue;
        }

        // Skip definition list lazy continuation lines (merged into the definition line)
        if (logLine < static_cast<int>(isDefinitionHiddenLine.size()) &&
            isDefinitionHiddenLine[logLine] && logLine != cursorLine) {
            continue;
        }

        const std::string& line = lines[logLine];
        int textY = baseY + (di - startDL) * computedLineHeight;
        if (editingMode == TextAreaEditingMode::MarkdownHybrid && di < static_cast<int>(markdownLineYOffsets.size()))
            textY += markdownLineYOffsets[di] - mdScrollBaseOffset;

        int x = visibleTextArea.x;
        if (!wordWrap) x -= horizontalScrollOffset;

        bool isFirstSegment = (dl.startGrapheme == 0);

        // --- CURRENT LINE: Show raw markdown with syntax highlighting ---
        // Use token-clipping approach (like DrawHighlightedText) to handle wrapped segments
        if (logLine == cursorLine) {
            if (logLine != lastTokenizedLogicalLine) {
                cachedTokens = syntaxTokenizer->TokenizeLine(line);
                lastTokenizedLogicalLine = logLine;
            }

            int tokenStartGrapheme = 0;
            for (const auto& token : cachedTokens) {
                int tokenLen = static_cast<int>(utf8_length(token.text));
                int tokenEndGrapheme = tokenStartGrapheme + tokenLen;

                // Check overlap with this display line's grapheme range
                int overlapStart = std::max(tokenStartGrapheme, dl.startGrapheme);
                int overlapEnd = std::min(tokenEndGrapheme, dl.endGrapheme);

                if (overlapStart < overlapEnd) {
                    std::string visibleText;
                    if (overlapStart == tokenStartGrapheme && overlapEnd == tokenEndGrapheme) {
                        visibleText = token.text;
                    } else {
                        int localStart = overlapStart - tokenStartGrapheme;
                        int localLen = overlapEnd - overlapStart;
                        visibleText = utf8_substr(token.text, localStart, localLen);
                    }

                    context->SetFontWeight(FontWeight::Normal);
                    int tokenWidth = context->GetTextLineWidth(visibleText);

                    if (x + tokenWidth >= visibleTextArea.x &&
                        x <= visibleTextArea.x + visibleTextArea.width) {
                        TokenStyle tokenStyle = GetStyleForTokenType(token.type);
                        context->SetTextPaint(tokenStyle.color);
                        context->DrawText(visibleText, {x, textY});
                    }
                    x += tokenWidth;
                }

                tokenStartGrapheme = tokenEndGrapheme;
                if (tokenStartGrapheme >= dl.endGrapheme) break;
            }
            continue;
        }

        // --- OTHER LINES: Show formatted markdown ---

        // Reset to default font settings
        context->SetFontWeight(FontWeight::Normal);
        context->SetFontSlant(FontSlant::Normal);
        context->SetFontSize(style.fontStyle.fontSize);
        context->SetFontFamily(style.fontStyle.fontFamily);

        // --- Code block content ---
        if (isInsideCodeBlock[logLine]) {
            if (!isFirstSegment && !isCodeBlockDelimiter[logLine]) {
                // Wrapped continuation segment: draw code block background + borders
                context->SetFillPaint(mdStyle.codeBlockBackgroundColor);
                context->SetStrokePaint(mdStyle.codeBlockBorderColor);
                context->SetStrokeWidth(0.5);
                context->FillRectangle(x, textY, visibleTextArea.width, computedLineHeight);
                context->DrawLine({x, textY}, { x, textY + computedLineHeight});
                context->DrawLine({x + visibleTextArea.width, textY}, { x + visibleTextArea.width, textY + computedLineHeight});

                context->SetFontFamily(mdStyle.codeFont);
                context->SetFontWeight(FontWeight::Normal);
                context->SetFontSlant(FontSlant::Normal);

                const std::string& lang = codeBlockLanguage[logLine];
                if (!lang.empty() && codeBlockTokenizer) {
                    // Syntax-highlighted wrap: tokenize full line, clip to segment range
                    auto tokens = codeBlockTokenizer->TokenizeLine(line);
                    int tokenStartGrapheme = 0;
                    int tokenX = x + 4;
                    for (const auto& token : tokens) {
                        int tokenLen = static_cast<int>(utf8_length(token.text));
                        int tokenEndGrapheme = tokenStartGrapheme + tokenLen;

                        int overlapStart = std::max(tokenStartGrapheme, dl.startGrapheme);
                        int overlapEnd = std::min(tokenEndGrapheme, dl.endGrapheme);

                        if (overlapStart < overlapEnd) {
                            std::string visibleText;
                            if (overlapStart == tokenStartGrapheme && overlapEnd == tokenEndGrapheme) {
                                visibleText = token.text;
                            } else {
                                int localStart = overlapStart - tokenStartGrapheme;
                                int localLen = overlapEnd - overlapStart;
                                visibleText = utf8_substr(token.text, localStart, localLen);
                            }

                            Color tokenColor = mdStyle.codeBlockTextColor;
                            bool tokenBold = false;
                            switch (token.type) {
                                case TokenType::Keyword:
                                case TokenType::Preprocessor:
                                    tokenColor = mdStyle.codeBlockKeywordColor;
                                    tokenBold = true;
                                    break;
                                case TokenType::String:
                                case TokenType::Character:
                                    tokenColor = mdStyle.codeBlockStringColor;
                                    break;
                                case TokenType::Comment:
                                    tokenColor = mdStyle.codeBlockCommentColor;
                                    break;
                                case TokenType::Number:
                                    tokenColor = mdStyle.codeBlockNumberColor;
                                    break;
                                case TokenType::Function:
                                case TokenType::Type:
                                case TokenType::Builtin:
                                    tokenColor = mdStyle.codeBlockKeywordColor;
                                    break;
                                default:
                                    break;
                            }

                            context->SetFontWeight(tokenBold ? FontWeight::Bold : FontWeight::Normal);
                            context->SetTextPaint(tokenColor);
                            int tokenWidth = context->GetTextLineWidth(visibleText);
                            context->DrawText(visibleText, {tokenX, textY});
                            tokenX += tokenWidth;
                        }

                        tokenStartGrapheme = tokenEndGrapheme;
                        if (tokenStartGrapheme >= dl.endGrapheme) break;
                    }
                } else {
                    // Plain monospace wrap
                    int segLen = dl.endGrapheme - dl.startGrapheme;
                    std::string codeSegment = utf8_substr(line, dl.startGrapheme, segLen);
                    context->SetTextPaint(mdStyle.codeBlockTextColor);
                    context->DrawText(codeSegment, {x + 4, textY});
                }

                context->SetFontFamily(style.fontStyle.fontFamily);
                context->SetFontWeight(FontWeight::Normal);
                continue;
            }

            if (!isFirstSegment) continue; // delimiter lines don't wrap

            std::string trimmed = TrimWhitespace(line);

            if (isCodeBlockDelimiter[logLine]) {
                // This is a ``` or ~~~ delimiter line
                // Determine opening vs closing by checking the previous line:
                // an opening delimiter has no code block line before it
                // (the line before is either out-of-range or not inside a code block).
                bool isOpening = (logLine == 0) ||
                                 !isInsideCodeBlock[logLine - 1] ||
                                 isCodeBlockDelimiter[logLine - 1];
                MarkdownInlineRenderer::RenderMarkdownCodeBlockDelimiter(
                        context, trimmed, x, textY, computedLineHeight,
                        visibleTextArea.width, style, mdStyle, isOpening);
            } else {
                // This is a code content line — use syntax highlighting if language known
                const std::string& lang = codeBlockLanguage[logLine];

                if (!lang.empty()) {
                    // Switch tokenizer language if needed
                    if (lang != codeBlockTokenizerLang || !codeBlockTokenizer) {
                        if (!codeBlockTokenizer) {
                            codeBlockTokenizer = std::make_unique<SyntaxTokenizer>();
                        }

                        // Map common markdown language tags to SyntaxTokenizer names
                        std::string normalizedLang = lang;
                        if (!normalizedLang.empty()) {
                            normalizedLang[0] = std::toupper(normalizedLang[0]);
                        }
                        // Special cases
                        if (lang == "cpp" || lang == "c++") normalizedLang = "C++";
                        else if (lang == "csharp" || lang == "cs" || lang == "c#") normalizedLang = "C#";
                        else if (lang == "js") normalizedLang = "JavaScript";
                        else if (lang == "ts") normalizedLang = "TypeScript";
                        else if (lang == "py") normalizedLang = "Python";
                        else if (lang == "rb") normalizedLang = "Ruby";
                        else if (lang == "rs") normalizedLang = "Rust";
                        else if (lang == "objc") normalizedLang = "Objective-C";
                        else if (lang == "sh" || lang == "bash" || lang == "shell") normalizedLang = "Shell Script";
                        else if (lang == "html" || lang == "htm") normalizedLang = "HTML";
                        else if (lang == "css") normalizedLang = "CSS";
                        else if (lang == "sql") normalizedLang = "SQL";
                        else if (lang == "json") normalizedLang = "JavaScript";
                        else if (lang == "xml") normalizedLang = "HTML";
                        else if (lang == "asm") normalizedLang = "x86 Assembly";
                        else if (lang == "pas" || lang == "pascal" || lang == "delphi") normalizedLang = "Pascal";

                        if (!codeBlockTokenizer->SetLanguage(normalizedLang)) {
                            codeBlockTokenizer->SetLanguageByExtension(lang);
                        }
                        codeBlockTokenizerLang = lang;
                    }

                    // Render syntax-highlighted code line
                    MarkdownInlineRenderer::RenderMarkdownCodeBlockHighlighted(
                            context, line, x, textY, computedLineHeight,
                            visibleTextArea.width, style, mdStyle,
                            codeBlockTokenizer.get(),
                            [this](TokenType type) -> TokenStyle { return GetStyleForTokenType(type); });
                } else {
                    // No language — render as plain monospace code
                    std::string codeLine = line;
                    if (codeBlockLanguage[logLine].empty() && !isCodeBlockDelimiter[logLine]) {
                        if (codeLine.length() >= 4 && codeLine.substr(0, 4) == "    ") {
                            codeLine = codeLine.substr(4);
                        } else if (!codeLine.empty() && codeLine[0] == '\t') {
                            codeLine = codeLine.substr(1);
                        }
                    }
                    MarkdownInlineRenderer::RenderMarkdownCodeBlockLine(
                            context, codeLine, x, textY, computedLineHeight,
                            visibleTextArea.width, style, mdStyle);
                }
            }
            continue;
        }

        // Empty lines
        if (line.empty()) continue;

        std::string trimmed = TrimWhitespace(line);
        if (trimmed.empty()) continue;


        // --- Table rows (block-level: render only on first segment) ---
        if (tableRoles[logLine] != TableLineRole::NoneRole) {
            if (!isFirstSegment) continue;

            const std::vector<int>* colWidths =
                    tableColumnWidths[logLine].empty() ? nullptr : &tableColumnWidths[logLine];

            if (tableRoles[logLine] == TableLineRole::Separator) {
                MarkdownInlineRenderer::RenderMarkdownTableSeparator(
                        context, x, textY, computedLineHeight,
                        visibleTextArea.width, tableColumnCounts[logLine], mdStyle,
                        colWidths);
            } else {
                bool isHeader = (tableRoles[logLine] == TableLineRole::Header);
                int rowHeight = tableRowHeights[logLine];
                MarkdownInlineRenderer::RenderMarkdownTableRow(
                        context, line, x, textY, computedLineHeight,
                        rowHeight, visibleTextArea.width, isHeader,
                        tableAlignments[logLine], tableColumnCounts[logLine],
                        style, mdStyle, &markdownHitRects,
                        colWidths, &markdownAbbreviations, &markdownFootnotes);
            }
            continue;
        }

        // --- Headers (block-level: render only on first segment) ---
        if (trimmed[0] == '#') {
            if (!isFirstSegment) continue;
            MarkdownInlineRenderer::RenderMarkdownHeader(
                    context, trimmed, x, textY, computedLineHeight,
                    style, mdStyle, &markdownHitRects, &markdownAbbreviations, &markdownFootnotes,
                    &markdownAnchorBacklinks, isDarkBg);
            continue;
        }

        // --- Horizontal rules (block-level: render only on first segment) ---
        if (IsMarkdownHorizontalRule(trimmed)) {
            if (!isFirstSegment) continue;
            MarkdownInlineRenderer::RenderMarkdownHorizontalRule(
                    context, x, textY, computedLineHeight,
                    visibleTextArea.width, mdStyle);
            continue;
        }

        // --- Wrappable content: extract this display line's text segment ---
        int segLen = dl.endGrapheme - dl.startGrapheme;
        if (segLen <= 0) continue;
        std::string segment;
        if (dl.startGrapheme == 0 && dl.endGrapheme == GetLineGraphemeCount(logLine)) {
            segment = line; // full line, no substr needed
        } else {
            segment = utf8_substr(line, dl.startGrapheme, segLen);
        }

        // --- Blockquotes ---
        if (isFirstSegment && trimmed[0] == '>') {
            MarkdownInlineRenderer::RenderMarkdownBlockquote(
                    context, segment, x, textY, computedLineHeight,
                    visibleTextArea.width, style, mdStyle, &markdownHitRects, &markdownAbbreviations, &markdownFootnotes);
            continue;
        }
        if (!isFirstSegment && TrimWhitespace(lines[logLine])[0] == '>') {
            // Continuation of a blockquote wrap: render with full blockquote styling
            const std::string& origLine = lines[logLine];
            int depth = 0;
            size_t cp = 0;
            while (cp < origLine.length() && origLine[cp] == ' ') cp++;
            while (cp < origLine.length() && origLine[cp] == '>') {
                depth++;
                cp++;
                if (cp < origLine.length() && origLine[cp] == ' ') cp++;
            }
            if (depth < 1) depth = 1;

            int barStride = mdStyle.quoteNestingStep;

            // Background
            context->SetFillPaint(mdStyle.quoteBackgroundColor);
            context->FillRectangle(x, textY, visibleTextArea.width, computedLineHeight);

            // Vertical bars
            for (int d = 0; d < depth; d++) {
                int barX = x + d * barStride;
                context->SetFillPaint(mdStyle.quoteBarColor);
                context->FillRectangle(barX, textY, mdStyle.quoteBarWidth, computedLineHeight);
            }

            // Italic text with quote color
            int textX = x + (depth - 1) * barStride + mdStyle.quoteIndent;
            context->SetFontSlant(FontSlant::Italic);
            context->SetTextPaint(mdStyle.quoteTextColor);
            MarkdownInlineRenderer::RenderMarkdownLine(
                    context, segment, textX, textY, computedLineHeight,
                    style, mdStyle, &markdownHitRects, &markdownAbbreviations, &markdownFootnotes);
            context->SetFontSlant(FontSlant::Normal);
            continue;
        }

        // --- List items ---
        if (isFirstSegment && IsMarkdownListItem(trimmed)) {
            MarkdownInlineRenderer::RenderMarkdownListItem(
                    context, segment, x, textY, computedLineHeight,
                    style, mdStyle, &markdownHitRects,
                    orderedListNumber[logLine], &markdownAbbreviations, &markdownFootnotes);
            continue;
        }
        if (!isFirstSegment && IsMarkdownListItem(TrimWhitespace(lines[logLine]))) {
            // Continuation of a list item: render with indent
            MarkdownInlineRenderer::RenderMarkdownLine(
                    context, segment, x + mdStyle.listIndent, textY, computedLineHeight,
                    style, mdStyle, &markdownHitRects, &markdownAbbreviations, &markdownFootnotes);
            continue;
        }

        // --- Definition list term: line before a ": " definition (render bold) ---
        if (logLine < static_cast<int>(isDefinitionTermLine.size()) &&
            isDefinitionTermLine[logLine] && logLine != cursorLine) {
            // Wrap in ** so the inline parser produces bold elements
            std::string boldSegment = "**" + segment + "**";
            MarkdownInlineRenderer::RenderMarkdownLine(
                    context, boldSegment, x, textY, computedLineHeight,
                    style, mdStyle, &markdownHitRects, &markdownAbbreviations, &markdownFootnotes);
            continue;
        }

        // --- Definition list definition line: starts with ": " ---
        if (isFirstSegment && trimmed.length() >= 2 && trimmed[0] == ':' && trimmed[1] == ' ') {
            int defIndent = mdStyle.listIndent + 10;
            // Use merged text (includes lazy continuations) if available
            std::string defText;
            if (logLine < static_cast<int>(definitionMergedText.size()) &&
                !definitionMergedText[logLine].empty()) {
                defText = definitionMergedText[logLine];
            } else {
                defText = trimmed.substr(2);
                size_t textStart = defText.find_first_not_of(" \t");
                if (textStart != std::string::npos && textStart > 0)
                    defText = defText.substr(textStart);
            }

            MarkdownInlineRenderer::RenderMarkdownLine(
                    context, defText, x + defIndent, textY, computedLineHeight,
                    style, mdStyle, &markdownHitRects, &markdownAbbreviations, &markdownFootnotes);
            continue;
        }

        // --- Definition list continuation paragraph (indented lines after definition) ---
        if (isFirstSegment && logLine < static_cast<int>(isDefinitionContinuationLine.size()) &&
            isDefinitionContinuationLine[logLine] && logLine != cursorLine) {
            int defIndent = mdStyle.listIndent + 10;
            MarkdownInlineRenderer::RenderMarkdownLine(
                    context, trimmed, x + defIndent, textY, computedLineHeight,
                    style, mdStyle, &markdownHitRects, &markdownAbbreviations, &markdownFootnotes);
            continue;
        }
        // --- Regular text with inline formatting ---
        MarkdownInlineRenderer::RenderMarkdownLine(
                context, segment, x, textY, computedLineHeight,
                style, mdStyle, &markdownHitRects, &markdownAbbreviations, &markdownFootnotes);
    }

    context->PopState();
}

// ---------------------------------------------------------------
// HELPER: Detect markdown list items
// ---------------------------------------------------------------
bool UltraCanvasTextArea::IsMarkdownListItem(const std::string& line) const {
    if (line.empty()) return false;

    size_t pos = 0;
    // Skip leading whitespace
    while (pos < line.length() && (line[pos] == ' ' || line[pos] == '\t')) pos++;

    if (pos >= line.length()) return false;

    // Check for unordered list markers: -, *, + followed by space
    if (line[pos] == '-' || line[pos] == '*' || line[pos] == '+') {
        // Make sure it's not a horizontal rule (--- or ***)
        if (pos + 1 < line.length() && line[pos + 1] == ' ') {
            return true;
        }
        return false;
    }

    // Check for ordered list: digit(s) followed by ". "
    if (std::isdigit(line[pos])) {
        while (pos < line.length() && std::isdigit(line[pos])) pos++;
        if (pos < line.length() && line[pos] == '.') {
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------
// HELPER: Trim whitespace from string
// ---------------------------------------------------------------
std::string UltraCanvasTextArea::TrimWhitespace(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

// ---------------------------------------------------------------
// MARKDOWN LINK/IMAGE CLICK HANDLING
// ---------------------------------------------------------------

bool UltraCanvasTextArea::HandleMarkdownClick(int mouseX, int mouseY) {
    if (editingMode != TextAreaEditingMode::MarkdownHybrid) return false;

    for (const auto& hitRect : markdownHitRects) {
        if (mouseX >= hitRect.bounds.x && mouseX <= hitRect.bounds.x + hitRect.bounds.width &&
            mouseY >= hitRect.bounds.y && mouseY <= hitRect.bounds.y + hitRect.bounds.height) {

            if (hitRect.isAnchorReturn) {
                // Backlink icon click — scroll to the source line that links to this header
                int targetLogLine = std::stoi(hitRect.url);
                int dlCount = GetDisplayLineCount();
                for (int di = 0; di < dlCount; di++) {
                    if (displayLines[di].logicalLine == targetLogLine) {
                        ScrollTo(di);
                        break;
                    }
                }
                return true;
            } else if (hitRect.isImage) {
                // Image click — trigger callback with image path
                if (onMarkdownImageClick) {
                    onMarkdownImageClick(hitRect.url, hitRect.altText);
                    return true;
                }
            } else if (!hitRect.url.empty() && hitRect.url[0] == '#') {
                // Internal anchor link — scroll to the target heading
                std::string anchorId = hitRect.url.substr(1);
                auto it = markdownAnchors.find(anchorId);
                if (it != markdownAnchors.end()) {
                    int targetLogLine = it->second;
                    // Find display line for this logical line
                    int dlCount = GetDisplayLineCount();
                    for (int di = 0; di < dlCount; di++) {
                        if (displayLines[di].logicalLine == targetLogLine) {
                            ScrollTo(di);
                            break;
                        }
                    }
                    return true;
                }
            } else {
                // External link click — trigger callback with URL
                if (onMarkdownLinkClick) {
                    onMarkdownLinkClick(hitRect.url);
                    return true;
                }
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------
// MARKDOWN HOVER HANDLING — update cursor for links/images
// ---------------------------------------------------------------

bool UltraCanvasTextArea::HandleMarkdownHover(int mouseX, int mouseY) {
    if (editingMode != TextAreaEditingMode::MarkdownHybrid) return false;

    for (const auto& hitRect : markdownHitRects) {
        if (mouseX >= hitRect.bounds.x && mouseX <= hitRect.bounds.x + hitRect.bounds.width &&
            mouseY >= hitRect.bounds.y && mouseY <= hitRect.bounds.y + hitRect.bounds.height) {

            if (hitRect.isAnchorReturn) {
                // Show "Return back" tooltip and hand cursor for backlink icon
                auto* win = GetWindow();
                if (win && !hitRect.altText.empty()) {
                    Point2Di tooltipPos = {mouseX, mouseY};
                    UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        win, hitRect.altText, tooltipPos);
                }
                SetMouseCursor(UCMouseCursor::Hand);
                return true;
            }

            if (hitRect.isAbbreviation || hitRect.isFootnote) {
                // Show tooltip with expansion/footnote text; keep default cursor
                auto* win = GetWindow();
                if (win && !hitRect.altText.empty()) {
                    Point2Di tooltipPos = {mouseX, mouseY};
                    UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        win, hitRect.altText, tooltipPos);
                }
                return true;
            }

            SetMouseCursor(UCMouseCursor::Hand);
            return true;
        }
    }

    // Mouse moved off all interactive elements — hide any active tooltip
    UltraCanvasTooltipManager::HideTooltip();
    return false;
}

} // namespace UltraCanvas