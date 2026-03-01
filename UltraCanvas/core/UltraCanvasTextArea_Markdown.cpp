// UltraCanvas/core/UltraCanvasTextArea_Markdown.cpp
// Markdown hybrid rendering enhancement for TextArea
// Shows current line as plain text, all other lines as formatted markdown
// Version: 2.3.3
// Last Modified: 2026-02-26
// Author: UltraCanvas Framework

#include "UltraCanvasTextArea.h"
#include "UltraCanvasSyntaxTokenizer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasUtils.h"
#include "Plugins/Text/UltraCanvasMarkdown.h"
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
    Color codeBlockBorderColor = Color(220, 220, 220);
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
    std::string bulletCharacter = "\xe2\x80\xa2"; // UTF-8 bullet •
    Color bulletColor = Color(80, 80, 80);

    // Quote styling
    Color quoteBarColor = Color(200, 200, 200);
    Color quoteBackgroundColor = Color(250, 250, 250);
    Color quoteTextColor = Color(100, 100, 100);
    int quoteBarWidth = 4;
    int quoteIndent = 15;

    // Horizontal rule
    Color horizontalRuleColor = Color(200, 200, 200);
    float horizontalRuleHeight = 2.0f;

    // Table styling
    Color tableBorderColor = Color(200, 200, 200);
    Color tableHeaderBackground = Color(240, 240, 240);
    float tableCellPadding = 4.0f;

    // Strikethrough
    Color strikethroughColor = Color(120, 120, 120);

    // Highlight
    Color highlightBackground = Color(255, 255, 0, 100);

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
    std::string url;
    std::string altText;
};

// ===== MATH / GREEK LETTER SUBSTITUTION =====
// Converts LaTeX-style commands to Unicode characters for display

static std::string SubstituteGreekLetters(const std::string& input) {
    // Map of LaTeX commands to UTF-8 Unicode characters
    static const std::vector<std::pair<std::string, std::string>> greekMap = {
        // Lowercase Greek
        {"\\alpha",    "\xCE\xB1"},   // α
        {"\\beta",     "\xCE\xB2"},   // β
        {"\\gamma",    "\xCE\xB3"},   // γ
        {"\\delta",    "\xCE\xB4"},   // δ
        {"\\epsilon",  "\xCE\xB5"},   // ε
        {"\\zeta",     "\xCE\xB6"},   // ζ
        {"\\eta",      "\xCE\xB7"},   // η
        {"\\theta",    "\xCE\xB8"},   // θ
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
        {"\\leq",      "\xE2\x89\xA4"},   // ≤
        {"\\geq",      "\xE2\x89\xA5"},   // ≥
        {"\\neq",      "\xE2\x89\xA0"},   // ≠
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
        {"\\leftarrow","\xE2\x86\x90"},   // ←
        {"\\Rightarrow","\xE2\x87\x92"},  // ⇒
        {"\\Leftarrow", "\xE2\x87\x90"},  // ⇐
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
        // Fractions
        {"\\frac{1}{2}", "\xC2\xBD"},       // ½
        {"\\frac{1}{3}", "\xE2\x85\x93"},   // ⅓
        {"\\frac{2}{3}", "\xE2\x85\x94"},   // ⅔
        {"\\frac{1}{4}", "\xC2\xBC"},       // ¼
        {"\\frac{3}{4}", "\xC2\xBE"},       // ¾
    };

    std::string result = input;

    // Replace longer patterns first to avoid partial matches
    // The vector is already ordered with longer patterns first within categories
    for (const auto& [pattern, replacement] : greekMap) {
        size_t searchPos = 0;
        while ((searchPos = result.find(pattern, searchPos)) != std::string::npos) {
            result.replace(searchPos, pattern.length(), replacement);
            searchPos += replacement.length();
        }
    }

    // Clean up remaining LaTeX formatting that we can simplify:
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
    // INLINE PARSER — handles bold, italic, bold+italic, code,
    //                 strikethrough, highlight, links, images
    // ---------------------------------------------------------------

    static std::vector<MarkdownInlineElement> ParseInlineMarkdown(const std::string& line) {
        std::vector<MarkdownInlineElement> elements;
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
                size_t altEnd = line.find(']', pos + 2);
                if (altEnd != std::string::npos && altEnd + 1 < len && line[altEnd + 1] == '(') {
                    size_t urlEnd = line.find(')', altEnd + 2);
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
                size_t textEnd = line.find(']', pos + 1);
                if (textEnd != std::string::npos && textEnd + 1 < len && line[textEnd + 1] == '(') {
                    size_t urlEnd = line.find(')', textEnd + 2);
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
                // consume the [ as plain text and advance to avoid re-matching
                if (!parsed) {
                    // Check if this is a footnote ref [^1] — render as superscript-style text
                    if (textEnd != std::string::npos && pos + 1 < len && line[pos + 1] == '^') {
                        std::string refText = line.substr(pos, textEnd - pos + 1);
                        elem.text = refText;
                        elem.isCode = false; // Render as plain text (footnote marker)
                        elements.push_back(elem);
                        pos = textEnd + 1;
                        parsed = true;
                    } else {
                        // Just a [ character — consume it as plain text
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
                        elem.text = line.substr(pos + 3, end - pos - 3);
                        elem.isBold = true;
                        elem.isItalic = true;
                        elements.push_back(elem);
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
                    if (end != std::string::npos) {
                        elem.text = line.substr(pos + 2, end - pos - 2);
                        elem.isBold = true;
                        elements.push_back(elem);
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
                                elem.text = line.substr(pos + 1, end - pos - 1);
                                elem.isItalic = true;
                                elements.push_back(elem);
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
    // INLINE LINE RENDERER — renders parsed inline elements
    // ---------------------------------------------------------------

    static int RenderMarkdownLine(IRenderContext* ctx, const std::string& line,
                                  int x, int y, int lineHeight,
                                  const TextAreaStyle& style,
                                  const MarkdownHybridStyle& mdStyle,
                                  std::vector<MarkdownHitRect>* hitRects = nullptr) {

        std::vector<MarkdownInlineElement> elements = ParseInlineMarkdown(line);
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
                    ctx->DrawText(elem.altText, currentX, y);
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
                ctx->FillRoundedRectangle(currentX - 2, y + 1, textWidth + 4, lineHeight - 2, 3);

                // Draw math text in italic
                ctx->SetFontSlant(FontSlant::Italic);
                ctx->SetFontWeight(FontWeight::Normal);
                ctx->SetTextPaint(mdStyle.mathTextColor);
                ctx->DrawText(elem.text, currentX, y);

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

                ctx->DrawText(elem.text, currentX, y);

                // Draw underline
                if (mdStyle.linkUnderline) {
                    int underlineY = y + lineHeight - 3;
                    ctx->SetStrokePaint(mdStyle.linkColor);
                    ctx->SetStrokeWidth(1.0f);
                    ctx->DrawLine(currentX, underlineY, currentX + textWidth, underlineY);
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
                float origSize = style.fontStyle.fontSize;
                float subSize = origSize * 0.7f;
                ctx->SetFontSize(subSize);
                ctx->SetFontWeight(FontWeight::Normal);
                ctx->SetFontSlant(FontSlant::Normal);
                ctx->SetTextPaint(style.fontColor);

                int textWidth = ctx->GetTextLineWidth(elem.text);
                int subY = y + static_cast<int>(lineHeight * 0.3f); // Lower position
                ctx->DrawText(elem.text, currentX, subY);

                ctx->SetFontSize(origSize);
                currentX += textWidth;
                continue;
            }

            // --- Superscript element: rendered smaller and higher ---
            if (elem.isSuperscript) {
                float origSize = style.fontStyle.fontSize;
                float supSize = origSize * 0.7f;
                ctx->SetFontSize(supSize);
                ctx->SetFontWeight(FontWeight::Normal);
                ctx->SetFontSlant(FontSlant::Normal);
                ctx->SetTextPaint(style.fontColor);

                int textWidth = ctx->GetTextLineWidth(elem.text);
                int supY = y - static_cast<int>(lineHeight * 0.15f); // Higher position
                ctx->DrawText(elem.text, currentX, supY);

                ctx->SetFontSize(origSize);
                currentX += textWidth;
                continue;
            }

            // --- Emoji element: rendered at normal size ---
            if (elem.isEmoji) {
                ctx->SetFontWeight(FontWeight::Normal);
                ctx->SetFontSlant(FontSlant::Normal);
                ctx->SetTextPaint(style.fontColor);
                ctx->DrawText(elem.text, currentX, y);
                currentX += ctx->GetTextLineWidth(elem.text);
                continue;
            }

            // --- Auto-linked URL: rendered as a clickable link ---
            if (elem.isAutoLink) {
                ctx->SetFontWeight(FontWeight::Normal);
                ctx->SetFontSlant(FontSlant::Normal);
                ctx->SetTextPaint(mdStyle.linkColor);
                int textWidth = ctx->GetTextLineWidth(elem.text);

                ctx->DrawText(elem.text, currentX, y);

                // Draw underline
                if (mdStyle.linkUnderline) {
                    int underlineY = y + lineHeight - 3;
                    ctx->SetStrokePaint(mdStyle.linkColor);
                    ctx->SetStrokeWidth(1.0f);
                    ctx->DrawLine(currentX, underlineY, currentX + textWidth, underlineY);
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

            // --- Determine formatting ---
            FontWeight weight = elem.isBold ? FontWeight::Bold : FontWeight::Normal;
            FontSlant slant = elem.isItalic ? FontSlant::Italic : FontSlant::Normal;
            Color color = style.fontColor;

            if (elem.isCode) {
                color = mdStyle.codeTextColor;
            }

            ctx->SetFontWeight(weight);
            ctx->SetFontSlant(slant);

            int textWidth = ctx->GetTextLineWidth(elem.text);

            // --- Highlight background ---
            if (elem.isHighlight) {
                ctx->SetFillPaint(mdStyle.highlightBackground);
                ctx->FillRectangle(currentX - 1, y, textWidth + 2, lineHeight);
            }

            // --- Code background ---
            if (elem.isCode) {
                ctx->SetFillPaint(mdStyle.codeBackgroundColor);
                ctx->FillRoundedRectangle(currentX - 2, y + 1, textWidth + 4, lineHeight - 2, 3);
                ctx->SetFontFamily(mdStyle.codeFont);
            }

            // --- Draw the text ---
            ctx->SetTextPaint(elem.isStrikethrough ? mdStyle.strikethroughColor : color);
            ctx->DrawText(elem.text, currentX, y);

            // --- Strikethrough line ---
            if (elem.isStrikethrough) {
                int strikeY = y + lineHeight / 2;
                ctx->SetStrokePaint(mdStyle.strikethroughColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawLine(currentX, strikeY, currentX + textWidth, strikeY);
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
                                     std::vector<MarkdownHitRect>* hitRects = nullptr) {
        // Detect header level
        int level = 0;
        size_t pos = 0;
        while (pos < line.length() && line[pos] == '#') {
            level++;
            pos++;
        }

        if (level == 0 || level > 6) {
            RenderMarkdownLine(ctx, line, x, y, lineHeight, style, mdStyle, hitRects);
            return;
        }

        // Skip whitespace after #
        while (pos < line.length() && line[pos] == ' ') pos++;

        std::string headerText = line.substr(pos);
        int levelIndex = std::clamp(level - 1, 0, 5);

        // Set header font size — clamped to not exceed line height
        float baseFontSize = style.fontStyle.fontSize;
        float headerFontSize = baseFontSize * mdStyle.headerSizeMultipliers[levelIndex];

        // Clamp font size so it doesn't overflow the fixed row height
        float maxFontSize = static_cast<float>(lineHeight) * 0.85f;
        headerFontSize = std::min(headerFontSize, maxFontSize);

        ctx->SetFontSize(headerFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(mdStyle.headerColors[levelIndex]);

        // Render inline markdown within header text (links, bold, etc.)
        // For headers, we render the text directly since inline formatting
        // within headers is a rare edge case and the header already has styling
        ctx->DrawText(headerText, x, y);

        // Draw underline for H1 and H2 (visual distinction)
        if (level <= 2) {
            int underlineY = y + lineHeight - 2;
            ctx->SetStrokePaint(mdStyle.horizontalRuleColor);
            ctx->SetStrokeWidth(level == 1 ? 2.0f : 1.0f);
            // We don't know the full width here, so use text width + some padding
            int textW = ctx->GetTextLineWidth(headerText);
            ctx->DrawLine(x, underlineY, x + textW + 20, underlineY);
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
                                       std::vector<MarkdownHitRect>* hitRects = nullptr) {
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
                orderNumber = std::stoi(line.substr(numStart, pos - numStart));
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

        // --- Draw bullet/number/checkbox ---
        if (isTaskList) {
            // Draw checkbox
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
                ctx->DrawLine(cbX + 3, cy, cx - 1, cbY + cbSize - 3);
                ctx->DrawLine(cx - 1, cbY + cbSize - 3, cbX + cbSize - 3, cbY + 3);
            }

            bulletX += cbSize + 6;
        } else if (isOrdered) {
            // Draw order number
            std::string numberStr = std::to_string(orderNumber) + ".";
            ctx->SetFontWeight(FontWeight::Normal);
            ctx->SetTextPaint(mdStyle.bulletColor);
            ctx->DrawText(numberStr, bulletX, y);
            int numWidth = ctx->GetTextLineWidth(numberStr);
            bulletX += numWidth + 4;
        } else {
            // Draw bullet character
            ctx->SetTextPaint(mdStyle.bulletColor);

            // Alternate bullet style per nesting level
            if (nestingLevel % 3 == 0) {
                // Filled circle (bullet character)
                ctx->DrawText(mdStyle.bulletCharacter, bulletX, y);
            } else if (nestingLevel % 3 == 1) {
                // Open circle
                int circleR = 3;
                int circleY = y + lineHeight / 2;
                ctx->SetStrokePaint(mdStyle.bulletColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawCircle(bulletX + circleR + 1, circleY, circleR);
            } else {
                // Small filled square
                int sqSize = 5;
                int sqY = y + (lineHeight - sqSize) / 2;
                ctx->SetFillPaint(mdStyle.bulletColor);
                ctx->FillRectangle(bulletX + 1, sqY, sqSize, sqSize);
            }

            int bulletCharWidth = ctx->GetTextLineWidth(mdStyle.bulletCharacter);
            bulletX += bulletCharWidth + 4;
        }

        // --- Draw item text with inline formatting ---
        // Checked and unchecked task items both render with normal inline formatting
        // The checkbox itself indicates completion — no strikethrough needed
        RenderMarkdownLine(ctx, itemText, bulletX, y, lineHeight, style, mdStyle, hitRects);
    }

    // ---------------------------------------------------------------
    // BLOCKQUOTE RENDERER
    // ---------------------------------------------------------------

    static void RenderMarkdownBlockquote(IRenderContext* ctx, const std::string& line,
                                         int x, int y, int lineHeight, int width,
                                         const TextAreaStyle& style,
                                         const MarkdownHybridStyle& mdStyle,
                                         std::vector<MarkdownHitRect>* hitRects = nullptr) {
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

        // Draw quote background
        int quoteX = x + (depth - 1) * (mdStyle.quoteBarWidth + 8);
        ctx->SetFillPaint(mdStyle.quoteBackgroundColor);
        ctx->FillRectangle(quoteX, y, width - (quoteX - x), lineHeight);

        // Draw vertical bar(s) for each nesting level
        for (int d = 0; d < depth; d++) {
            int barX = x + d * (mdStyle.quoteBarWidth + 8);
            ctx->SetFillPaint(mdStyle.quoteBarColor);
            ctx->FillRectangle(barX, y, mdStyle.quoteBarWidth, lineHeight);
        }

        // Draw text with inline formatting
        int textX = quoteX + mdStyle.quoteIndent;
        ctx->SetFontSlant(FontSlant::Italic);
        ctx->SetTextPaint(mdStyle.quoteTextColor);
        RenderMarkdownLine(ctx, quoteText, textX, y, lineHeight, style, mdStyle, hitRects);
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
        ctx->FillRectangle(x - 4, y, width, lineHeight);

        // Draw left border accent
        ctx->SetFillPaint(mdStyle.codeBlockBorderColor);
        ctx->FillRectangle(x - 4, y, 3, lineHeight);

        // Draw code text in monospace
        ctx->SetFontFamily(mdStyle.codeFont);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetFontSlant(FontSlant::Normal);
        ctx->SetTextPaint(mdStyle.codeBlockTextColor);
        ctx->DrawText(line, x + 4, y);

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
        ctx->FillRectangle(x - 4, y, width, lineHeight);

        // Draw left border accent
        ctx->SetFillPaint(mdStyle.codeBlockBorderColor);
        ctx->FillRectangle(x - 4, y, 3, lineHeight);

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
                ctx->DrawText(token.text, tokenX, y);
                tokenX += tokenWidth;
            }
        } else {
            // Fallback: render as plain monospace
            ctx->SetTextPaint(mdStyle.codeBlockTextColor);
            ctx->DrawText(line, x + 4, y);
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
                                                  const MarkdownHybridStyle& mdStyle) {
        // Draw background
        ctx->SetFillPaint(mdStyle.codeBlockBackgroundColor);
        ctx->FillRectangle(x - 4, y, width, lineHeight);

        // Draw left border accent
        ctx->SetFillPaint(mdStyle.codeBlockBorderColor);
        ctx->FillRectangle(x - 4, y, 3, lineHeight);

        // Delimiter lines (``` or ~~~) are rendered as empty accent bars
        // The language tag is internal metadata used for syntax highlighting
        // and is NOT displayed as visible text
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
        ctx->DrawLine(x, ruleY, x + width, ruleY);
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

    static void RenderMarkdownTableRow(IRenderContext* ctx, const std::string& line,
                                       int x, int y, int lineHeight, int width,
                                       bool isHeaderRow,
                                       const std::vector<TableColumnAlignment>& alignments,
                                       int columnCount,
                                       const TextAreaStyle& style,
                                       const MarkdownHybridStyle& mdStyle,
                                       std::vector<MarkdownHitRect>* hitRects = nullptr) {
        auto parsed = ParseTableRow(line);
        if (!parsed.isValid) return;

        int availableWidth = width;
        int cellWidth = (columnCount > 0) ? availableWidth / columnCount : availableWidth;
        int padding = static_cast<int>(mdStyle.tableCellPadding);

        // Draw header background
        if (isHeaderRow) {
            ctx->SetFillPaint(mdStyle.tableHeaderBackground);
            ctx->FillRectangle(x, y, availableWidth, lineHeight);
        }

        // Draw top border line (for all rows)
        ctx->SetStrokePaint(mdStyle.tableBorderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(x, y, x + availableWidth, y);

        // Draw bottom border line
        // For header rows: DON'T draw bottom border — the separator line handles it
        // For data rows: draw bottom border
        if (!isHeaderRow) {
            ctx->DrawLine(x, y + lineHeight, x + availableWidth, y + lineHeight);
        }

        // Draw cells
        int cellX = x;
        for (size_t col = 0; col < parsed.cells.size() && col < static_cast<size_t>(columnCount); col++) {
            // Draw vertical separator
            ctx->SetStrokePaint(mdStyle.tableBorderColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(cellX, y, cellX, y + lineHeight);

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
            ctx->SetFontWeight(isHeaderRow ? FontWeight::Bold : FontWeight::Normal);
            ctx->SetTextPaint(style.fontColor);

            // Draw cell text
            const std::string& cellText = parsed.cells[col];
            int textX = cellX + padding;
            int textWidth = ctx->GetTextLineWidth(cellText);
            int contentWidth = cellWidth - padding * 2;

            if (align == TextAlignment::Center) {
                textX = cellX + (cellWidth - textWidth) / 2;
            } else if (align == TextAlignment::Right) {
                textX = cellX + cellWidth - padding - textWidth;
            }

            // Render cell content with inline markdown
            RenderMarkdownLine(ctx, cellText, textX, y, lineHeight, style, mdStyle, hitRects);

            cellX += cellWidth;
        }

        // Draw right border of last column
        ctx->SetStrokePaint(mdStyle.tableBorderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(cellX, y, cellX, y + lineHeight);

        // Reset font
        ctx->SetFontWeight(FontWeight::Normal);
    }

    static void RenderMarkdownTableSeparator(IRenderContext* ctx,
                                             int x, int y, int lineHeight, int width,
                                             int columnCount,
                                             const MarkdownHybridStyle& mdStyle) {
        // Option A: The separator source line (|---|---|) is rendered as a thin
        // horizontal divider between header and data rows.
        // It occupies its source line space but does NOT render as a full row
        // with cell borders — just a single clean horizontal line.

        int separatorY = y + lineHeight / 2;
        ctx->SetStrokePaint(mdStyle.tableBorderColor);
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawLine(x, separatorY, x + width, separatorY);
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

    context->PushState();
    context->ClipRect(visibleTextArea);
    context->SetFontStyle(style.fontStyle);

    // Get current line index where cursor is located
    auto [cursorLine, cursorCol] = GetLineColumnFromPosition(cursorGraphemePosition);

    // Calculate visible lines
    int startLine = std::max(0, firstVisibleLine - 1);
    int endLine = std::min(static_cast<int>(lines.size()), firstVisibleLine + maxVisibleLines + 1);

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

    // --- Create a temporary SyntaxTokenizer for code block highlighting ---
    // Reused across all code block lines of the same language
    std::unique_ptr<SyntaxTokenizer> codeBlockTokenizer;
    std::string currentCodeBlockLang;

    // --- Pre-scan: detect table context for visible range ---
    // For each visible line, determine if it's part of a table and its role
    enum class TableLineRole { NoneRole, Header, Separator, DataRow };
    std::vector<TableLineRole> tableRoles(lines.size(), TableLineRole::NoneRole);
    std::vector<int> tableColumnCounts(lines.size(), 0);
    std::vector<std::vector<TableColumnAlignment>> tableAlignments(lines.size());
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

                    // Skip past the table we just processed
                    // (the outer loop will still iterate, but roles are already set)
                }
            }
        }
    }

    // --- Markdown hybrid style ---
    MarkdownHybridStyle mdStyle = MarkdownHybridStyle::Default();

    // Clear previous hit rects
    markdownHitRects.clear();

    int baseY = visibleTextArea.y - (firstVisibleLine - startLine) * computedLineHeight;

    for (int i = startLine; i < endLine; i++) {
        const std::string& line = lines[i];
        int textY = baseY + (i - startLine) * computedLineHeight;
        int x = visibleTextArea.x - horizontalScrollOffset;

        // --- CURRENT LINE: Show raw markdown with syntax highlighting ---
        if (i == cursorLine) {
            auto tokens = syntaxTokenizer->TokenizeLine(line);

            for (const auto& token : tokens) {
                context->SetFontWeight(GetStyleForTokenType(token.type).bold ?
                                       FontWeight::Bold : FontWeight::Normal);
                int tokenWidth = context->GetTextLineWidth(token.text);

                if (x + tokenWidth >= visibleTextArea.x &&
                    x <= visibleTextArea.x + visibleTextArea.width) {
                    TokenStyle tokenStyle = GetStyleForTokenType(token.type);
                    context->SetTextPaint(tokenStyle.color);
                    context->DrawText(token.text, x, textY);
                }

                x += tokenWidth;
            }
            continue;
        }

        // --- OTHER LINES: Show formatted markdown ---

        // Reset to default font settings
        context->SetFontWeight(FontWeight::Normal);
        context->SetFontSlant(FontSlant::Normal);
        context->SetFontSize(style.fontStyle.fontSize);
        context->SetFontFamily(style.fontStyle.fontFamily);

        // Empty lines
        if (line.empty()) continue;

        std::string trimmed = TrimWhitespace(line);
        if (trimmed.empty()) continue;

        // --- Code block content ---
        if (isInsideCodeBlock[i]) {
            if (isCodeBlockDelimiter[i]) {
                // This is a ``` or ~~~ delimiter line
                MarkdownInlineRenderer::RenderMarkdownCodeBlockDelimiter(
                        context, trimmed, x, textY, computedLineHeight,
                        visibleTextArea.width, style, mdStyle);
            } else {
                // This is a code content line — use syntax highlighting if language known
                const std::string& lang = codeBlockLanguage[i];

                if (!lang.empty()) {
                    // Switch tokenizer language if needed
                    if (lang != currentCodeBlockLang || !codeBlockTokenizer) {
                        codeBlockTokenizer = std::make_unique<SyntaxTokenizer>();

                        // Map common markdown language tags to SyntaxTokenizer names
                        // SyntaxTokenizer expects exact language names like "Python", "C++"
                        std::string normalizedLang = lang;
                        // Capitalize first letter for common mappings
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
                        else if (lang == "sh" || lang == "bash" || lang == "shell") normalizedLang = "Shell";
                        else if (lang == "html" || lang == "htm") normalizedLang = "HTML";
                        else if (lang == "css") normalizedLang = "CSS";
                        else if (lang == "sql") normalizedLang = "SQL";
                        else if (lang == "json") normalizedLang = "JavaScript";
                        else if (lang == "xml") normalizedLang = "HTML";
                        else if (lang == "asm") normalizedLang = "x86 Assembly";
                        else if (lang == "pas" || lang == "pascal" || lang == "delphi") normalizedLang = "Pascal";

                        if (!codeBlockTokenizer->SetLanguage(normalizedLang)) {
                            // Try by extension as fallback
                            codeBlockTokenizer->SetLanguageByExtension(lang);
                        }
                        currentCodeBlockLang = lang;
                    }

                    // Render syntax-highlighted code line
                    MarkdownInlineRenderer::RenderMarkdownCodeBlockHighlighted(
                            context, line, x, textY, computedLineHeight,
                            visibleTextArea.width, style, mdStyle,
                            codeBlockTokenizer.get(),
                            [this](TokenType type) -> TokenStyle { return GetStyleForTokenType(type); });
                } else {
                    // No language — render as plain monospace code
                    // For 4-space indented blocks, strip the leading 4 spaces
                    std::string codeLine = line;
                    if (codeBlockLanguage[i].empty() && !isCodeBlockDelimiter[i]) {
                        // Check if it's a 4-space indented line
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

        // --- Table rows ---
        if (tableRoles[i] != TableLineRole::NoneRole) {
            if (tableRoles[i] == TableLineRole::Separator) {
                MarkdownInlineRenderer::RenderMarkdownTableSeparator(
                        context, x, textY, computedLineHeight,
                        visibleTextArea.width, tableColumnCounts[i], mdStyle);
            } else {
                bool isHeader = (tableRoles[i] == TableLineRole::Header);
                MarkdownInlineRenderer::RenderMarkdownTableRow(
                        context, line, x, textY, computedLineHeight,
                        visibleTextArea.width, isHeader,
                        tableAlignments[i], tableColumnCounts[i],
                        style, mdStyle, &markdownHitRects);
            }
            continue;
        }

        // --- Headers ---
        if (trimmed[0] == '#') {
            MarkdownInlineRenderer::RenderMarkdownHeader(
                    context, trimmed, x, textY, computedLineHeight,
                    style, mdStyle, &markdownHitRects);
            continue;
        }

        // --- Horizontal rules ---
        if (IsMarkdownHorizontalRule(trimmed)) {
            MarkdownInlineRenderer::RenderMarkdownHorizontalRule(
                    context, x, textY, computedLineHeight,
                    visibleTextArea.width, mdStyle);
            continue;
        }

        // --- Blockquotes ---
        if (trimmed[0] == '>') {
            MarkdownInlineRenderer::RenderMarkdownBlockquote(
                    context, line, x, textY, computedLineHeight,
                    visibleTextArea.width, style, mdStyle, &markdownHitRects);
            continue;
        }

        // --- List items ---
        if (IsMarkdownListItem(trimmed)) {
            MarkdownInlineRenderer::RenderMarkdownListItem(
                    context, line, x, textY, computedLineHeight,
                    style, mdStyle, &markdownHitRects);
            continue;
        }

        // --- Definition list definition line: starts with ": " ---
        // A definition list consists of:
        //   Term          (plain text line — rendered normally)
        //   : Definition  (line starting with ": ")
        if (trimmed.length() >= 2 && trimmed[0] == ':' && trimmed[1] == ' ') {
            std::string defText = trimmed.substr(2);
            int defIndent = mdStyle.listIndent + 10; // Indent definition text

            // Draw small dash indicator
            context->SetTextPaint(mdStyle.bulletColor);
            context->DrawText("\xE2\x80\x94", x + 4, textY); // em-dash —

            // Draw definition text with inline formatting
            MarkdownInlineRenderer::RenderMarkdownLine(
                    context, defText, x + defIndent, textY, computedLineHeight,
                    style, mdStyle, &markdownHitRects);
            continue;
        }

        // --- Regular text with inline formatting ---
        MarkdownInlineRenderer::RenderMarkdownLine(
                context, line, x, textY, computedLineHeight,
                style, mdStyle, &markdownHitRects);
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
            if (pos + 1 < line.length() && line[pos + 1] == ' ') {
                return true;
            }
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
    if (!markdownHybridMode) return false;

    for (const auto& hitRect : markdownHitRects) {
        if (mouseX >= hitRect.bounds.x && mouseX <= hitRect.bounds.x + hitRect.bounds.width &&
            mouseY >= hitRect.bounds.y && mouseY <= hitRect.bounds.y + hitRect.bounds.height) {

            if (hitRect.isImage) {
                // Image click — trigger callback with image path
                if (onMarkdownImageClick) {
                    onMarkdownImageClick(hitRect.url, hitRect.altText);
                    return true;
                }
            } else {
                // Link click — trigger callback with URL
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
    if (!markdownHybridMode) return false;

    for (const auto& hitRect : markdownHitRects) {
        if (mouseX >= hitRect.bounds.x && mouseX <= hitRect.bounds.x + hitRect.bounds.width &&
            mouseY >= hitRect.bounds.y && mouseY <= hitRect.bounds.y + hitRect.bounds.height) {
            SetMouseCursor(UCMouseCursor::Hand);
            return true;
        }
    }
    return false;
}

} // namespace UltraCanvas