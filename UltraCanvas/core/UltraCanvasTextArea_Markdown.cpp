// UltraCanvas/core/UltraCanvasTextArea_Markdown.cpp
// Markdown hybrid rendering enhancement for TextArea
// Shows current line as plain text, all other lines as formatted markdown
// Version: 2.5.0
// Last Modified: 2026-04-12
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
    std::array<float, 6> headerSizeMultipliers = {1.8f, 1.5f, 1.3f, 1.2f, 1.1f, 1.0f};

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
    // INLINE LINE RENDERER — uses ITextLayout with ITextAttribute
    // Builds concatenated display text from parsed elements, applies
    // Pango attributes for styling, handles non-text decorations manually
    // ---------------------------------------------------------------

    static int RenderMarkdownLine(IRenderContext* ctx, const std::string& line,
                                  int x, int y, int lineHeight,
                                  const TextAreaStyle& style,
                                  const MarkdownHybridStyle& mdStyle,
                                  std::vector<MarkdownHitRect>* hitRects = nullptr,
                                  const std::unordered_map<std::string, std::string>* abbreviations = nullptr,
                                  const std::unordered_map<std::string, std::string>* footnotes = nullptr,
                                  const FontStyle* baseFontStyle = nullptr,
                                  const Color* baseColor = nullptr,
                                  int availableWidth = -1) {
        // Use caller-provided font/color or fall back to style defaults
        const FontStyle& layoutFont = baseFontStyle ? *baseFontStyle : style.fontStyle;
        const Color& defaultColor = baseColor ? *baseColor : style.fontColor;

        std::vector<MarkdownInlineElement> elements = ParseInlineMarkdown(line);
        if (abbreviations && !abbreviations->empty()) {
            elements = ApplyAbbreviations(elements, *abbreviations);
        }
        if (footnotes && !footnotes->empty()) {
            elements = ApplyFootnotes(elements, *footnotes);
        }

        // Build concatenated display text with PANGO_ATTR_SHAPE placeholders for images.
        // For each image we insert a space (1 byte) and attach a shape attribute that
        // reserves the image's visual width. After rendering the layout, we use
        // IndexToPos on the placeholder byte to find the image's pixel position and
        // draw the image there.
        struct ElementRange {
            const MarkdownInlineElement* elem;
            int startByte;
            int endByte;
            int imgSize;          // reserved width for images (includes alt text)
            std::string altText;  // extracted alt text for images (drawn after layout)
        };

        std::string displayText;
        std::vector<ElementRange> ranges;
        ranges.reserve(elements.size());

        for (const auto& elem : elements) {
            if (elem.isImage) {
                // Compute reserved width: icon + 4px gap + italic alt text width + 2px
                int imgSize = lineHeight - 2;
                int reservedW = imgSize + 4;
                if (!elem.altText.empty()) {
                    // Measure alt text in italic
                    ctx->PushState();
                    ctx->SetFontStyle(layoutFont);
                    ctx->SetFontSlant(FontSlant::Italic);
                    reservedW += ctx->GetTextLineWidth(elem.altText) + 2;
                    ctx->PopState();
                }

                int startByte = static_cast<int>(displayText.size());
                displayText += ' ';  // 1-byte placeholder
                int endByte = static_cast<int>(displayText.size());
                ranges.push_back({&elem, startByte, endByte, reservedW, elem.altText});
                continue;
            }
            if (elem.text.empty()) continue;

            int startByte = static_cast<int>(displayText.size());
            displayText += elem.text;
            int endByte = static_cast<int>(displayText.size());
            ranges.push_back({&elem, startByte, endByte, 0, ""});
        }

        // Nothing to render
        if (displayText.empty()) return 0;

        // Create the layout for the entire line
        auto layout = ctx->CreateTextLayout(displayText, false);
        layout->SetFontStyle(layoutFont);
        if (availableWidth > 0) {
            layout->SetExplicitWidth(availableWidth);
            layout->SetWrap(TextWrap::WrapWordChar);
        }

        // Insert attributes for each element
        for (const auto& r : ranges) {
            const auto& elem = *r.elem;
            int startByte = r.startByte;
            int endByte = r.endByte;

            // Image: reserve space via shape attribute
            if (elem.isImage) {
                auto shapeAttr = TextAttributeFactory::CreateShapeSpacer(r.imgSize);
                shapeAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(shapeAttr));
                continue;
            }

            // Bold
            if (elem.isBold) {
                auto attr = TextAttributeFactory::CreateWeight(FontWeight::Bold);
                attr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(attr));
            }
            // Italic (also for math)
            if (elem.isItalic || elem.isMath) {
                auto attr = TextAttributeFactory::CreateStyle(FontSlant::Italic);
                attr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(attr));
            }
            // Code spans: different font family + color + background
            if (elem.isCode) {
                auto famAttr = TextAttributeFactory::CreateFamily(mdStyle.codeFont);
                famAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(famAttr));
                auto fgAttr = TextAttributeFactory::CreateForeground(mdStyle.codeTextColor);
                fgAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(fgAttr));
                auto bgAttr = TextAttributeFactory::CreateBackground(mdStyle.codeBackgroundColor);
                bgAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(bgAttr));
            }
            // Links and auto-links
            if (elem.isLink || elem.isAutoLink) {
                auto fgAttr = TextAttributeFactory::CreateForeground(mdStyle.linkColor);
                fgAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(fgAttr));
                if (mdStyle.linkUnderline) {
                    auto ulAttr = TextAttributeFactory::CreateUnderline(UCUnderlineType::UnderlineSingle);
                    ulAttr->SetRange(startByte, endByte);
                    layout->InsertAttribute(std::move(ulAttr));
                    auto ulcAttr = TextAttributeFactory::CreateUnderlineColor(mdStyle.linkColor);
                    ulcAttr->SetRange(startByte, endByte);
                    layout->InsertAttribute(std::move(ulcAttr));
                }
            }
            // Strikethrough
            if (elem.isStrikethrough) {
                auto stAttr = TextAttributeFactory::CreateStrikethrough(true);
                stAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(stAttr));
                auto stcAttr = TextAttributeFactory::CreateStrikethroughColor(mdStyle.strikethroughColor);
                stcAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(stcAttr));
                auto fgAttr = TextAttributeFactory::CreateForeground(mdStyle.strikethroughColor);
                fgAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(fgAttr));
            }
            // Math text color
            if (elem.isMath) {
                auto fgAttr = TextAttributeFactory::CreateForeground(mdStyle.mathTextColor);
                fgAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(fgAttr));
            }
            // Highlight background
            if (elem.isHighlight) {
                auto bgAttr = TextAttributeFactory::CreateBackground(mdStyle.highlightBackground);
                bgAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(bgAttr));
            }
            // Subscript
            if (elem.isSubscript) {
                auto scaleAttr = TextAttributeFactory::CreateScale(0.7);
                scaleAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(scaleAttr));
                auto riseAttr = TextAttributeFactory::CreateRise(static_cast<int>(-lineHeight * 0.15));
                riseAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(riseAttr));
            }
            // Superscript
            if (elem.isSuperscript) {
                auto scaleAttr = TextAttributeFactory::CreateScale(0.7);
                scaleAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(scaleAttr));
                auto riseAttr = TextAttributeFactory::CreateRise(static_cast<int>(lineHeight * 0.25));
                riseAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(riseAttr));
            }
            // Footnote: superscript + colored
            if (elem.isFootnote) {
                auto scaleAttr = TextAttributeFactory::CreateScale(0.7);
                scaleAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(scaleAttr));
                auto riseAttr = TextAttributeFactory::CreateRise(static_cast<int>(lineHeight * 0.25));
                riseAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(riseAttr));
                auto fgAttr = TextAttributeFactory::CreateForeground(mdStyle.footnoteRefColor);
                fgAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(fgAttr));
            }
        }

        // Draw math and abbreviation backgrounds (rounded rects, need manual drawing)
        for (const auto& r : ranges) {
            const auto& elem = *r.elem;
            if (elem.isMath) {
                Rect2Di startPos = layout->IndexToPos(r.startByte);
                Rect2Di endPos = layout->IndexToPos(r.endByte);
                int mathX = x + startPos.x;
                int mathW = endPos.x - startPos.x;
                ctx->SetFillPaint(mdStyle.mathBackgroundColor);
                ctx->FillRoundedRectangle(Rect2Df(mathX - 2, y + 1, mathW + 4, lineHeight - 2), 3);
            }
            if (elem.isAbbreviation) {
                Rect2Di startPos = layout->IndexToPos(r.startByte);
                Rect2Di endPos = layout->IndexToPos(r.endByte);
                int abbrX = x + startPos.x;
                int abbrW = endPos.x - startPos.x;
                ctx->SetFillPaint(mdStyle.abbreviationBackground);
                ctx->FillRoundedRectangle(Rect2Df(abbrX - 1, y + 1, abbrW + 2, lineHeight - 2), 2);
            }
        }

        // Draw the layout text (spacers leave blank space at image positions)
        ctx->SetCurrentPaint(defaultColor);
        ctx->DrawTextLayout(*layout, {x, y});

        // Draw abbreviation dotted underlines (after text)
        for (const auto& r : ranges) {
            const auto& elem = *r.elem;
            if (elem.isAbbreviation) {
                Rect2Di startPos = layout->IndexToPos(r.startByte);
                Rect2Di endPos = layout->IndexToPos(r.endByte);
                int abbrX = x + startPos.x;
                int abbrW = endPos.x - startPos.x;
                int underlineY = y + lineHeight - 2;
                ctx->SetStrokePaint(mdStyle.abbreviationUnderlineColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->SetLineDash(UCDashPattern({mdStyle.abbreviationUnderlineDashLength, mdStyle.abbreviationUnderlineGapLength}));
                ctx->DrawLine({abbrX, underlineY}, {abbrX + abbrW, underlineY});
                ctx->SetLineDash(UCDashPattern()); // reset dash
            }
        }

        // Draw images into the reserved shape-spacer slots
        for (const auto& r : ranges) {
            const auto& elem = *r.elem;
            if (!elem.isImage) continue;
            Rect2Di pos = layout->IndexToPos(r.startByte);
            int imgX = x + pos.x;
            int imgSize = lineHeight - 2;
            int imgY = y + 1;
            ctx->SetFillPaint(mdStyle.imagePlaceholderBackground);
            ctx->FillRectangle(Rect2Df(imgX, imgY, imgSize, imgSize));
            ctx->SetStrokePaint(mdStyle.imagePlaceholderBorderColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRectangle(Rect2Df(imgX, imgY, imgSize, imgSize));
            // Mountain icon
            ctx->SetFillPaint(mdStyle.imagePlaceholderTextColor);
            ctx->ClearPath();
            ctx->MoveTo(imgX + 2, imgY + imgSize - 2);
            ctx->LineTo(imgX + imgSize / 2, imgY + 3);
            ctx->LineTo(imgX + imgSize - 2, imgY + imgSize - 2);
            ctx->ClosePath();
            ctx->Fill();
            if (hitRects) {
                hitRects->push_back({{imgX, imgY, imgSize, imgSize}, elem.url, elem.altText, true});
            }
            // Alt text (italic) next to the icon
            if (!r.altText.empty()) {
                ctx->PushState();
                ctx->SetFontStyle(layoutFont);
                ctx->SetFontSlant(FontSlant::Italic);
                ctx->SetCurrentPaint(mdStyle.imagePlaceholderTextColor);
                ctx->DrawText(r.altText, {imgX + imgSize + 4, y});
                ctx->PopState();
            }
        }

        // Collect hit rects for links, footnotes, abbreviations
        if (hitRects) {
            for (const auto& r : ranges) {
                const auto& elem = *r.elem;
                if (elem.isImage) continue; // already handled above
                Rect2Di startPos = layout->IndexToPos(r.startByte);
                Rect2Di endPos = layout->IndexToPos(r.endByte);
                int elemX = x + startPos.x;
                int elemW = endPos.x - startPos.x;

                if (elem.isLink || elem.isAutoLink) {
                    hitRects->push_back({{elemX, y, elemW, lineHeight}, elem.url, ""});
                }
                if (elem.isFootnote && !elem.footnoteContent.empty()) {
                    MarkdownHitRect fnHr;
                    fnHr.bounds = {elemX, y, elemW, lineHeight};
                    fnHr.altText = elem.footnoteContent;
                    fnHr.isFootnote = true;
                    hitRects->push_back(fnHr);
                }
                if (elem.isAbbreviation) {
                    MarkdownHitRect hr;
                    hr.bounds = {elemX, y, elemW, lineHeight};
                    hr.altText = elem.abbreviationExpansion;
                    hr.isAbbreviation = true;
                    hitRects->push_back(hr);
                }
            }
        }

        return layout->GetLayoutWidth();
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

        // Build header font style
        FontStyle headerFont = style.fontStyle;
        headerFont.fontSize = headerFontSize;
        headerFont.fontWeight = FontWeight::Bold;
        Color headerColor = mdStyle.headerColors[levelIndex];

        // Vertically center the header text within the fixed line height
        ctx->SetFontSize(headerFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        int textHeight = ctx->GetTextLineHeight(headerText.empty() ? "M" : headerText);
        int centeredY = y + (lineHeight - textHeight) / 2;

        // Render inline markdown within header text (links, bold, etc.)
        int renderedWidth = RenderMarkdownLine(ctx, headerText, x, centeredY, lineHeight, style, mdStyle, hitRects, abbreviations, footnotes,
                                               &headerFont, &headerColor);

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
                ctx->DrawImage(iconPath,
                               Rect2Df(static_cast<float>(iconX), static_cast<float>(iconY),
                                       static_cast<float>(iconSize), static_cast<float>(iconSize)),
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
                                       const std::unordered_map<std::string, std::string>* footnotes = nullptr,
                                       int availableWidth = -1) {
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
            ctx->DrawRectangle(Rect2Df(cbX, cbY, cbSize, cbSize));

            if (isTaskChecked) {
                // Fill checkbox
                ctx->SetFillPaint(mdStyle.checkboxCheckedColor);
                ctx->FillRectangle(Rect2Df(cbX + 1, cbY + 1, cbSize - 2, cbSize - 2));

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
        int itemAvailWidth = (availableWidth > 0) ? std::max(1, availableWidth - (bulletX - x)) : -1;
        RenderMarkdownLine(ctx, itemText, bulletX, y, lineHeight, style, mdStyle, hitRects, abbreviations, footnotes,
                           nullptr, nullptr, itemAvailWidth);
    }

    // ---------------------------------------------------------------
    // BLOCKQUOTE RENDERER
    // ---------------------------------------------------------------

    // Returns total rendered height (may span multiple lineHeight rows if text wraps)
    static int RenderMarkdownBlockquote(IRenderContext* ctx, const std::string& line,
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
        int barStride = mdStyle.quoteNestingStep;

        // Text starts after the outermost bar + gap + per-level indent
        int textX = x + (depth - 1) * barStride + mdStyle.quoteIndent;
        int textAvailWidth = std::max(1, (x + width) - textX - mdStyle.quoteIndent);

        FontStyle quoteFont = style.fontStyle;
        quoteFont.fontSlant = FontSlant::Italic;

        // Pre-measure wrapped height so bg/bars can extend across all wrapped rows
        int wrappedLineCount = 1;
        if (!quoteText.empty()) {
            auto measureLayout = ctx->CreateTextLayout(quoteText, false);
            measureLayout->SetFontStyle(quoteFont);
            measureLayout->SetExplicitWidth(textAvailWidth);
            measureLayout->SetWrap(TextWrap::WrapWordChar);
            wrappedLineCount = std::max(1, measureLayout->GetLineCount());
        }
        int totalHeight = wrappedLineCount * lineHeight;

        // Full background covers from x to x+width for all wrapped rows
        ctx->SetFillPaint(mdStyle.quoteBackgroundColor);
        ctx->FillRectangle(Rect2Df(x, y, width, totalHeight));

        // Draw one vertical bar per nesting level for the full height
        for (int d = 0; d < depth; d++) {
            int barX = x + d * barStride;
            ctx->SetFillPaint(mdStyle.quoteBarColor);
            ctx->FillRectangle(Rect2Df(barX, y, mdStyle.quoteBarWidth, totalHeight));
        }

        // Render the quote text with Pango-wrapped layout (single call handles all wrapped rows)
        Color quoteColor = mdStyle.quoteTextColor;
        RenderMarkdownLine(ctx, quoteText, textX, y, lineHeight, style, mdStyle, hitRects, abbreviations, footnotes,
                           &quoteFont, &quoteColor, textAvailWidth);

        return totalHeight;
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
        ctx->FillRectangle(Rect2Df(x, y, width, lineHeight));

        // Draw left/right border accent
        ctx->DrawLine({x, y}, { x, y + lineHeight});
        ctx->DrawLine({x + width, y}, { x + width, y + lineHeight});

        // Create layout with monospace font
        FontStyle codeFont = style.fontStyle;
        codeFont.fontFamily = mdStyle.codeFont;
        auto layout = ctx->CreateTextLayout(line, false);
        layout->SetFontStyle(codeFont);

        ctx->SetTextPaint(mdStyle.codeBlockTextColor);
        ctx->DrawTextLayout(*layout, {x + 4, y});
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
        ctx->FillRectangle(Rect2Df(x, y, width, lineHeight));

        // Draw left/right border accent
        ctx->DrawLine({x, y}, { x, y + lineHeight});
        ctx->DrawLine({x + width, y}, { x + width, y + lineHeight});

        // Create layout with monospace font
        FontStyle codeFont = style.fontStyle;
        codeFont.fontFamily = mdStyle.codeFont;

        auto layout = ctx->CreateTextLayout(line, false);
        layout->SetFontStyle(codeFont);

        if (tokenizer) {
            auto tokens = tokenizer->TokenizeLine(line);
            int currentByte = 0;

            for (const auto& token : tokens) {
                int startByte = currentByte;
                int endByte = currentByte + static_cast<int>(token.text.size());

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

                auto fgAttr = TextAttributeFactory::CreateForeground(tokenColor);
                fgAttr->SetRange(startByte, endByte);
                layout->InsertAttribute(std::move(fgAttr));

                if (tokenBold) {
                    auto wAttr = TextAttributeFactory::CreateWeight(FontWeight::Bold);
                    wAttr->SetRange(startByte, endByte);
                    layout->InsertAttribute(std::move(wAttr));
                }
                currentByte = endByte;
            }
        }

        ctx->SetTextPaint(mdStyle.codeBlockTextColor);
        ctx->DrawTextLayout(*layout, {x + 4, y});
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
        ctx->FillRectangle(Rect2Df(x, bgY, width, bgHeight));
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
            ctx->FillRectangle(Rect2Df(x, y, totalTableWidth, rowHeight));
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

            // Build cell font style (bold for header rows)
            FontStyle cellFont = style.fontStyle;
            if (isHeaderRow) cellFont.fontWeight = FontWeight::Bold;
            ctx->SetFontWeight(isHeaderRow ? FontWeight::Bold : FontWeight::Normal);

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
                RenderMarkdownLine(ctx, wrappedLines[wl], textX, lineY, lineHeight, style, mdStyle, hitRects, abbreviations, footnotes,
                                   &cellFont);
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
        ctx->FillRectangle(Rect2Df(x, y, totalTableWidth, lineHeight));

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

// Walk every cached LineLayoutBase's hitRects (recorded in layout-local coords by the MD parser)
// and translate them to screen coords using the same convention as RenderLineLayout. Returns
// a pointer to the matching MarkdownHitRect or nullptr.
const MarkdownHitRect* UltraCanvasTextArea::FindHitRectAtScreenPos(int mouseX, int mouseY) const {
    for (const auto& ll : lineLayouts) {
        if (!ll) continue;
        int originX = visibleTextArea.x + ll->bounds.x + ll->layoutShift.x;
        int originY = visibleTextArea.y + ll->bounds.y + ll->layoutShift.y - verticalScrollOffset;
        for (const auto& r : ll->hitRects) {
            int sx = originX + r.bounds.x;
            int sy = originY + r.bounds.y;
            if (mouseX >= sx && mouseX <= sx + r.bounds.width &&
                mouseY >= sy && mouseY <= sy + r.bounds.height) {
                return &r;
            }
        }
    }
    return nullptr;
}

bool UltraCanvasTextArea::HandleMarkdownClick(int mouseX, int mouseY) {
    if (editingMode != TextAreaEditingMode::MarkdownHybrid) return false;

    const MarkdownHitRect* hit = FindHitRectAtScreenPos(mouseX, mouseY);
    if (!hit) return false;

    if (hit->isAnchorReturn) {
        int targetLogLine = std::stoi(hit->url);
        ScrollTo(targetLogLine);
        return true;
    }
    if (hit->isImage) {
        if (onMarkdownImageClick) {
            onMarkdownImageClick(hit->url, hit->altText);
            return true;
        }
        return false;
    }
    if (!hit->url.empty() && hit->url[0] == '#') {
        std::string anchorId = hit->url.substr(1);
        auto it = markdownAnchors.find(anchorId);
        if (it != markdownAnchors.end()) {
            ScrollTo(it->second);
            return true;
        }
        return false;
    }
    if (onMarkdownLinkClick) {
        onMarkdownLinkClick(hit->url);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------
// MARKDOWN HOVER HANDLING — update cursor for links/images
// ---------------------------------------------------------------

bool UltraCanvasTextArea::HandleMarkdownHover(int mouseX, int mouseY) {
    if (editingMode != TextAreaEditingMode::MarkdownHybrid) return false;

    const MarkdownHitRect* hit = FindHitRectAtScreenPos(mouseX, mouseY);
    if (!hit) {
        UltraCanvasTooltipManager::HideTooltip();
        return false;
    }

    if (hit->isAnchorReturn) {
        if (auto* win = GetWindow(); win && !hit->altText.empty()) {
            UltraCanvasTooltipManager::UpdateAndShowTooltip(win, hit->altText, {mouseX, mouseY});
        }
        SetMouseCursor(UCMouseCursor::Hand);
        return true;
    }
    if (hit->isAbbreviation || hit->isFootnote) {
        if (auto* win = GetWindow(); win && !hit->altText.empty()) {
            UltraCanvasTooltipManager::UpdateAndShowTooltip(win, hit->altText, {mouseX, mouseY});
        }
        return true;
    }
    SetMouseCursor(UCMouseCursor::Hand);
    return true;
}

} // namespace UltraCanvas
