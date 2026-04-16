// core/UltraCanvasTextArea.cpp
// Advanced text area component with syntax highlighting and full UTF-8 support
// Version: 3.2.3
// Last Modified: 2026-04-16
// Author: UltraCanvas Framework

#include "UltraCanvasTextArea.h"
#include "UltraCanvasSyntaxTokenizer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasUtilsUtf8.h"
#include <algorithm>
#include <sstream>
#include <cmath>
#include <cstring>

namespace UltraCanvas {

namespace {
    // Maps :shortcode: names to their UTF-8 emoji. Mirrors the table in
    // UltraCanvasTextArea_Markdown.cpp — kept duplicated to keep the inline scanner
    // self-contained rather than introducing a shared header for a single lookup.
    const std::string& LookupEmojiShortcode(const std::string& code) {
        static const std::unordered_map<std::string, std::string> emojiMap = {
            // Smileys & People
            {"smile",           "\xF0\x9F\x98\x84"},   // 😄
            {"laughing",        "\xF0\x9F\x98\x86"},   // 😆
            {"blush",           "\xF0\x9F\x98\x8A"},   // 😊
            {"smiley",          "\xF0\x9F\x98\x83"},   // 😃
            {"relaxed",         "\xE2\x98\xBA"},       // ☺
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
            {"point_up",        "\xE2\x98\x9D"},       // ☝
            {"point_down",      "\xF0\x9F\x91\x87"},   // 👇
            {"point_left",      "\xF0\x9F\x91\x88"},   // 👈
            {"point_right",     "\xF0\x9F\x91\x89"},   // 👉
            {"ok_hand",         "\xF0\x9F\x91\x8C"},   // 👌
            {"v",               "\xE2\x9C\x8C"},       // ✌
            {"eyes",            "\xF0\x9F\x91\x80"},   // 👀
            // Hearts & Symbols
            {"heart",           "\xE2\x9D\xA4"},       // ❤
            {"broken_heart",    "\xF0\x9F\x92\x94"},   // 💔
            {"star",            "\xE2\xAD\x90"},       // ⭐
            {"sparkles",        "\xE2\x9C\xA8"},       // ✨
            {"fire",            "\xF0\x9F\x94\xA5"},   // 🔥
            {"100",             "\xF0\x9F\x92\xAF"},   // 💯
            {"boom",            "\xF0\x9F\x92\xA5"},   // 💥
            {"zap",             "\xE2\x9A\xA1"},       // ⚡
            {"warning",         "\xE2\x9A\xA0"},       // ⚠
            {"x",               "\xE2\x9D\x8C"},       // ❌
            {"white_check_mark","\xE2\x9C\x85"},       // ✅
            {"heavy_check_mark","\xE2\x9C\x94"},       // ✔
            {"ballot_box_with_check", "\xE2\x98\x91"}, // ☑
            {"question",        "\xE2\x9D\x93"},       // ❓
            {"exclamation",     "\xE2\x9D\x97"},       // ❗
            {"bulb",            "\xF0\x9F\x92\xA1"},   // 💡
            {"mega",            "\xF0\x9F\x93\xA3"},   // 📣
            {"bell",            "\xF0\x9F\x94\x94"},   // 🔔
            {"bookmark",        "\xF0\x9F\x94\x96"},   // 🔖
            {"link",            "\xF0\x9F\x94\x97"},   // 🔗
            {"key",             "\xF0\x9F\x94\x91"},   // 🔑
            {"lock",            "\xF0\x9F\x94\x92"},   // 🔒
            // Nature & Weather
            {"sunny",           "\xE2\x98\x80"},       // ☀
            {"cloud",           "\xE2\x98\x81"},       // ☁
            {"umbrella",        "\xE2\x98\x82"},       // ☂
            {"snowflake",       "\xE2\x9D\x84"},       // ❄
            {"rainbow",         "\xF0\x9F\x8C\x88"},   // 🌈
            {"earth_americas",  "\xF0\x9F\x8C\x8E"},   // 🌎
            {"seedling",        "\xF0\x9F\x8C\xB1"},   // 🌱
            {"evergreen_tree",  "\xF0\x9F\x8C\xB2"},   // 🌲
            {"fallen_leaf",     "\xF0\x9F\x8D\x82"},   // 🍂
            {"rose",            "\xF0\x9F\x8C\xB9"},   // 🌹
            {"sunflower",       "\xF0\x9F\x8C\xBB"},   // 🌻
            // Food & Drink
            {"coffee",          "\xE2\x98\x95"},       // ☕
            {"beer",            "\xF0\x9F\x8D\xBA"},   // 🍺
            {"wine_glass",      "\xF0\x9F\x8D\xB7"},   // 🍷
            {"pizza",           "\xF0\x9F\x8D\x95"},   // 🍕
            {"hamburger",       "\xF0\x9F\x8D\x94"},   // 🍔
            {"cake",            "\xF0\x9F\x8E\x82"},   // 🎂
            {"apple",           "\xF0\x9F\x8D\x8E"},   // 🍎
            // Activities & Objects
            {"tent",            "\xE2\x9B\xBA"},       // ⛺
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
            {"soccer",          "\xE2\x9A\xBD"},       // ⚽
            {"basketball",      "\xF0\x9F\x8F\x80"},   // 🏀
            // Objects & Tech
            {"computer",        "\xF0\x9F\x92\xBB"},   // 💻
            {"phone",           "\xF0\x9F\x93\xB1"},   // 📱
            {"email",           "\xF0\x9F\x93\xA7"},   // 📧
            {"memo",            "\xF0\x9F\x93\x9D"},   // 📝
            {"book",            "\xF0\x9F\x93\x96"},   // 📖
            {"pencil",          "\xE2\x9C\x8F"},       // ✏
            {"pencil2",         "\xE2\x9C\x8F"},       // ✏
            {"wrench",          "\xF0\x9F\x94\xA7"},   // 🔧
            {"hammer",          "\xF0\x9F\x94\xA8"},   // 🔨
            {"gear",            "\xE2\x9A\x99"},       // ⚙
            {"package",         "\xF0\x9F\x93\xA6"},   // 📦
            {"chart_with_upwards_trend", "\xF0\x9F\x93\x88"}, // 📈
            {"mag",             "\xF0\x9F\x94\x8D"},   // 🔍
            {"clipboard",       "\xF0\x9F\x93\x8B"},   // 📋
            {"pushpin",         "\xF0\x9F\x93\x8C"},   // 📌
            // Arrows & Misc
            {"arrow_right",     "\xE2\x9E\xA1"},       // ➡
            {"arrow_left",      "\xE2\xAC\x85"},       // ⬅
            {"arrow_up",        "\xE2\xAC\x86"},       // ⬆
            {"arrow_down",      "\xE2\xAC\x87"},       // ⬇
            {"recycle",         "\xE2\x99\xBB"},       // ♻
            {"copyright",       "\xC2\xA9"},           // ©
            {"registered",      "\xC2\xAE"},           // ®
            {"tm",              "\xE2\x84\xA2"},       // ™
            {"info",            "\xE2\x84\xB9"},       // ℹ
        };
        static const std::string kEmpty;
        auto it = emojiMap.find(code);
        return it != emojiMap.end() ? it->second : kEmpty;
    }

    // Typography substitutions — parenthesised markers that expand unconditionally, matching
    // Pandoc / markdown-it's "smart" behaviour. Longest literal first so `(tm)` wins over
    // `(t…`. Escape with `\(c\)` to keep the literal.
    struct Emoticon { const char* text; const char* emoji; };
    const std::vector<Emoticon>& TypographyTable() {
        static const std::vector<Emoticon> table = {
            {"(tm)", "\xE2\x84\xA2"},   // ™
            {"(TM)", "\xE2\x84\xA2"},   // ™
            {"(c)",  "\xC2\xA9"},       // ©
            {"(C)",  "\xC2\xA9"},       // ©
            {"(r)",  "\xC2\xAE"},       // ®
            {"(R)",  "\xC2\xAE"},       // ®
            {"(p)",  "\xE2\x84\x97"},   // ℗
            {"(P)",  "\xE2\x84\x97"},   // ℗
        };
        return table;
    }

    // ASCII emoticon table — longest literal first so prefix matching picks the full form
    // (`:-)` before `:)`). Only matched on a word boundary in ParseInlineMarkdownRuns so
    // `http://` or `(8)` aren't disturbed.
    const std::vector<Emoticon>& EmoticonTable() {
        static const std::vector<Emoticon> table = {
            {":'(",  "\xF0\x9F\x98\xA2"},   // 😢
            {":-)",  "\xF0\x9F\x99\x82"},   // 🙂
            {":-(",  "\xF0\x9F\x99\x81"},   // 🙁
            {":-D",  "\xF0\x9F\x98\x83"},   // 😃
            {":-P",  "\xF0\x9F\x98\x9B"},   // 😛
            {":-p",  "\xF0\x9F\x98\x9B"},   // 😛
            {":-O",  "\xF0\x9F\x98\xB2"},   // 😲
            {":-o",  "\xF0\x9F\x98\xB2"},   // 😲
            {":-|",  "\xF0\x9F\x98\x90"},   // 😐
            {":-/",  "\xF0\x9F\x98\x95"},   // 😕
            {";-)",  "\xF0\x9F\x98\x89"},   // 😉
            {"8-)",  "\xF0\x9F\x98\x8E"},   // 😎
            {":)",   "\xF0\x9F\x99\x82"},   // 🙂
            {":(",   "\xF0\x9F\x99\x81"},   // 🙁
            {":D",   "\xF0\x9F\x98\x83"},   // 😃
            {":P",   "\xF0\x9F\x98\x9B"},   // 😛
            {":p",   "\xF0\x9F\x98\x9B"},   // 😛
            {":O",   "\xF0\x9F\x98\xB2"},   // 😲
            {":o",   "\xF0\x9F\x98\xB2"},   // 😲
            {":|",   "\xF0\x9F\x98\x90"},   // 😐
            {":/",   "\xF0\x9F\x98\x95"},   // 😕
            {";)",   "\xF0\x9F\x98\x89"},   // 😉
            {"8)",   "\xF0\x9F\x98\x8E"},   // 😎
            {"<3",   "\xE2\x9D\xA4"},       // ❤
        };
        return table;
    }

    // Convert LaTeX-style commands inside `$…$` math spans to UTF-8 characters.
    // Copied from UltraCanvasTextArea_Markdown.cpp's SubstituteGreekLetters — kept
    // duplicated rather than shared via a new header, matching the pattern used for the
    // emoji / emoticon / typography tables above.
    std::string SubstituteGreekLetters(const std::string& input) {
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
                {"\\le",       "\xE2\x89\xA4"},   // ≤
                {"\\ge",       "\xE2\x89\xA5"},   // ≥
                {"\\ne",       "\xE2\x89\xA0"},   // ≠
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
                // Named functions (upright text)
                {"\\sin",  "sin"}, {"\\cos",  "cos"}, {"\\tan",  "tan"},
                {"\\log",  "log"}, {"\\ln",   "ln"},  {"\\lim",  "lim"},
                {"\\min",  "min"}, {"\\max",  "max"}, {"\\exp",  "exp"},
                {"\\det",  "det"}, {"\\dim",  "dim"}, {"\\ker",  "ker"},
                {"\\deg",  "deg"},
                // Numeric super/subscripts with braces
                {"^{0}",  "\xE2\x81\xB0"}, {"^{1}",  "\xC2\xB9"},
                {"^{2}",  "\xC2\xB2"},     {"^{3}",  "\xC2\xB3"},
                {"^{4}",  "\xE2\x81\xB4"}, {"^{5}",  "\xE2\x81\xB5"},
                {"^{6}",  "\xE2\x81\xB6"}, {"^{7}",  "\xE2\x81\xB7"},
                {"^{8}",  "\xE2\x81\xB8"}, {"^{9}",  "\xE2\x81\xB9"},
                {"^{n}",  "\xE2\x81\xBF"}, {"^{i}",  "\xE2\x81\xB1"},
                {"_{0}",  "\xE2\x82\x80"}, {"_{1}",  "\xE2\x82\x81"},
                {"_{2}",  "\xE2\x82\x82"}, {"_{3}",  "\xE2\x82\x83"},
                {"_{4}",  "\xE2\x82\x84"}, {"_{5}",  "\xE2\x82\x85"},
                {"_{6}",  "\xE2\x82\x86"}, {"_{7}",  "\xE2\x82\x87"},
                {"_{8}",  "\xE2\x82\x88"}, {"_{9}",  "\xE2\x82\x89"},
                // Common Unicode fractions
                {"\\frac{1}{2}", "\xC2\xBD"},
                {"\\frac{1}{3}", "\xE2\x85\x93"},
                {"\\frac{2}{3}", "\xE2\x85\x94"},
                {"\\frac{1}{4}", "\xC2\xBC"},
                {"\\frac{3}{4}", "\xC2\xBE"},
            };
            std::stable_sort(map.begin(), map.end(), [](const auto& a, const auto& b) {
                return a.first.length() > b.first.length();
            });
            return map;
        }();

        std::string result = input;
        for (const auto& [pattern, replacement] : greekMap) {
            const bool isBackslashCmd = (!pattern.empty() && pattern[0] == '\\');
            size_t searchPos = 0;
            while ((searchPos = result.find(pattern, searchPos)) != std::string::npos) {
                size_t afterMatch = searchPos + pattern.length();
                // Word boundary on backslash commands: don't let `\in` bite inside `\int…`.
                if (isBackslashCmd && afterMatch < result.length() &&
                    std::isalpha(static_cast<unsigned char>(result[afterMatch]))) {
                    searchPos++;
                    continue;
                }
                result.replace(searchPos, pattern.length(), replacement);
                searchPos += replacement.length();
            }
        }

        // Generic \frac{X}{Y} → X/Y (not in the known-fraction table)
        {
            size_t searchPos = 0;
            while ((searchPos = result.find("\\frac{", searchPos)) != std::string::npos) {
                size_t numEnd = result.find('}', searchPos + 6);
                if (numEnd != std::string::npos && numEnd + 1 < result.length() &&
                    result[numEnd + 1] == '{') {
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

        // \text{...} / \mathbf{...} / \mathrm{...} → inner text.
        for (const char* cmd : { "\\text{", "\\mathbf{", "\\mathrm{" }) {
            const size_t cmdLen = std::strlen(cmd);
            size_t searchPos = 0;
            while ((searchPos = result.find(cmd, searchPos)) != std::string::npos) {
                size_t braceEnd = result.find('}', searchPos + cmdLen);
                if (braceEnd == std::string::npos) break;
                std::string content = result.substr(searchPos + cmdLen,
                                                    braceEnd - searchPos - cmdLen);
                result.replace(searchPos, braceEnd - searchPos + 1, content);
                searchPos += content.length();
            }
        }

        // \sqrt was already replaced with √ above — wrap its {…} argument as √(…)
        {
            size_t searchPos = 0;
            while ((searchPos = result.find("\xE2\x88\x9A{", searchPos)) != std::string::npos) {
                size_t braceEnd = result.find('}', searchPos + 4);
                if (braceEnd == std::string::npos) break;
                std::string content = result.substr(searchPos + 4, braceEnd - searchPos - 4);
                std::string replacement = "\xE2\x88\x9A(" + content + ")";
                result.replace(searchPos, braceEnd - searchPos + 1, replacement);
                searchPos += replacement.length();
            }
        }

        // Drop bare \left / \right delimiters.
        for (const char* cmd : { "\\left", "\\right" }) {
            const size_t cmdLen = std::strlen(cmd);
            size_t searchPos = 0;
            while ((searchPos = result.find(cmd, searchPos)) != std::string::npos) {
                result.erase(searchPos, cmdLen);
            }
        }

        return result;
    }
} // namespace

// Constructor
    UltraCanvasTextArea::UltraCanvasTextArea(const std::string& name, int id, int x, int y,
                                             int width, int height)
            : UltraCanvasUIElement(name, id, x, y, width, height),
              horizontalScrollOffset(0),
              verticalScrollOffset(0),
              cursorBlinkTime(0),
              cursorVisible(true),
              isReadOnly(false),
              wordWrap(false),
              highlightCurrentLine(false),
              isNeedRecalculateVisibleArea(true)  {

        // Initialize with empty line
        lines.push_back(std::string());

        // Initialize style with defaults
        ApplyDefaultStyle();

        // Initialize syntax highlighter if needed
        if (style.highlightSyntax) {
            syntaxTokenizer = std::make_unique<SyntaxTokenizer>();
        }
    }

// Destructor
    UltraCanvasTextArea::~UltraCanvasTextArea() = default;

// Initialize default style
    void UltraCanvasTextArea::ApplyDefaultStyle() {
        auto* app = UltraCanvasApplication::GetInstance();
        if (app) {
            style.fontStyle.fontFamily = app->GetSystemFontStyle().fontFamily;
            style.fixedFontStyle.fontFamily = app->GetDefaultMonospacedFontStyle().fontFamily;
        }
        style.fontStyle.fontSize = 11;
        style.fixedFontStyle.fontSize = 11;
        style.fontColor = {0, 0, 0, 255};
        style.lineHeight = 1.1;
        style.backgroundColor = {255, 255, 255, 255};
        style.borderColor = {200, 200, 200, 255};
        style.selectionColor = {51, 153, 255, 100};
        style.cursorColor = {0, 0, 0, 255};
        style.currentLineColor = {240, 240, 240, 255};
        style.lineNumbersColor = {128, 128, 128, 255};
        style.lineNumbersBackgroundColor = {248, 248, 248, 255};
        style.currentLineHighlightColor = {255, 255, 0, 30};
        style.scrollbarTrackColor = {128, 128, 128, 255};
        style.scrollbarColor = {200, 200, 200, 255};
        style.borderWidth = 1;
        style.padding = 5;
        style.showLineNumbers = false;
        style.highlightSyntax = false;

        // Syntax highlighting colors
        style.tokenStyles.keywordStyle.color = {0, 0, 255, 255};
        style.tokenStyles.functionStyle.color = {128, 0, 128, 255};
        style.tokenStyles.stringStyle.color = {0, 128, 0, 255};
        style.tokenStyles.characterStyle.color = {0, 128, 0, 255};
        style.tokenStyles.commentStyle.color = {128, 128, 128, 255};
        style.tokenStyles.numberStyle.color = {255, 128, 0, 255};
        style.tokenStyles.identifierStyle.color = {0, 128, 128, 255};
        style.tokenStyles.operatorStyle.color = {128, 0, 0, 255};
        style.tokenStyles.constantStyle.color = {0, 0, 128, 255};
        style.tokenStyles.preprocessorStyle.color = {64, 128, 128, 255};
        style.tokenStyles.builtinStyle.color = {128, 0, 255, 255};
    }

// ===== LINE ENDING HELPERS =====

    LineEndingType UltraCanvasTextArea::GetSystemDefaultLineEnding() {
#ifdef _WIN32
        return LineEndingType::CRLF;
#else
        return LineEndingType::LF;
#endif
    }

    LineEndingType UltraCanvasTextArea::DetectLineEnding(const std::string& text) {
        int lfCount = 0;
        int crlfCount = 0;
        int crCount = 0;

        for (size_t i = 0; i < text.size(); i++) {
            if (text[i] == '\r') {
                if (i + 1 < text.size() && text[i + 1] == '\n') {
                    crlfCount++;
                    i++; // skip the \n
                } else {
                    crCount++;
                }
            } else if (text[i] == '\n') {
                lfCount++;
            }
        }

        // No line endings found — use system default
        if (lfCount == 0 && crlfCount == 0 && crCount == 0) {
            return GetSystemDefaultLineEnding();
        }

        // Return the dominant type
        if (crlfCount >= lfCount && crlfCount >= crCount) return LineEndingType::CRLF;
        if (crCount >= lfCount && crCount >= crlfCount) return LineEndingType::CR;
        return LineEndingType::LF;
    }

    std::string UltraCanvasTextArea::LineEndingToString(LineEndingType type) {
        switch (type) {
            case LineEndingType::LF:   return "LF";
            case LineEndingType::CRLF: return "CRLF";
            case LineEndingType::CR:   return "CR";
        }
        return "LF";
    }

    std::string UltraCanvasTextArea::LineEndingSequence(LineEndingType type) {
        switch (type) {
            case LineEndingType::LF:   return "\n";
            case LineEndingType::CRLF: return "\r\n";
            case LineEndingType::CR:   return "\r";
        }
        return "\n";
    }

    void UltraCanvasTextArea::SetLineEnding(LineEndingType type) {
        if (lineEndingType != type) {
            lineEndingType = type;
            RebuildText();
            if (onLineEndingChanged) {
                onLineEndingChanged(lineEndingType);
            }
        }
    }

// ===== UTF-8 HELPER METHODS =====

    // Convert grapheme column to byte offset within a line
//    size_t UltraCanvasTextArea::GraphemeToByteOffset(int lineIndex, int graphemeColumn) const {
//        if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) {
//            return 0;
//        }
//        return utf8_cp_to_byte(lines[lineIndex], graphemeColumn);
//    }

    // Get grapheme count for a line
    int UltraCanvasTextArea::GetLineGraphemeCount(int lineIndex) const {
        if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) {
            return 0;
        }
        return static_cast<int>(utf8_length(lines[lineIndex]));
    }

    // Get total grapheme count (cached)
    int UltraCanvasTextArea::GetTotalGraphemeCount() const {
        if (cachedTotalGraphemes >= 0) {
            return cachedTotalGraphemes;
        }
        
        int total = 0;
        for (size_t i = 0; i < lines.size(); i++) {
            total += static_cast<int>(utf8_length(lines[i]));
            if (i < lines.size() - 1) {
                total++; // Count newline as one grapheme
            }
        }
        cachedTotalGraphemes = total;
        return total;
    }

// ===== POSITION CONVERSION =====

    // Convert grapheme position to line/column (both in graphemes)
    std::pair<int, int> UltraCanvasTextArea::GetLineColumnFromPosition(int graphemePosition) const {
        int line = 0;
        int col = 0;
        int currentPos = 0;

        for (size_t i = 0; i < lines.size(); i++) {
            int lineLength = static_cast<int>(utf8_length(lines[i]));
            if (currentPos + lineLength >= graphemePosition) {
                line = static_cast<int>(i);
                col = graphemePosition - currentPos;
                break;
            }
            currentPos += lineLength + 1; // +1 for newline (counts as 1 grapheme)
            line = static_cast<int>(i);
        }

        return {line, col};
    }

    // Convert line/column (in graphemes) to grapheme position
    int UltraCanvasTextArea::GetPositionFromLineColumn(int line, int graphemeColumn) const {
        int position = 0;

        for (int i = 0; i < line && i < static_cast<int>(lines.size()); i++) {
            position += static_cast<int>(utf8_length(lines[i])) + 1; // +1 for newline
        }

        if (line < static_cast<int>(lines.size())) {
            position += std::min(graphemeColumn, static_cast<int>(utf8_length(lines[line])));
        }

        return position;
    }

// ===== TEXT MANIPULATION METHODS =====

    void UltraCanvasTextArea::SetText(const std::string& newText, bool runNotifications) {
        // Detect and set line ending type from content
        LineEndingType detectedEOL = DetectLineEnding(newText);
        bool eolChanged = (detectedEOL != lineEndingType);
        lineEndingType = detectedEOL;

        // Split into lines, handling all EOL types
        lines = utf8_split_lines(newText);
        if (lines.empty()) {
            lines.push_back(std::string());
        }

        // Rebuild textContent with the detected line ending
        textContent.clear();
        std::string eolSeq = LineEndingSequence(lineEndingType);
        for (size_t i = 0; i < lines.size(); i++) {
            textContent.append(lines[i]);
            if (i < lines.size() - 1) {
                textContent.append(eolSeq);
            }
        }

        InvalidateGraphemeCache();
        // Full text replacement: invalidate the layout cache + MD index.
        lineLayouts.clear();
        lineLayouts.resize(lines.size());
        markdownIndexDirty = true;
        cursorPosition = {0, 0};
        currentLine.reset();
        selectionStart = LineColumnIndex::INVALID;
        selectionEnd   = LineColumnIndex::INVALID;
        isNeedRecalculateVisibleArea = true;
        RequestRedraw();
        if (runNotifications && onTextChanged) {
            onTextChanged(textContent);
        }
        if (eolChanged && onLineEndingChanged) {
            onLineEndingChanged(lineEndingType);
        }
    }

    std::string UltraCanvasTextArea::GetText() const {
        return textContent;
    }

    void UltraCanvasTextArea::InsertText(const std::string& textToInsert) {
        if (isReadOnly) return;

        SaveState();
        if (HasSelection()) {
            DeleteSelection();
        }

        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;
        int startLine = line;

        if (lines.empty()) {
            lines.push_back(std::string());
            InsertLineLayoutEntry(0);
            line = 0;
            col  = 0;

        }

        const char* p   = textToInsert.c_str();
        const char* end = p + textToInsert.size();
        while (p < end) {
            if (*p == '\r' || *p == '\n') {
                bool isCRLF = (*p == '\r' && p + 1 < end && *(p + 1) == '\n');
                std::string currentLine = lines[line];
                lines[line] = utf8_substr(currentLine, 0, col);
                lines.insert(lines.begin() + line + 1, utf8_substr(currentLine, col));
                InsertLineLayoutEntry(line + 1);
                line++;
                col = 0;
                p += isCRLF ? 2 : 1;
            } else {
                const char* next = g_utf8_next_char(p);
                std::string ch(p, static_cast<size_t>(next - p));
                utf8_insert(lines[line], col, ch);
                col++;
                p = next;
            }
        }

        for (int i = startLine; i <= line && i < static_cast<int>(lines.size()); i++) {
            InvalidateLineLayout(i);
        }

        cursorPosition = {line, col};

        RebuildText();
    }

    void UltraCanvasTextArea::InsertCodepoint(char32_t codepoint) {
        if (isReadOnly) return;

        InsertText(utf8_encode(codepoint));
    }

    void UltraCanvasTextArea::InsertNewLine() {
        if (isReadOnly) return;

        SaveState();
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;

        if (lines.empty()) {
            lines.push_back(std::string());
            InsertLineLayoutEntry(0);
            line = 0;
            col  = 0;
        }

        std::string currentLine = lines[line];
        lines[line] = utf8_substr(currentLine, 0, col);
        lines.insert(lines.begin() + line + 1, utf8_substr(currentLine, col));
        InvalidateLineLayout(line);
        InsertLineLayoutEntry(line + 1);

        cursorPosition = {line + 1, 0};
        InvalidateGraphemeCache();

        RebuildText();
    }

    void UltraCanvasTextArea::InsertTab() {
        if (isReadOnly) return;

        // Insert spaces for tab
        std::string tabStr(tabSize, ' ');
        InsertText(tabStr);
    }

    // Delete one codepoint backward (backspace)
    void UltraCanvasTextArea::DeleteCharacterBackward() {
        if (isReadOnly) return;
        if (cursorPosition.lineIndex == 0 && cursorPosition.columnIndex == 0) return;

        SaveState();
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;
        bool needsRebuild = false;

        if (col > 0) {
            utf8_erase(lines[line], col - 1, 1);
            InvalidateLineLayout(line);
            cursorPosition = {line, col - 1};
        } else if (line > 0) {
            int prevLineLength = static_cast<int>(utf8_length(lines[line - 1]));
            lines[line - 1].append(lines[line]);
            lines.erase(lines.begin() + line);
            InvalidateLineLayout(line - 1);
            RemoveLineLayoutEntry(line);
            cursorPosition = {line - 1, prevLineLength};
        }

        InvalidateGraphemeCache();
        RebuildText();
    }

    // Delete one codepoint forward (delete key)
    void UltraCanvasTextArea::DeleteCharacterForward() {
        if (isReadOnly) return;

        SaveState();
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;

        if (line < static_cast<int>(lines.size())) {
            int lineLen = static_cast<int>(utf8_length(lines[line]));
            if (col < lineLen) {
                utf8_erase(lines[line], col, 1);
                InvalidateLineLayout(line);
            } else if (line < static_cast<int>(lines.size()) - 1) {
                lines[line].append(lines[line + 1]);
                lines.erase(lines.begin() + line + 1);
                InvalidateLineLayout(line);
                RemoveLineLayoutEntry(line + 1);
            }

            InvalidateGraphemeCache();
            RebuildText();
        }
    }

    void UltraCanvasTextArea::DeleteSelection() {
        if (!HasSelection() || isReadOnly) return;

        SaveState();
        // Lexicographic ordering on (lineIndex, columnIndex).
        LineColumnIndex a = selectionStart, b = selectionEnd;
        if (a.lineIndex > b.lineIndex ||
            (a.lineIndex == b.lineIndex && a.columnIndex > b.columnIndex)) {
            std::swap(a, b);
        }
        int startLine = a.lineIndex, startCol = a.columnIndex;
        int endLine   = b.lineIndex, endCol   = b.columnIndex;

        if (startLine == endLine) {
            utf8_erase(lines[startLine], startCol, endCol - startCol);
            InvalidateLineLayout(startLine);
        } else {
            std::string newLine = utf8_substr(lines[startLine], 0, startCol);
            newLine.append(utf8_substr(lines[endLine], endCol));
            lines[startLine] = newLine;
            lines.erase(lines.begin() + startLine + 1, lines.begin() + endLine + 1);
            InvalidateLineLayout(startLine);
            for (int i = endLine; i > startLine; i--) {
                RemoveLineLayoutEntry(i);
            }
        }

        cursorPosition = {startLine, startCol};
        selectionStart = LineColumnIndex::INVALID;
        selectionEnd   = LineColumnIndex::INVALID;
        InvalidateGraphemeCache();

        RebuildText();
    }
// ===== CURSOR MOVEMENT METHODS (Grapheme-aware) =====

    void UltraCanvasTextArea::MoveCursorLeft(bool selecting) {
        if (cursorPosition.lineIndex == 0 && cursorPosition.columnIndex == 0) return;

        LineColumnIndex oldPos = cursorPosition;
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;

        // Try layout-based cursor movement (correct bidi/complex-script handling).
        //auto ctx = GetRenderContext();
        bool moved = false;
        if (line >= 0 && line < static_cast<int>(lines.size()) && currentLine) {
            ITextLayout* layout = currentLine->layout.get();
            if (layout && col > 0) {
                int curByte = static_cast<int>(utf8_cp_to_byte(lines[line], col));
                auto result = layout->MoveCursorVisually(true, curByte, 0, -1);
                if (result.newIndex >= 0 && result.newIndex <= static_cast<int>(lines[line].size())) {
                    int newCol = utf8_byte_to_cp(lines[line], result.newIndex) + result.newTrailing;
                    cursorPosition = {line, newCol};
                    moved = true;
                }
            }
        }

        // Fallback: codepoint-based movement, wrapping to previous line if at col 0.
        if (!moved) {
            if (col > 0) {
                cursorPosition = {line, col - 1};
            } else if (line > 0) {
                cursorPosition = {line - 1, utf8_length(lines[line - 1])};
            }
        }

        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(cursorPosition.lineIndex, cursorPosition.columnIndex);
        }
    }

    void UltraCanvasTextArea::MoveCursorRight(bool selecting) {
        int totalLines = (int)lines.size();
        if (cursorPosition.lineIndex >= totalLines - 1 &&
            (totalLines == 0 || cursorPosition.columnIndex >= utf8_length(lines[cursorPosition.lineIndex]))) {
            return;
        }

        LineColumnIndex oldPos = cursorPosition;
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;

        auto ctx = GetRenderContext();
        bool moved = false;
        if (ctx && line >= 0 && line < static_cast<int>(lines.size()) && currentLine) {
            ITextLayout* layout = currentLine->layout.get();
            int lineCps = static_cast<int>(utf8_length(lines[line]));
            if (layout && col < lineCps) {
                int curByte = static_cast<int>(utf8_cp_to_byte(lines[line], col));
                auto result = layout->MoveCursorVisually(true, curByte, 0, 1);
                if (result.newIndex >= 0 && result.newIndex <= static_cast<int>(lines[line].size())) {
                    int newCol = utf8_byte_to_cp(lines[line], result.newIndex) + result.newTrailing;
                    cursorPosition = {line, newCol};
                    moved = true;
                }
            }
        }

        if (!moved) {
            int lineCps = utf8_length(lines[line]);
            if (col < lineCps) {
                cursorPosition = {line, col + 1};
            } else if (line + 1 < totalLines) {
                cursorPosition = {line + 1, 0};
            }
        }

        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(cursorPosition.lineIndex, cursorPosition.columnIndex);
        }
    }

    // Vertical cursor movement uses the layout-based cursor rect to walk across wrapped sub-lines
    // inside a single ITextLayout (handled by PosToLineColumn → Pango XYToIndex) as well as
    // across logical line boundaries. No displayLines table needed.
    void UltraCanvasTextArea::MoveCursorUp(bool selecting) {
        LineColumnIndex oldPos = cursorPosition;
        Rect2Di curRect = LineColumnToCursorPos(cursorPosition);
        LineColumnIndex newPos = cursorPosition;
        if (curRect.IsValid()) {
            int targetX = curRect.x;
            int targetY = curRect.y - 2;
            if (targetY < visibleTextArea.y) targetY = visibleTextArea.y;
            LineColumnIndex hit = PosToLineColumn({targetX, targetY});
            if (hit.lineIndex >= 0 &&
                (hit.lineIndex < cursorPosition.lineIndex ||
                 (hit.lineIndex == cursorPosition.lineIndex && hit.columnIndex < cursorPosition.columnIndex))) {
                newPos = hit;
            } else if (cursorPosition.lineIndex > 0) {
                newPos.lineIndex = cursorPosition.lineIndex - 1;
                newPos.columnIndex = std::min(cursorPosition.columnIndex,
                                              utf8_length(lines[newPos.lineIndex]));
            }
        } else if (cursorPosition.lineIndex > 0) {
            newPos.lineIndex = cursorPosition.lineIndex - 1;
            newPos.columnIndex = std::min(cursorPosition.columnIndex,
                                          utf8_length(lines[newPos.lineIndex]));
        } else {
            return;
        }
        cursorPosition = newPos;
        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(newPos.lineIndex, newPos.columnIndex);
        }
    }

    void UltraCanvasTextArea::MoveCursorDown(bool selecting) {
        LineColumnIndex oldPos = cursorPosition;
        Rect2Di curRect = LineColumnToCursorPos(cursorPosition);
        LineColumnIndex newPos = cursorPosition;
        if (curRect.IsValid()) {
            int targetX = curRect.x;
            int targetY = curRect.y + curRect.height + 2;
            LineColumnIndex hit = PosToLineColumn({targetX, targetY});
            if (hit.lineIndex >= 0 &&
                (hit.lineIndex > cursorPosition.lineIndex ||
                 (hit.lineIndex == cursorPosition.lineIndex && hit.columnIndex > cursorPosition.columnIndex))) {
                newPos = hit;
            } else if (cursorPosition.lineIndex < (int)lines.size() - 1) {
                newPos.lineIndex = cursorPosition.lineIndex + 1;
                newPos.columnIndex = std::min(cursorPosition.columnIndex,
                                              utf8_length(lines[newPos.lineIndex]));
            }
        } else if (cursorPosition.lineIndex < (int)lines.size() - 1) {
            newPos.lineIndex = cursorPosition.lineIndex + 1;
            newPos.columnIndex = std::min(cursorPosition.columnIndex,
                                          utf8_length(lines[newPos.lineIndex]));
        } else {
            return;
        }
        cursorPosition = newPos;
        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(newPos.lineIndex, newPos.columnIndex);
        }
    }

    void UltraCanvasTextArea::MoveCursorWordLeft(bool selecting) {
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;
        if (line >= static_cast<int>(lines.size())) return;

        LineColumnIndex oldPos = cursorPosition;

        if (col == 0) {
            if (line > 0) {
                line--;
                col = utf8_length(lines[line]);
            } else {
                if (selecting) {
                    if (selectionStart.lineIndex < 0) selectionStart = cursorPosition;
                    selectionEnd = cursorPosition;
                } else {
                    selectionStart = LineColumnIndex::INVALID;
                    selectionEnd   = LineColumnIndex::INVALID;
                }
                return;
            }
        } else {
            const std::string& currentLine = lines[line];
            while (col > 0) {
                gunichar cp = utf8_get_cp(currentLine, col - 1);
                if (g_unichar_isalnum(cp) || cp == '_') break;
                col--;
            }
            while (col > 0) {
                gunichar cp = utf8_get_cp(currentLine, col - 1);
                if (!g_unichar_isalnum(cp) && cp != '_') break;
                col--;
            }
        }

        cursorPosition = {line, col};

        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) onCursorPositionChanged(line, col);
    }

    void UltraCanvasTextArea::MoveCursorWordRight(bool selecting) {
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;
        if (line >= static_cast<int>(lines.size())) return;

        LineColumnIndex oldPos = cursorPosition;
        const std::string& currentLine = lines[line];
        int lineLen = utf8_length(currentLine);

        if (col >= lineLen) {
            if (line + 1 < static_cast<int>(lines.size())) {
                line++;
                col = 0;
            } else {
                if (selecting) {
                    if (selectionStart.lineIndex < 0) selectionStart = cursorPosition;
                    selectionEnd = cursorPosition;
                } else {
                    selectionStart = LineColumnIndex::INVALID;
                    selectionEnd   = LineColumnIndex::INVALID;
                }
                return;
            }
        } else {
            while (col < lineLen) {
                gunichar cp = utf8_get_cp(currentLine, col);
                if (g_unichar_isalnum(cp) || cp == '_') break;
                col++;
            }
            while (col < lineLen) {
                gunichar cp = utf8_get_cp(currentLine, col);
                if (!g_unichar_isalnum(cp) && cp != '_') break;
                col++;
            }
        }

        cursorPosition = {line, col};

        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) onCursorPositionChanged(line, col);
    }

    void UltraCanvasTextArea::MoveCursorPageUp(bool selecting) {
        LineColumnIndex oldPos = cursorPosition;
        Rect2Di curRect = LineColumnToCursorPos(cursorPosition);
        LineColumnIndex newPos = cursorPosition;
        if (curRect.IsValid()) {
            int targetY = curRect.y - visibleTextArea.height;
            LineColumnIndex hit = PosToLineColumn({curRect.x, targetY});
            if (hit.lineIndex >= 0) newPos = hit;
        } else if (cursorPosition.lineIndex > 0) {
            newPos.lineIndex  = std::max(0, cursorPosition.lineIndex - 10);
            newPos.columnIndex = std::min(cursorPosition.columnIndex,
                                          utf8_length(lines[newPos.lineIndex]));
        } else {
            return;
        }
        cursorPosition = newPos;
        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) onCursorPositionChanged(newPos.lineIndex, newPos.columnIndex);
    }

    void UltraCanvasTextArea::MoveCursorPageDown(bool selecting) {
        LineColumnIndex oldPos = cursorPosition;
        Rect2Di curRect = LineColumnToCursorPos(cursorPosition);
        LineColumnIndex newPos = cursorPosition;
        if (curRect.IsValid()) {
            int targetY = curRect.y + visibleTextArea.height;
            LineColumnIndex hit = PosToLineColumn({curRect.x, targetY});
            if (hit.lineIndex >= 0) newPos = hit;
        } else if (cursorPosition.lineIndex < (int)lines.size() - 1) {
            newPos.lineIndex  = std::min((int)lines.size() - 1, cursorPosition.lineIndex + 10);
            newPos.columnIndex = std::min(cursorPosition.columnIndex,
                                          utf8_length(lines[newPos.lineIndex]));
        } else {
            return;
        }
        cursorPosition = newPos;
        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) onCursorPositionChanged(newPos.lineIndex, newPos.columnIndex);
    }

    void UltraCanvasTextArea::MoveCursorToLineStart(bool selecting) {
        int line = cursorPosition.lineIndex;
        LineColumnIndex oldPos = cursorPosition;
        int targetCol = 0;
        if (wordWrap) {
            Rect2Di curRect = LineColumnToCursorPos(cursorPosition);
            if (curRect.IsValid()) {
                LineColumnIndex hit = PosToLineColumn({visibleTextArea.x, curRect.y});
                if (hit.lineIndex == line) targetCol = hit.columnIndex;
            }
        }
        cursorPosition = {line, targetCol};
        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) {
            onCursorPositionChanged(line, targetCol);
        }
    }

    void UltraCanvasTextArea::MoveCursorToLineEnd(bool selecting) {
        int line = cursorPosition.lineIndex;
        LineColumnIndex oldPos = cursorPosition;

        if (line < static_cast<int>(lines.size())) {
            int targetCol = utf8_length(lines[line]);
            if (wordWrap) {
                Rect2Di curRect = LineColumnToCursorPos(cursorPosition);
                if (curRect.IsValid()) {
                    int farRight = visibleTextArea.x + visibleTextArea.width + 100000;
                    LineColumnIndex hit = PosToLineColumn({farRight, curRect.y});
                    if (hit.lineIndex == line) targetCol = hit.columnIndex;
                }
            }
            cursorPosition = {line, targetCol};
            if (selecting) {
                if (selectionStart.lineIndex < 0) selectionStart = oldPos;
                selectionEnd = cursorPosition;
            } else {
                selectionStart = LineColumnIndex::INVALID;
                selectionEnd   = LineColumnIndex::INVALID;
            }
            RequestRedraw();
            if (onCursorPositionChanged) onCursorPositionChanged(line, targetCol);
        }
    }

    void UltraCanvasTextArea::MoveCursorToStart(bool selecting) {
        LineColumnIndex oldPos = cursorPosition;
        cursorPosition = {0, 0};
        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) onCursorPositionChanged(0, 0);
    }

    void UltraCanvasTextArea::MoveCursorToEnd(bool selecting) {
        LineColumnIndex oldPos = cursorPosition;
        int toLine = std::max(static_cast<int>(lines.size()) - 1, 0);
        int lineLength = utf8_length(lines[toLine]);
        cursorPosition = {toLine, lineLength};
        if (selecting) {
            if (selectionStart.lineIndex < 0) selectionStart = oldPos;
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
        }
        RequestRedraw();
        if (onCursorPositionChanged) onCursorPositionChanged(toLine, lineLength);
    }

// ===== SELECTION METHODS =====

    void UltraCanvasTextArea::SelectAll() {
        if (lines.empty()) {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd = LineColumnIndex::INVALID;
            return;
        }
        selectionStart = {0, 0};
        int last = (int)lines.size() - 1;
        selectionEnd  = {last, utf8_length(lines[last])};
        cursorPosition = selectionEnd;
        RequestRedraw();
    }

    void UltraCanvasTextArea::SelectLine(int lineIndex) {
        if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) {
            selectionStart = {lineIndex, 0};
            selectionEnd   = {lineIndex, utf8_length(lines[lineIndex])};
            cursorPosition = selectionEnd;
            RequestRedraw();
        }
    }

    void UltraCanvasTextArea::SelectWord() {
        int line = cursorPosition.lineIndex;
        int col  = cursorPosition.columnIndex;
        if (line >= static_cast<int>(lines.size())) return;

        const std::string& currentLine = lines[line];
        int lineLen = utf8_length(currentLine);
        if (lineLen == 0) return;

        int wordStart = col;
        while (wordStart > 0) {
            gunichar cp = utf8_get_cp(currentLine, wordStart - 1);
            if (!g_unichar_isalnum(cp) && cp != '_') break;
            wordStart--;
        }
        int wordEnd = col;
        while (wordEnd < lineLen) {
            gunichar cp = utf8_get_cp(currentLine, wordEnd);
            if (!g_unichar_isalnum(cp) && cp != '_') break;
            wordEnd++;
        }

        selectionStart = {line, wordStart};
        selectionEnd   = {line, wordEnd};
        cursorPosition = selectionEnd;
        RequestRedraw();
    }

    // Public API: arguments are flat codepoint positions.
    void UltraCanvasTextArea::SetSelection(int startGrapheme, int endGrapheme) {
        auto [sLine, sCol] = GetLineColumnFromPosition(startGrapheme);
        auto [eLine, eCol] = GetLineColumnFromPosition(endGrapheme);
        selectionStart = {sLine, sCol};
        selectionEnd   = {eLine, eCol};
        RequestRedraw();
    }

    void UltraCanvasTextArea::ClearSelection() {
        selectionStart = LineColumnIndex::INVALID;
        selectionEnd   = LineColumnIndex::INVALID;
        RequestRedraw();
    }

    bool UltraCanvasTextArea::HasSelection() const {
        if (selectionStart.lineIndex < 0 || selectionEnd.lineIndex < 0) return false;
        return !(selectionStart.lineIndex == selectionEnd.lineIndex &&
                 selectionStart.columnIndex == selectionEnd.columnIndex);
    }

    int UltraCanvasTextArea::GetSelectionMinGrapheme() const {
        if (!HasSelection()) return -1;
        LineColumnIndex a = selectionStart, b = selectionEnd;
        if (a.lineIndex > b.lineIndex ||
            (a.lineIndex == b.lineIndex && a.columnIndex > b.columnIndex)) std::swap(a, b);
        return GetPositionFromLineColumn(a.lineIndex, a.columnIndex);
    }

    std::string UltraCanvasTextArea::GetSelectedText() const {
        if (!HasSelection()) return std::string();
        LineColumnIndex a = selectionStart, b = selectionEnd;
        if (a.lineIndex > b.lineIndex ||
            (a.lineIndex == b.lineIndex && a.columnIndex > b.columnIndex)) std::swap(a, b);
        int startLine = a.lineIndex, startCol = a.columnIndex;
        int endLine   = b.lineIndex, endCol   = b.columnIndex;

        std::string eolSeq = LineEndingSequence(lineEndingType);
        std::string result;
        for (int i = startLine; i <= endLine && i < static_cast<int>(lines.size()); i++) {
            int colStart = (i == startLine) ? startCol : 0;
            int colEnd = (i == endLine) ? endCol : static_cast<int>(utf8_length(lines[i]));
            result.append(utf8_substr(lines[i], colStart, colEnd - colStart));
            if (i < endLine) {
                result.append(eolSeq);
            }
        }
        return result;
    }

// ===== CLIPBOARD OPERATIONS =====

    void UltraCanvasTextArea::CopySelection() {
        if (!HasSelection()) return;

        std::string selectedText = GetSelectedText();
        SetClipboardText(selectedText);
    }

    void UltraCanvasTextArea::CutSelection() {
        if (!HasSelection() || isReadOnly) return;

        CopySelection();
        DeleteSelection();
    }

    void UltraCanvasTextArea::PasteClipboard() {
        if (isReadOnly) return;

        std::string clipboardText;
        auto result = GetClipboardText(clipboardText);
        if (result && !clipboardText.empty()) {
            InsertText(clipboardText);
        }
    }

// ===== LINE OPERATIONS =====

    int UltraCanvasTextArea::GetCurrentLine() const {
        return cursorPosition.lineIndex;
    }

    int UltraCanvasTextArea::GetCurrentColumn() const {
        return cursorPosition.columnIndex;
    }

    int UltraCanvasTextArea::GetLineCount() const {
        return static_cast<int>(lines.size());
    }

    std::string UltraCanvasTextArea::GetLine(int lineIndex) const {
        if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) {
            return lines[lineIndex];
        }
        return std::string();
    }

    void UltraCanvasTextArea::SetLine(int lineIndex, const std::string& text) {
        if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) {
            lines[lineIndex] = text;
            InvalidateLineLayout(lineIndex);
            InvalidateGraphemeCache();
            RebuildText();
        }
    }

    void UltraCanvasTextArea::GoToLine(int lineNumber) {
        int lineIndex = std::max(0, std::min(lineNumber, static_cast<int>(lines.size()) - 1));
        cursorPosition = {lineIndex, 0};
        EnsureCursorVisible();
        RequestRedraw();
    }

// ===== RENDERING METHODS =====
    void UltraCanvasTextArea::Render(IRenderContext* ctx) {
        ctx->PushState();

        // UpdateGeometry runs CalculateVisibleArea + UpdateLineLayouts. It's idempotent
        // if layouts are already built; calling here guards against frames where the framework
        // skipped the geometry pass.
        UpdateGeometry(ctx);

        DrawBackground(ctx);

        if (editingMode == TextAreaEditingMode::Hex) {
            DrawHexView(ctx);
        } else {
            if (style.showLineNumbers) {
                DrawLineNumbers(ctx);
            }

            // New render path: iterate the per-line layout cache and dispatch each visible line
            // through RenderLineLayout. Selection backgrounds and syntax colors are already
            // baked in as ITextLayout attributes (Step 4 / 4b / 6).
            ctx->PushState();
            ctx->ClipRect(visibleTextArea);
            int viewportTop    = verticalScrollOffset;
            int viewportBottom = viewportTop + visibleTextArea.height;
            LineLayoutBase* ll;
            for (int i = 0; i < (int)lineLayouts.size(); i++) {
                ll = GetActualLineLayout(i);
                if (!ll) continue;
                int lineTop    = ll->bounds.y;
                int lineBottom = lineTop + ll->bounds.height;
                if (lineBottom < viewportTop)  continue;
                if (lineTop    > viewportBottom) break;

                RenderLineLayout(ctx, ll);
            }
            ctx->PopState();

//            if (IsFocused() && cursorVisible && !isReadOnly) {
                DrawCursor(ctx);
//            }
        }

        DrawBorder(ctx);
        DrawScrollbars(ctx);
        ctx->PopState();
    }

    void UltraCanvasTextArea::DrawBorder(IRenderContext* context) {
        auto bounds = GetBounds();
        if (style.borderWidth > 0) {
            context->DrawFilledRectangle(bounds, Colors::Transparent, style.borderWidth, style.borderColor);
        }
    }

    // DrawPlainText / DrawHighlightedText removed in Step 8 — rendering goes through
    // RenderLineLayout driven from Render().

    void UltraCanvasTextArea::DrawLineNumbers(IRenderContext* context) {
        auto bounds = GetBounds();
        int gutterW = computedLineNumbersWidth;

        context->SetFillPaint(style.lineNumbersBackgroundColor);
        context->FillRectangle(Rect2Df(bounds.x, bounds.y, gutterW, bounds.height));

        context->SetStrokePaint(style.borderColor);
        context->SetStrokeWidth(1);
        context->DrawLine({bounds.x + gutterW, bounds.y},
                          {bounds.x + gutterW, bounds.y + bounds.height});

        context->SetFontFace(style.fontStyle.fontFamily, style.fontStyle.fontWeight, style.fontStyle.fontSlant);
        context->SetFontSize(style.fontStyle.fontSize);
        context->SetTextAlignment(TextAlignment::Right);

        Rect2Df gutterClip{bounds.x, visibleTextArea.y, gutterW, visibleTextArea.height};
        context->PushState();
        context->ClipRect(gutterClip);

        // Iterate the per-line layout cache; draw the number for each logical line at the Y of
        // its layout's bounds.
        int viewportTop    = verticalScrollOffset;
        int viewportBottom = viewportTop + visibleTextArea.height;
        for (int i = 0; i < (int)lineLayouts.size(); i++) {
            const auto& ll = GetActualLineLayout(i);
            if (!ll) continue;
            int lineTop    = ll->bounds.y;
            int lineBottom = lineTop + ll->bounds.height;
            if (lineBottom < viewportTop) continue;
            if (lineTop    > viewportBottom) break;

            int numY = visibleTextArea.y + lineTop - verticalScrollOffset;
            if (IsFocused() && i == cursorPosition.lineIndex) {
                context->SetFillPaint(Color(255, 128, 128, 255));
                context->FillRectangle(Rect2Df(bounds.x, numY, gutterW, ll->bounds.height));
                context->SetTextPaint(style.fontColor);
                context->SetFontWeight(FontWeight::Bold);
            } else {
                context->SetTextPaint(style.lineNumbersColor);
                context->SetFontWeight(FontWeight::Normal);
            }
            context->DrawTextInRect(std::to_string(i + 1),
                Rect2Df(bounds.x, numY, gutterW - 4, ll->bounds.height));
        }
        context->PopState();
    }

    // DrawSelection removed in Step 8 — selection is now rendered as a Pango background-color
    // attribute inside each line's ITextLayout (applied in ApplyLineSelectionBackground).

    void UltraCanvasTextArea::DrawBackground(IRenderContext* context) {
        auto bounds = GetBounds();
        context->SetFillPaint(style.backgroundColor);
        context->FillRectangle(bounds);

        if (highlightCurrentLine && IsFocused() && currentLine) {
            int highlightX = style.showLineNumbers ? bounds.x + computedLineNumbersWidth : bounds.x;
            int highlightW = bounds.width - (style.showLineNumbers ? computedLineNumbersWidth : 0);
            int highlightY = visibleTextArea.y + currentLine->bounds.y - verticalScrollOffset;
            context->PushState();
            context->ClipRect(visibleTextArea);
            context->SetFillPaint(style.currentLineHighlightColor);
            context->FillRectangle(Rect2Df(highlightX, highlightY, highlightW, currentLine->bounds.height));
            context->PopState();
        }
    }

    void UltraCanvasTextArea::DrawCursor(IRenderContext* context) {
        Rect2Di cursorRect = LineColumnToCursorPos(cursorPosition);
        if (!cursorRect.IsValid()) return;

        // Clip to visible text area.
        if (cursorRect.y + cursorRect.height < visibleTextArea.y) return;
        if (cursorRect.y > visibleTextArea.y + visibleTextArea.height) return;
        if (cursorRect.x > visibleTextArea.x + visibleTextArea.width) return;
        if (cursorRect.x < visibleTextArea.x) return;

        int caretHeight = cursorRect.height > 0 ? cursorRect.height
                                                : (computedLineHeight > 0 ? computedLineHeight : 16);
        context->PushState();
        context->SetStrokeWidth(2);
        context->DrawLine({cursorRect.x, cursorRect.y},
                          {cursorRect.x, cursorRect.y + caretHeight}, style.cursorColor);
        context->PopState();
    }

    void UltraCanvasTextArea::DrawScrollbars(IRenderContext* context) {
        auto bounds = GetBounds();

        if (IsNeedVerticalScrollbar()) {
            int scrollbarX = bounds.x + bounds.width - 15;
            int scrollbarHeight = bounds.height - (IsNeedHorizontalScrollbar() ? 15 : 0);

            // Hex mode still uses row-index scrolling; text mode uses pixel content height
            // derived from the built line layouts.
            int thumbHeight, thumbY = bounds.y, maxThumbY;
            if (editingMode == TextAreaEditingMode::Hex) {
                int totalLines  = hexTotalRows;
                int visibleLines = hexMaxVisibleRows;
                int firstLine    = hexFirstVisibleRow;
                thumbHeight = std::max(20, (visibleLines * scrollbarHeight) / std::max(1, totalLines));
                maxThumbY = scrollbarHeight - thumbHeight;
                if (totalLines > visibleLines && maxThumbY > 0) {
                    thumbY = bounds.y + (firstLine * maxThumbY) / (totalLines - visibleLines);
                }
            } else {
                int contentHeight = 0;
                if (!lineLayouts.empty() && lineLayouts.back()) {
                    contentHeight = lineLayouts.back()->bounds.y + lineLayouts.back()->bounds.height;
                } else {
                    contentHeight = (int)lines.size() * std::max(1, computedLineHeight);
                }
                int viewportH = visibleTextArea.height;
                thumbHeight = std::max(20, (viewportH * scrollbarHeight) / std::max(1, contentHeight));
                maxThumbY = scrollbarHeight - thumbHeight;
                if (contentHeight > viewportH && maxThumbY > 0) {
                    thumbY = bounds.y + (verticalScrollOffset * maxThumbY) / (contentHeight - viewportH);
                }
            }

            context->SetFillPaint(style.scrollbarTrackColor);
            context->FillRectangle(Rect2Df(scrollbarX, bounds.y, 15, scrollbarHeight));

            verticalScrollThumb = {scrollbarX, thumbY, 15, thumbHeight};

            context->SetFillPaint(style.scrollbarColor);
            context->FillRectangle(Rect2Df(scrollbarX + 2, thumbY + 2, 11, thumbHeight - 4));
        }

        if (IsNeedHorizontalScrollbar()) {
            float scrollbarY = static_cast<float>(bounds.y + bounds.height - 15);
            float scrollbarWidth = static_cast<float>(bounds.width - (IsNeedVerticalScrollbar() ? 15 : 0));

            float thumbWidthRatio = static_cast<float>(visibleTextArea.width) / static_cast<float>(maxLineWidth);
            float thumbWidth = std::max(20.0f, scrollbarWidth * thumbWidthRatio);

            float maxThumbX = scrollbarWidth - thumbWidth;
            float thumbX = static_cast<float>(bounds.x);

            if (maxLineWidth > visibleTextArea.width && maxThumbX > 0) {
                float scrollRatio = static_cast<float>(horizontalScrollOffset) /
                                    static_cast<float>(maxLineWidth - visibleTextArea.width);
                thumbX = static_cast<float>(bounds.x) + scrollRatio * maxThumbX;
            }

            context->SetFillPaint(style.scrollbarTrackColor);
            context->FillRectangle(Rect2Df(static_cast<float>(bounds.x), scrollbarY, scrollbarWidth, 15.0f));

            horizontalScrollThumb = {static_cast<int>(thumbX), static_cast<int>(scrollbarY), static_cast<int>(thumbWidth), 15};

            context->SetFillPaint(style.scrollbarColor);
            context->FillRectangle(Rect2Df(thumbX + 2, scrollbarY + 2, thumbWidth - 4, 11.0f));
        }
    }

    // DrawSearchHighlights removed in Step 8 — search highlights will be reintroduced as an
    // attribute pass over the cached ITextLayouts in a follow-up.

    void UltraCanvasTextArea::DrawAutoComplete(IRenderContext* context) {
        // Placeholder for auto-complete rendering
    }

    void UltraCanvasTextArea::DrawMarkers(IRenderContext* context) {
        // Placeholder for error/warning marker rendering
    }
// ===== EVENT HANDLING =====

    bool UltraCanvasTextArea::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        // Hex mode delegates to its own handlers
        if (editingMode == TextAreaEditingMode::Hex) {
            switch (event.type) {
                case UCEventType::MouseDown:
                    return HandleHexMouseDown(event);
                case UCEventType::MouseUp:
                    if (isDraggingVerticalThumb) {
                        isDraggingVerticalThumb = false;
                        UltraCanvasApplication::GetInstance()->ReleaseMouse(this);
                        return true;
                    }
                    if (hexIsSelectingWithMouse) {
                        hexIsSelectingWithMouse = false;
                        hexSelectionAnchor = -1;
                        UltraCanvasApplication::GetInstance()->ReleaseMouse(this);
                        RequestRedraw();
                    }
                    return true;
                case UCEventType::MouseMove:
                    return HandleHexMouseMove(event);
                case UCEventType::KeyDown:
                    return HandleHexKeyDown(event);
                case UCEventType::MouseWheel:
                    return HandleMouseWheel(event);
                case UCEventType::FocusGained:
                    cursorVisible = true;
                    cursorBlinkTime = 0;
                    return true;
                case UCEventType::FocusLost:
                    cursorVisible = false;
                    return true;
                default:
                    return false;
            }
        }

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);
            case UCEventType::MouseUp:
                return HandleMouseUp(event);
            case UCEventType::MouseMove:
                return HandleMouseMove(event);
            case UCEventType::MouseDoubleClick:
                return HandleMouseDoubleClick(event);
            case UCEventType::KeyDown:
                return HandleKeyDown(event);
            case UCEventType::MouseWheel:
                return HandleMouseWheel(event);
            case UCEventType::FocusGained:
                cursorVisible = true;
                cursorBlinkTime = 0;
                RequestRedraw();
                return true;
            case UCEventType::FocusLost:
                cursorVisible = false;
                // Return the in-edit line to its formatted MD layout while the widget is idle
                // (or discard the stash if the line was modified). On focus regain, the swap
                // re-runs via ReconcileLayoutState.
                currentLine.reset();
                RequestRedraw();
                return true;
            default:
                return false;
        }
    }

    bool UltraCanvasTextArea::HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return false;

        // Scrollbar thumb dragging takes priority
        if (IsNeedVerticalScrollbar() && verticalScrollThumb.Contains(event.x, event.y)) {
            isDraggingVerticalThumb = true;
            dragStartOffset.y = event.globalY - verticalScrollThumb.y;
            UltraCanvasApplication::GetInstance()->CaptureMouse(this);
            return true;
        }

        if (IsNeedHorizontalScrollbar() && horizontalScrollThumb.Contains(event.x, event.y)) {
            isDraggingHorizontalThumb = true;
            dragStartOffset.x = event.globalX - horizontalScrollThumb.x;
            UltraCanvasApplication::GetInstance()->CaptureMouse(this);
            return true;
        }

        SetFocus(true);

        // --- Markdown link/image click: intercept before cursor move ---
        if (editingMode == TextAreaEditingMode::MarkdownHybrid && HandleMarkdownClick(event.x, event.y)) {
            return true;
        }

        // --- Click counting for single / double / triple click ---
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClickTime).count();
        int dx = std::abs(event.x - lastClickX);
        int dy = std::abs(event.y - lastClickY);

        if (elapsed <= MultiClickTimeThresholdMs &&
            dx <= MultiClickDistanceThreshold &&
            dy <= MultiClickDistanceThreshold) {
            clickCount++;
        } else {
            clickCount = 1;
        }

        lastClickTime = now;
        lastClickX = event.x;
        lastClickY = event.y;

        // --- Compute clicked text position ---
        LineColumnIndex clicked = PosToLineColumn({event.x, event.y});
        int clickedLine = std::max(0, clicked.lineIndex);
        int clickedCol  = std::max(0, clicked.columnIndex);

        // --- Act on click count ---
        if (clickCount >= 3) {
            // Triple click: select entire line
            clickCount = 3; // Cap to prevent quad-click escalation
            HandleMouseTripleClick(event);
            return true;
        }

        if (clickCount == 2) {
            // cursorPosition = {clickedLine, clickedCol};
            // currentLineIndex = clickedLine;
            // SelectWord();
            // selectionAnchor = selectionStart;
            // isSelectingText = true;
            // UltraCanvasApplication::GetInstance()->CaptureMouse(this);
            return true;
        }

        // --- Single click ---
        cursorPosition = {clickedLine, clickedCol};

        if (event.shift && selectionStart.lineIndex >= 0) {
            selectionEnd = cursorPosition;
        } else {
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
            selectionAnchor = cursorPosition;
        }

        // Begin text drag selection tracking
        isSelectingText = true;
        UltraCanvasApplication::GetInstance()->CaptureMouse(this);

        RequestRedraw();
        return true;
    }

    bool UltraCanvasTextArea::HandleMouseDoubleClick(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return false;

        LineColumnIndex clicked = PosToLineColumn({event.x, event.y});
        if (clicked.lineIndex < 0) return true;
        cursorPosition = clicked;

        SelectWord();
        selectionAnchor = selectionStart;
        //isSelectingText = true;
        clickCount = 2;
        //UltraCanvasApplication::GetInstance()->CaptureMouse(this);
        return true;
    }

    bool UltraCanvasTextArea::HandleMouseTripleClick(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return false;

        LineColumnIndex clicked = PosToLineColumn({event.x, event.y});
        if (clicked.lineIndex < 0) return true;
        SelectLine(clicked.lineIndex);
        selectionAnchor = selectionStart;
        //isSelectingText = true;
        //UltraCanvasApplication::GetInstance()->CaptureMouse(this);
        return true;
    }

    bool UltraCanvasTextArea::HandleMouseMove(const UCEvent& event) {
        // Scrollbar thumb dragging (pixel-based).
        if (isDraggingVerticalThumb) {
            auto bounds = GetBounds();
            int scrollbarHeight = bounds.height - (IsNeedHorizontalScrollbar() ? 15 : 0);
            int thumbHeight = verticalScrollThumb.height;
            int maxThumbY = scrollbarHeight - thumbHeight;

            int contentHeight = 0;
            if (!lineLayouts.empty() && lineLayouts.back()) {
                contentHeight = lineLayouts.back()->bounds.y + lineLayouts.back()->bounds.height;
            } else {
                contentHeight = (int)lines.size() * std::max(1, computedLineHeight);
            }
            int viewportH = visibleTextArea.height;

            int newThumbY = event.globalY - dragStartOffset.y - bounds.y;
            newThumbY = std::max(0, std::min(newThumbY, maxThumbY));

            if (maxThumbY > 0 && contentHeight > viewportH) {
                verticalScrollOffset =
                    (newThumbY * (contentHeight - viewportH)) / maxThumbY;
                verticalScrollOffset = std::max(0, std::min(verticalScrollOffset, contentHeight - viewportH));
            }

            RequestRedraw();
            return true;
        }

        if (isDraggingHorizontalThumb) {
            auto bounds = GetBounds();
            float scrollbarWidth = static_cast<float>(bounds.width - (IsNeedVerticalScrollbar() ? 15 : 0));
            float thumbWidth = static_cast<float>(horizontalScrollThumb.width);
            float maxThumbX = scrollbarWidth - thumbWidth;

            float newThumbX = static_cast<float>(event.globalX - dragStartOffset.x - bounds.x);
            newThumbX = std::max(0.0f, std::min(newThumbX, maxThumbX));

            if (maxThumbX > 0 && maxLineWidth > visibleTextArea.width) {
                horizontalScrollOffset = static_cast<int>((newThumbX / maxThumbX) *
                                                          static_cast<float>(maxLineWidth - visibleTextArea.width));
                horizontalScrollOffset = std::max(0, std::min(horizontalScrollOffset, maxLineWidth - visibleTextArea.width));
            }

            RequestRedraw();
            return true;
        }

        // --- Scrollbar hover cursor ---
        if (!isSelectingText) {
            auto bounds = GetBounds();
            if (IsNeedVerticalScrollbar() &&
                event.x >= bounds.x + bounds.width - 15 && event.x <= bounds.x + bounds.width &&
                event.y >= bounds.y && event.y <= bounds.y + bounds.height) {
                SetMouseCursor(UCMouseCursor::SizeNS);
                return true;
            }
            if (IsNeedHorizontalScrollbar() &&
                event.y >= bounds.y + bounds.height - 15 && event.y <= bounds.y + bounds.height &&
                event.x >= bounds.x && event.x <= bounds.x + bounds.width) {
                SetMouseCursor(UCMouseCursor::SizeWE);
                return true;
            }
            SetMouseCursor(UCMouseCursor::Text);
        }

        // --- Markdown hover: update cursor for links/images ---
        if (editingMode == TextAreaEditingMode::MarkdownHybrid && !isSelectingText) {
            if (!HandleMarkdownHover(event.x, event.y)) {
                SetMouseCursor(UCMouseCursor::Text);
            }
        }

        // --- Text drag selection ---
        if (isSelectingText && selectionAnchor.lineIndex >= 0) {
            LineColumnIndex drag = PosToLineColumn({event.x, event.y});
            if (drag.lineIndex < 0) return true;

            selectionStart = selectionAnchor;
            selectionEnd   = drag;
            cursorPosition = selectionEnd;

            if (event.y < visibleTextArea.y)                             ScrollUp(1);
            else if (event.y > visibleTextArea.y + visibleTextArea.height) ScrollDown(1);
            if (event.x < visibleTextArea.x)                             ScrollLeft(1);
            else if (event.x > visibleTextArea.x + visibleTextArea.width) ScrollRight(1);

            RequestRedraw();
            if (onSelectionChanged) {
                onSelectionChanged(GetPositionFromLineColumn(selectionStart.lineIndex, selectionStart.columnIndex),
                                   GetPositionFromLineColumn(selectionEnd.lineIndex,   selectionEnd.columnIndex));
            }
            return true;
        }

        return false;
    }

    bool UltraCanvasTextArea::HandleMouseUp(const UCEvent& event) {
        bool wasHandled = false;

        // Finalize scrollbar dragging
        if (isDraggingVerticalThumb || isDraggingHorizontalThumb) {
            isDraggingVerticalThumb = false;
            isDraggingHorizontalThumb = false;
            UltraCanvasApplication::GetInstance()->ReleaseMouse(this);
            return true;
        }

        // Finalize text selection dragging
        if (isSelectingText) {
            isSelectingText = false;
            selectionAnchor = LineColumnIndex::INVALID;
            UltraCanvasApplication::GetInstance()->ReleaseMouse(this);

            // If start equals end, there's no real selection — clear it
            if (selectionStart.lineIndex >= 0 &&
                selectionStart.lineIndex  == selectionEnd.lineIndex &&
                selectionStart.columnIndex == selectionEnd.columnIndex) {
                selectionStart = LineColumnIndex::INVALID;
                selectionEnd   = LineColumnIndex::INVALID;
            }

            RequestRedraw();
            wasHandled = true;
        }

        return wasHandled;
    }

    bool UltraCanvasTextArea::HandleMouseDrag(const UCEvent& event) {
        return HandleMouseMove(event);
    }

    bool UltraCanvasTextArea::HandleMouseWheel(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return false;

        int scrollAmount = 1;

        if (editingMode == TextAreaEditingMode::Hex) {
            if (event.wheelDelta > 0) {
                hexFirstVisibleRow = std::max(0, hexFirstVisibleRow - scrollAmount);
            } else {
                int maxFirstRow = std::max(0, hexTotalRows - hexMaxVisibleRows);
                hexFirstVisibleRow = std::min(maxFirstRow, hexFirstVisibleRow + scrollAmount);
            }
        } else {
            int h = std::max(1, computedLineHeight);
            if (event.wheelDelta > 0) {
                ScrollUp(3);
            } else {
                ScrollDown(3);
            }
        }

        RequestRedraw();
        return true;
    }

    bool UltraCanvasTextArea::HandleKeyDown(const UCEvent& event) {
        bool handled = true;

        switch (event.virtualKey) {
            case UCKeys::Left:
                if (event.ctrl && !event.alt) {
                    MoveCursorWordLeft(event.shift);
                } else {
                    MoveCursorLeft(event.shift);
                }
                EnsureCursorVisible();
                break;
            case UCKeys::Right:
                if (event.ctrl && !event.alt) {
                    MoveCursorWordRight(event.shift);
                } else {
                    MoveCursorRight(event.shift);
                }
                EnsureCursorVisible();
                break;
            case UCKeys::Up:
                if (event.ctrl && !event.alt) {
                    ScrollUp(1);
                } else {
                    MoveCursorUp(event.shift);
                }
                EnsureCursorVisible();
                break;
            case UCKeys::Down:
                if (event.ctrl && !event.alt) {
                    ScrollDown(1);
                } else {
                    MoveCursorDown(event.shift);
                }
                EnsureCursorVisible();
                break;
            case UCKeys::Home:
                if (event.ctrl) {
                    MoveCursorToStart(event.shift);
                } else {
                    MoveCursorToLineStart(event.shift);
                }
                EnsureCursorVisible();
                break;
            case UCKeys::End:
                if (event.ctrl) {
                    MoveCursorToEnd(event.shift);
                } else {
                    MoveCursorToLineEnd(event.shift);
                }
                EnsureCursorVisible();
                break;
            case UCKeys::PageUp:
                MoveCursorPageUp(event.shift);
                EnsureCursorVisible();
                break;
            case UCKeys::PageDown:
                MoveCursorPageDown(event.shift);
                EnsureCursorVisible();
                break;
            case UCKeys::Backspace:
                if (HasSelection()) DeleteSelection();
                else DeleteCharacterBackward();
                EnsureCursorVisible();
                break;
            case UCKeys::Delete:
                if (!event.shift && event.ctrl && !event.alt) {
                    CutSelection();
                } else if (HasSelection()) {
                    DeleteSelection();
                } else {
                    DeleteCharacterForward();
                }
                EnsureCursorVisible();
                break;
            case UCKeys::Enter:
                if (HasSelection()) DeleteSelection();
                InsertNewLine();
                EnsureCursorVisible();
                break;
            case UCKeys::Tab:
                if (!event.alt && !event.ctrl && !event.shift) {
                    if (HasSelection()) DeleteSelection();
                    InsertTab();
                    EnsureCursorVisible();
                } else {
                    handled = false;
                }
                break;
            case UCKeys::A:
                if (!event.shift && event.ctrl && !event.alt) {
                    SelectAll();
                } else {
                    handled = false;
                }
                break;
            case UCKeys::C:
                if (!event.shift && event.ctrl && !event.alt) {
                    CopySelection();
                } else {
                    handled = false;
                }
                break;
            case UCKeys::X:
                if (!event.shift && event.ctrl && !event.alt) {
                    CutSelection();
                } else {
                    handled = false;
                }
                break;
            case UCKeys::V:
                if (!event.shift && event.ctrl && !event.alt) { 
                    PasteClipboard(); 
                    EnsureCursorVisible(); 
                } else {
                    handled = false;
                }
                break;
            case UCKeys::Insert:
                if (event.shift && !event.ctrl && !event.alt) { 
                    PasteClipboard(); EnsureCursorVisible(); 
                } else if (!event.shift && event.ctrl && !event.alt) {
                    CopySelection();
                } else {
                    handled = false;
                }
                break;
            case UCKeys::Z:
                if (event.ctrl) {
                    if (event.shift) Redo();
                    else Undo();
                    EnsureCursorVisible();
                } else handled = false;
                break;
            case UCKeys::Y:
                if (event.ctrl) { Redo(); EnsureCursorVisible(); }
                else handled = false;
                break;
            case UCKeys::Escape:
                break;
            default:
                handled = false;
                break;
        }

        // Handle UTF-8 text input
        if (!handled && !event.ctrl && !event.alt) {
            std::string insertTxt = event.text;
            if (insertTxt.empty() && event.character != 0 && std::isprint(event.character)) {
                std::string charStr(1, event.character);
                insertTxt = charStr;
            }
            if (!event.text.empty()) {
                if (HasSelection()) {
                    DeleteSelection();
                }
                InsertText(event.text);
                EnsureCursorVisible();
                handled = true;
            }
        }

        return handled;
    }

// ===== HELPER METHODS =====

    void UltraCanvasTextArea::EnsureCursorVisible() {
        // Use the cached per-line layouts to find the cursor line's pixel Y. If the layout
        // isn't built yet (edit happened before a geometry pass), fall back to style.fontStyle
        // line height as a rough per-line pixel size.
        if (!currentLine) return;
        int lineTop = currentLine->bounds.y;
        int lineHeight = currentLine->bounds.height;
        int lineBottom = lineTop + lineHeight;

        if (lineTop < verticalScrollOffset) {
            verticalScrollOffset = lineTop;
        } else if (lineBottom > verticalScrollOffset + visibleTextArea.height) {
            verticalScrollOffset = lineBottom - visibleTextArea.height;
        }
        if (verticalScrollOffset < 0) verticalScrollOffset = 0;

        // Horizontal scroll (only when not word-wrapping).
        if (!wordWrap) {
            int line = cursorPosition.lineIndex;
            int col  = cursorPosition.columnIndex;
            if (col > 0 && line < static_cast<int>(lines.size())) {
                std::string textToCursor = utf8_substr(lines[line], 0, col);
                int cursorX = MeasureTextWidth(textToCursor);
                int visibleWidth = visibleTextArea.width;
                if (cursorX < horizontalScrollOffset) {
                    horizontalScrollOffset = cursorX;
                } else if (cursorX > horizontalScrollOffset + visibleWidth) {
                    horizontalScrollOffset = cursorX - visibleWidth;
                }
                if (maxLineWidth > visibleWidth) {
                    horizontalScrollOffset = std::min(horizontalScrollOffset, maxLineWidth - visibleWidth);
                }
            } else {
                horizontalScrollOffset = 0;
            }
        } else {
            horizontalScrollOffset = 0;
        }
        RequestRedraw();
    }

    void UltraCanvasTextArea::CalculateVisibleArea() {
        auto ctx = GetRenderContext();
        if (!ctx) return;
        
        visibleTextArea = GetBounds();
        visibleTextArea.x += style.padding;
        visibleTextArea.y += style.padding;
        visibleTextArea.width -= style.padding * 2;
        visibleTextArea.height -= style.padding * 2;
        
        if (style.showLineNumbers) {
            computedLineNumbersWidth = CalculateLineNumbersWidth(ctx);
            visibleTextArea.x += (computedLineNumbersWidth + 5);
            visibleTextArea.width -= (computedLineNumbersWidth + 5);
        }

        ctx->PushState();
        ctx->SetFontStyle(style.fontStyle);
        ctx->SetFontWeight(FontWeight::Normal);
        computedLineHeight = static_cast<int>(static_cast<float>(ctx->GetTextLineHeight("M")) * style.lineHeight);
        if (wordWrap) {
            maxLineWidth = visibleTextArea.width;
        } else {
            maxLineWidth = 0;
            for (const auto& line : lines) {
                maxLineWidth = std::max(maxLineWidth, ctx->GetTextLineWidth(line));
            }
        }
        ctx->PopState();
        // Reserve space for the scrollbars. visibleTextArea gets shrunk so downstream layout
        // width/height calculations use the true text area.
        bool needVerticalScrollbar   = false;
        bool needHorizontalScrollbar = false;
        if (IsNeedHorizontalScrollbar()) {
            needHorizontalScrollbar = true;
            visibleTextArea.height -= 15;
            if (IsNeedVerticalScrollbar()) {
                needVerticalScrollbar = true;
                visibleTextArea.width -= 15;
            }
        } else if (IsNeedVerticalScrollbar()) {
            needVerticalScrollbar = true;
            visibleTextArea.width -= 15;
            if (IsNeedHorizontalScrollbar()) {
                needHorizontalScrollbar = true;
                visibleTextArea.height -= 15;
            }
        }

        // UpdateLineLayouts detects wrap-width changes via lineLayoutsWrapWidth and
        // invalidates the cache automatically.

        if (!needVerticalScrollbar && IsNeedVerticalScrollbar()) {
            visibleTextArea.width -= 15;
            if (!needHorizontalScrollbar && IsNeedHorizontalScrollbar()) {
                visibleTextArea.height -= 15;
            }
        }

        isNeedRecalculateVisibleArea = false;
    }

    void UltraCanvasTextArea::SetWordWrap(bool wrap) {
        if (wordWrap == wrap) return;
        wordWrap = wrap;
        horizontalScrollOffset = 0;
        isNeedRecalculateVisibleArea = true;
        RequestRedraw();
    }

    // ===== LINE LAYOUT CACHE =====

    void UltraCanvasTextArea::InvalidateAllLineLayouts() {
        for (auto& ll : lineLayouts) {
            ll.reset();
        }
    }

    void UltraCanvasTextArea::InvalidateLineLayout(int logicalLine) {
        if (logicalLine >= 0 && logicalLine < static_cast<int>(lineLayouts.size())) {
            lineLayouts[logicalLine].reset();
            if (logicalLine == cursorPosition.lineIndex) {
                currentLine.reset();
            }
        }
    }

    void UltraCanvasTextArea::InsertLineLayoutEntry(int logicalLine) {
        if (logicalLine < 0) return;
        if (logicalLine > static_cast<int>(lineLayouts.size())) {
            lineLayouts.resize(logicalLine);
        }
        lineLayouts.insert(lineLayouts.begin() + logicalLine, nullptr);
    }

    void UltraCanvasTextArea::RemoveLineLayoutEntry(int logicalLine) {
        if (logicalLine >= 0 && logicalLine < static_cast<int>(lineLayouts.size())) {
            lineLayouts.erase(lineLayouts.begin() + logicalLine);
        }
    }

    // Step 6 (pending): wire selection-background attributes.
    void UltraCanvasTextArea::ApplySelectionAttributes() {
    }

    void UltraCanvasTextArea::ClearSelectionAttributes(int /*logicalLine*/) {
    }

    // Called at the top of UpdateLineLayouts each frame. Detects cursor-line changes
    // (so MarkdownHybrid can swap in/out raw editing) and selection-range changes (so affected
    // line layouts rebuild with fresh background-color attributes).
    void UltraCanvasTextArea::ReconcileLayoutState() {
        // If the selection endpoints changed since the attributes were last applied, invalidate
        // the union of old & new line ranges so MakeLineLayout rebuilds them with fresh
        // background-color attributes (Step 6).
        bool selectionMoved =
            !(lastAppliedSelectionStart.lineIndex  == selectionStart.lineIndex &&
              lastAppliedSelectionStart.columnIndex == selectionStart.columnIndex &&
              lastAppliedSelectionEnd.lineIndex    == selectionEnd.lineIndex &&
              lastAppliedSelectionEnd.columnIndex  == selectionEnd.columnIndex);
        if (selectionMoved) {
            auto lineRange = [](LineColumnIndex s, LineColumnIndex e, int& first, int& last) {
                if (s.lineIndex < 0 || e.lineIndex < 0) { first = last = -1; return; }
                first = std::min(s.lineIndex, e.lineIndex);
                last  = std::max(s.lineIndex, e.lineIndex);
            };
            int oldFirst, oldLast, newFirst, newLast;
            lineRange(lastAppliedSelectionStart, lastAppliedSelectionEnd, oldFirst, oldLast);
            lineRange(selectionStart, selectionEnd, newFirst, newLast);
            int invStart = -1, invEnd = -1;
            if (oldFirst >= 0) { invStart = oldFirst; invEnd = oldLast; }
            if (newFirst >= 0) {
                invStart = (invStart < 0) ? newFirst : std::min(invStart, newFirst);
                invEnd   = (invEnd   < 0) ? newLast  : std::max(invEnd,   newLast);
            }
            for (int i = invStart; invStart >= 0 && i <= invEnd && i < (int)lineLayouts.size(); i++) {
                lineLayouts[i].reset();
            }
            lastAppliedSelectionStart = selectionStart;
            lastAppliedSelectionEnd   = selectionEnd;
        }
    }

    // Apply the selection background-color attribute to the portion of this line's layout text
    // that falls inside the current selection. Coordinates:
    //   selection* is in real-line codepoints (columnIndex); layout text may have a markup prefix
    //   stripped (layoutTextStartIndex in codepoints), so we subtract that before converting to
    //   layout-text bytes via utf8_cp_to_byte.
    void UltraCanvasTextArea::ApplyLineSelectionBackground(LineLayoutBase* ll, int lineIndex) {
        if (!ll || !ll->layout) return;
        if (selectionStart.lineIndex < 0 || selectionEnd.lineIndex < 0) return;

        LineColumnIndex a = selectionStart, b = selectionEnd;
        if (a.lineIndex > b.lineIndex ||
            (a.lineIndex == b.lineIndex && a.columnIndex > b.columnIndex)) {
            std::swap(a, b);
        }
        if (lineIndex < a.lineIndex || lineIndex > b.lineIndex) return;
        if (a.lineIndex == b.lineIndex && a.columnIndex == b.columnIndex) return;

        int realLineCpCount = utf8_length(lines[lineIndex]);
        int startCp = (lineIndex == a.lineIndex) ? a.columnIndex : 0;
        int endCp   = (lineIndex == b.lineIndex) ? b.columnIndex : realLineCpCount;

        // Translate from real-line codepoints to layout-text codepoints.
        startCp -= ll->layoutTextStartIndex;
        endCp   -= ll->layoutTextStartIndex;
        if (startCp < 0) startCp = 0;
        std::string layoutText = ll->layout->GetText();
        int layoutCpCount = utf8_length(layoutText);
        if (endCp > layoutCpCount) endCp = layoutCpCount;
        if (startCp >= endCp) return;

        int startByte = static_cast<int>(utf8_cp_to_byte(layoutText, startCp));
        int endByte   = static_cast<int>(utf8_cp_to_byte(layoutText, endCp));

        auto bgAttr = TextAttributeFactory::CreateBackground(style.selectionColor);
        bgAttr->SetRange(startByte, endByte);
        ll->layout->ChangeAttribute(std::move(bgAttr));
        //debugOutput << "Layout attrs: " << ll->layout->GetAttributesAsString() << " ltype: " << ll->layoutType << " str:" << layoutText << " startbyte:" << startByte << " endbyte:" << endByte << std::endl;
    }

    // Doc-wide scan: pick up abbreviation definitions, footnote definitions, heading anchors,
    // and tally anchor link references. Runs once when markdownIndexDirty is set (after text
    // edits); read by MakeMarkdownLineLayout to decide anchor-return icons and abbreviation /
    // footnote tooltips.
    void UltraCanvasTextArea::RebuildMarkdownIndex() {
        markdownAbbreviations.clear();
        markdownFootnotes.clear();
        markdownAnchors.clear();
        markdownAnchorBacklinks.clear();
        std::unordered_map<std::string, std::vector<int>> anchorRefs;

        for (int i = 0; i < (int)lines.size(); i++) {
            const std::string& line = lines[i];
            std::string trimmed = TrimWhitespace(line);
            if (trimmed.empty()) continue;

            // Abbreviation: *[TERM]: expansion
            if (trimmed.size() >= 5 && trimmed[0] == '*' && trimmed[1] == '[') {
                auto close = trimmed.find("]:", 2);
                if (close != std::string::npos) {
                    std::string abbr = trimmed.substr(2, close - 2);
                    std::string exp  = trimmed.substr(close + 2);
                    auto firstNonWs = exp.find_first_not_of(" \t");
                    if (firstNonWs != std::string::npos) exp = exp.substr(firstNonWs);
                    if (!abbr.empty()) markdownAbbreviations[abbr] = exp;
                    continue;
                }
            }

            // Footnote definition: [^label]: content
            if (trimmed.size() >= 5 && trimmed[0] == '[' && trimmed[1] == '^') {
                auto close = trimmed.find("]:", 2);
                if (close != std::string::npos) {
                    std::string label = trimmed.substr(2, close - 2);
                    std::string content = trimmed.substr(close + 2);
                    auto firstNonWs = content.find_first_not_of(" \t");
                    if (firstNonWs != std::string::npos) content = content.substr(firstNonWs);
                    if (!label.empty()) markdownFootnotes[label] = content;
                    continue;
                }
            }

            // Heading with `{#id}` anchor.
            if (trimmed[0] == '#') {
                int level = 0;
                while (level < 6 && level < (int)trimmed.size() && trimmed[level] == '#') level++;
                if (level > 0 && level < (int)trimmed.size() && trimmed[level] == ' ' &&
                    trimmed.back() == '}') {
                    auto open = trimmed.rfind("{#");
                    if (open != std::string::npos && open + 2 < trimmed.size() - 1) {
                        std::string id = trimmed.substr(open + 2, trimmed.size() - 1 - (open + 2));
                        if (!id.empty()) markdownAnchors[id] = i;
                    }
                }
            }

            // Anchor references [text](#id). Skip [^label] footnote refs. Reference-style
            // [text][ref] links are not supported here.
            for (size_t p = 0; p < line.size(); ) {
                auto open = line.find('[', p);
                if (open == std::string::npos) break;
                if (open + 1 < line.size() && line[open + 1] == '^') { p = open + 1; continue; }
                auto close = line.find(']', open + 1);
                if (close == std::string::npos || close + 2 >= line.size()) break;
                if (line[close + 1] != '(' || line[close + 2] != '#') { p = close + 1; continue; }
                auto urlEnd = line.find(')', close + 3);
                if (urlEnd == std::string::npos) break;
                std::string id = line.substr(close + 3, urlEnd - (close + 3));
                if (!id.empty()) anchorRefs[id].push_back(i);
                p = urlEnd + 1;
            }
        }

        // Record backlinks only for anchors with exactly one referring line (unambiguous).
        for (const auto& [id, srcs] : anchorRefs) {
            if (srcs.size() == 1) markdownAnchorBacklinks[id] = srcs[0];
        }
        markdownIndexDirty = false;
    }

    // Inline markdown scanner. Single-pass, not recursive — doesn't handle nested inline markers
    // (e.g. **bold _italic_**). Marker bytes (`*`, `_`, `` ` ``, `~`, `^`, `[`, `]`, `(`, `)`,
    // `!`) are stripped from the visible output. A trailing `\` escapes the next literal
    // character. Known `:shortcode:` names and word-boundary ASCII emoticons (`:-)`, `;)`,
    // `8-)`, `<3`, …) are expanded to their UTF-8 emoji inline, and typography markers
    // `(c)` / `(r)` / `(tm)` / `(p)` (case-insensitive) expand to © ® ™ ℗. Inline math
    // `$…$` runs LaTeX commands through SubstituteGreekLetters (`\alpha` → α, etc.).
    void UltraCanvasTextArea::ParseInlineMarkdownRuns(const std::string& rawLine,
                                                     std::string& visibleText,
                                                     std::vector<InlineRun>& runs) {
        visibleText.clear();
        runs.clear();
        const int n = (int)rawLine.size();
        int i = 0;

        auto scanPair = [&](int from, char a, char b) -> int {
            for (int j = from; j + 1 < n; j++) {
                if (rawLine[j] == a && rawLine[j + 1] == b) return j;
            }
            return -1;
        };
        auto scanChar = [&](int from, char c) -> int {
            for (int j = from; j < n; j++) if (rawLine[j] == c) return j;
            return -1;
        };

        while (i < n) {
            char c = rawLine[i];

            // Backslash escape.
            if (c == '\\' && i + 1 < n) {
                visibleText.push_back(rawLine[i + 1]);
                i += 2;
                continue;
            }

            // Bold: **...** or __...__
            if (i + 1 < n && (c == '*' || c == '_') && rawLine[i + 1] == c) {
                int close = scanPair(i + 2, c, c);
                if (close > i + 2) {
                    int s = (int)visibleText.size();
                    visibleText.append(rawLine, i + 2, close - (i + 2));
                    runs.push_back({s, (int)visibleText.size(), InlineRun::Bold, {}, {}});
                    i = close + 2;
                    continue;
                }
            }
            // Italic: *...* or _..._
            if (c == '*' || c == '_') {
                int close = scanChar(i + 1, c);
                if (close > i + 1) {
                    int s = (int)visibleText.size();
                    visibleText.append(rawLine, i + 1, close - (i + 1));
                    runs.push_back({s, (int)visibleText.size(), InlineRun::Italic, {}, {}});
                    i = close + 1;
                    continue;
                }
            }
            // Inline code: `...`
            if (c == '`') {
                int close = scanChar(i + 1, '`');
                if (close > i + 1) {
                    int s = (int)visibleText.size();
                    visibleText.append(rawLine, i + 1, close - (i + 1));
                    runs.push_back({s, (int)visibleText.size(), InlineRun::Code, {}, {}});
                    i = close + 1;
                    continue;
                }
            }
            // Strike: ~~...~~
            if (i + 1 < n && c == '~' && rawLine[i + 1] == '~') {
                int close = scanPair(i + 2, '~', '~');
                if (close > i + 2) {
                    int s = (int)visibleText.size();
                    visibleText.append(rawLine, i + 2, close - (i + 2));
                    runs.push_back({s, (int)visibleText.size(), InlineRun::Strike, {}, {}});
                    i = close + 2;
                    continue;
                }
            }
            // Subscript: ~text~ (content non-empty, no whitespace, no '~').
            if (c == '~') {
                int j = i + 1;
                while (j < n && rawLine[j] != '~' &&
                       !std::isspace(static_cast<unsigned char>(rawLine[j]))) {
                    j++;
                }
                if (j > i + 1 && j < n && rawLine[j] == '~') {
                    int s = (int)visibleText.size();
                    visibleText.append(rawLine, i + 1, j - (i + 1));
                    runs.push_back({s, (int)visibleText.size(), InlineRun::Subscript, {}, {}});
                    i = j + 1;
                    continue;
                }
            }
            // Superscript: ^text^ (content non-empty, no whitespace, no '^').
            if (c == '^') {
                int j = i + 1;
                while (j < n && rawLine[j] != '^' &&
                       !std::isspace(static_cast<unsigned char>(rawLine[j]))) {
                    j++;
                }
                if (j > i + 1 && j < n && rawLine[j] == '^') {
                    int s = (int)visibleText.size();
                    visibleText.append(rawLine, i + 1, j - (i + 1));
                    runs.push_back({s, (int)visibleText.size(), InlineRun::Superscript, {}, {}});
                    i = j + 1;
                    continue;
                }
            }
            // Inline math: $...$ — LaTeX commands inside are substituted via
            // SubstituteGreekLetters. Block math `$$...$$` is skipped (the opener check
            // refuses a `$$` pair) so a future block pass can claim it.
            if (c == '$' && !(i + 1 < n && rawLine[i + 1] == '$')) {
                int close = i + 1;
                while (close < n && rawLine[close] != '$') close++;
                if (close < n && close > i + 1 &&
                    !(close + 1 < n && rawLine[close + 1] == '$')) {
                    std::string body = SubstituteGreekLetters(
                        rawLine.substr(i + 1, close - (i + 1)));
                    int s = (int)visibleText.size();
                    visibleText.append(body);
                    runs.push_back({s, (int)visibleText.size(), InlineRun::Math, {}, {}});
                    i = close + 1;
                    continue;
                }
            }
            // Footnote reference: [^label]
            if (c == '[' && i + 1 < n && rawLine[i + 1] == '^') {
                int close = scanChar(i + 2, ']');
                if (close > i + 2) {
                    std::string label = rawLine.substr(i + 2, close - (i + 2));
                    int s = (int)visibleText.size();
                    // Render the label itself inline (e.g. "[^1]" → visible "1"). Subscript-style
                    // tooltip / color is applied by the caller via InlineRun::Footnote.
                    visibleText.append(label);
                    InlineRun run;
                    run.startByte = s;
                    run.endByte   = (int)visibleText.size();
                    run.kind      = InlineRun::Footnote;
                    run.url       = label;
                    runs.push_back(run);
                    i = close + 1;
                    continue;
                }
            }

            // Typography: (c), (C), (r), (R), (tm), (TM), (p), (P) — unconditional expansion.
            if (c == '(') {
                bool matched = false;
                for (const auto& e : TypographyTable()) {
                    int len = (int)std::strlen(e.text);
                    if (i + len > n) continue;
                    if (std::memcmp(rawLine.data() + i, e.text, len) != 0) continue;
                    visibleText.append(e.emoji);
                    i += len;
                    matched = true;
                    break;
                }
                if (matched) continue;
            }

            // Emoji shortcode: :name: — expand to UTF-8 if found in the lookup table.
            // Shortcode chars are alphanumeric, `_`, `+`, `-`; must be non-empty and end with `:`.
            if (c == ':' && i + 2 < n) {
                int j = i + 1;
                while (j < n) {
                    unsigned char cc = (unsigned char)rawLine[j];
                    if (cc == ':') break;
                    if (std::isalnum(cc) || cc == '_' || cc == '+' || cc == '-') { j++; continue; }
                    break;
                }
                if (j > i + 1 && j < n && rawLine[j] == ':') {
                    const std::string& emoji = LookupEmojiShortcode(rawLine.substr(i + 1, j - (i + 1)));
                    if (!emoji.empty()) {
                        visibleText.append(emoji);
                        i = j + 1;
                        continue;
                    }
                }
            }

            // ASCII emoticon: `:-)`, `:)`, `;)`, `8-)`, `<3`, etc. Only at a word boundary —
            // the preceding visible char must not be alphanumeric and the char after the
            // matched token must also not be alphanumeric, so `:Dick` / `http://` don't match.
            {
                bool leftOk = visibleText.empty() ||
                              !std::isalnum(static_cast<unsigned char>(visibleText.back()));
                if (leftOk) {
                    bool matched = false;
                    for (const auto& e : EmoticonTable()) {
                        int len = (int)std::strlen(e.text);
                        if (i + len > n) continue;
                        if (std::memcmp(rawLine.data() + i, e.text, len) != 0) continue;
                        if (i + len < n &&
                            std::isalnum(static_cast<unsigned char>(rawLine[i + len]))) continue;
                        visibleText.append(e.emoji);
                        i += len;
                        matched = true;
                        break;
                    }
                    if (matched) continue;
                }
            }

            // Image ![alt](url) or link [text](url)
            bool isImage = (c == '!' && i + 1 < n && rawLine[i + 1] == '[');
            if (c == '[' || isImage) {
                int textStart = isImage ? i + 2 : i + 1;
                int textEnd = textStart;
                int depth = 1;
                while (textEnd < n && depth > 0) {
                    if (rawLine[textEnd] == '[') depth++;
                    else if (rawLine[textEnd] == ']') { depth--; if (depth == 0) break; }
                    textEnd++;
                }
                if (textEnd < n && textEnd + 1 < n && rawLine[textEnd + 1] == '(') {
                    int urlStart = textEnd + 2;
                    int urlEnd = scanChar(urlStart, ')');
                    if (urlEnd >= urlStart) {
                        std::string visible = rawLine.substr(textStart, textEnd - textStart);
                        std::string url     = rawLine.substr(urlStart, urlEnd - urlStart);
                        int s = (int)visibleText.size();
                        visibleText.append(visible);
                        InlineRun run;
                        run.startByte = s;
                        run.endByte   = (int)visibleText.size();
                        run.kind      = isImage ? InlineRun::Image : InlineRun::Link;
                        run.url       = url;
                        if (isImage) run.alt = visible;
                        runs.push_back(run);
                        i = urlEnd + 1;
                        continue;
                    }
                }
            }

            visibleText.push_back(c);
            i++;
        }
    }

    // Apply inline-run attributes (bold/italic/code/strike/link/image/footnote) + a scan for
    // known abbreviations to `layout`. Byte offsets in `runs` must refer to `visibleText`.
    // Hit rects are appended to `outHitRects` in layout-local coords.
    void UltraCanvasTextArea::ApplyInlineRunsAndAbbreviations(
            ITextLayout* layout,
            const std::string& visibleText,
            const std::vector<InlineRun>& runs,
            std::vector<MarkdownHitRect>& outHitRects)
    {
        if (!layout) return;
        const Color mdLinkColor(0, 102, 204);
        const Color mdCodeTextColor(200, 50, 50);
        const Color mdCodeBgColor(245, 245, 245);
        const Color mdMathColor(0, 120, 60);

        for (const auto& run : runs) {
            if (run.startByte >= run.endByte) continue;
            switch (run.kind) {
                case InlineRun::Bold: {
                    auto a = TextAttributeFactory::CreateWeight(FontWeight::Bold);
                    a->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(a));
                    break;
                }
                case InlineRun::Italic: {
                    auto a = TextAttributeFactory::CreateStyle(FontSlant::Italic);
                    a->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(a));
                    break;
                }
                case InlineRun::Code: {
                    auto fs = TextAttributeFactory::CreateFontStyle(style.fixedFontStyle);
                    fs->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(fs));
                    auto fg = TextAttributeFactory::CreateForeground(mdCodeTextColor);
                    fg->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(fg));
                    auto bg = TextAttributeFactory::CreateBackground(mdCodeBgColor);
                    bg->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(bg));
                    break;
                }
                case InlineRun::Strike: {
                    auto a = TextAttributeFactory::CreateStrikethrough(true);
                    a->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(a));
                    break;
                }
                case InlineRun::Subscript: {
                    int lh = computedLineHeight > 0
                             ? computedLineHeight
                             : static_cast<int>(style.fontStyle.fontSize * 1.3f);
                    auto scaleAttr = TextAttributeFactory::CreateScale(0.7);
                    scaleAttr->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(scaleAttr));
                    auto riseAttr = TextAttributeFactory::CreateRise(static_cast<int>(-lh * 0.15));
                    riseAttr->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(riseAttr));
                    break;
                }
                case InlineRun::Superscript: {
                    int lh = computedLineHeight > 0
                             ? computedLineHeight
                             : static_cast<int>(style.fontStyle.fontSize * 1.3f);
                    auto scaleAttr = TextAttributeFactory::CreateScale(0.7);
                    scaleAttr->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(scaleAttr));
                    auto riseAttr = TextAttributeFactory::CreateRise(static_cast<int>(lh * 0.25));
                    riseAttr->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(riseAttr));
                    break;
                }
                case InlineRun::Math: {
                    auto italic = TextAttributeFactory::CreateStyle(FontSlant::Italic);
                    italic->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(italic));
                    auto fg = TextAttributeFactory::CreateForeground(mdMathColor);
                    fg->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(fg));
                    break;
                }
                case InlineRun::Link:
                case InlineRun::Image:
                case InlineRun::Footnote: {
                    auto fg = TextAttributeFactory::CreateForeground(mdLinkColor);
                    fg->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(fg));
                    if (run.kind != InlineRun::Footnote) {
                        auto u = TextAttributeFactory::CreateUnderline(UCUnderlineType::UnderlineSingle);
                        u->SetRange(run.startByte, run.endByte);
                        layout->InsertAttribute(std::move(u));
                    } else {
                        int lh = computedLineHeight > 0
                                 ? computedLineHeight
                                 : static_cast<int>(style.fontStyle.fontSize * 1.3f);
                        auto scaleAttr = TextAttributeFactory::CreateScale(0.7);
                        scaleAttr->SetRange(run.startByte, run.endByte);
                        layout->InsertAttribute(std::move(scaleAttr));
                        auto riseAttr = TextAttributeFactory::CreateRise(static_cast<int>(lh * 0.25));
                        riseAttr->SetRange(run.startByte, run.endByte);
                        layout->InsertAttribute(std::move(riseAttr));
                    }
                    Rect2Di startPos = layout->IndexToPos(run.startByte);
                    Rect2Di endPos   = layout->IndexToPos(run.endByte);
                    MarkdownHitRect hit;
                    hit.bounds.x = startPos.x;
                    hit.bounds.y = startPos.y;
                    hit.bounds.width  = std::max(1, endPos.x - startPos.x);
                    hit.bounds.height = std::max(startPos.height, endPos.height);
                    hit.url     = run.url;
                    hit.altText = run.alt;
                    hit.isImage    = (run.kind == InlineRun::Image);
                    hit.isFootnote = (run.kind == InlineRun::Footnote);
                    if (run.kind == InlineRun::Footnote) {
                        auto it = markdownFootnotes.find(run.url);
                        if (it != markdownFootnotes.end()) hit.altText = it->second;
                    }
                    outHitRects.push_back(hit);
                    break;
                }
            }
        }

        // Abbreviation overlay: whole-word, case-sensitive match against markdownAbbreviations.
        for (const auto& [abbr, expansion] : markdownAbbreviations) {
            if (abbr.empty()) continue;
            size_t pos = 0;
            while ((pos = visibleText.find(abbr, pos)) != std::string::npos) {
                bool leftOk  = (pos == 0) ||
                    !std::isalnum(static_cast<unsigned char>(visibleText[pos - 1]));
                bool rightOk = (pos + abbr.size() >= visibleText.size()) ||
                    !std::isalnum(static_cast<unsigned char>(visibleText[pos + abbr.size()]));
                if (leftOk && rightOk) {
                    int startByte = (int)pos;
                    int endByte   = (int)(pos + abbr.size());
                    auto u = TextAttributeFactory::CreateUnderline(UCUnderlineType::UnderlineSingle);
                    u->SetRange(startByte, endByte);
                    layout->InsertAttribute(std::move(u));
                    Rect2Di startPos = layout->IndexToPos(startByte);
                    Rect2Di endPos   = layout->IndexToPos(endByte);
                    MarkdownHitRect hit;
                    hit.bounds.x = startPos.x;
                    hit.bounds.y = startPos.y;
                    hit.bounds.width  = std::max(1, endPos.x - startPos.x);
                    hit.bounds.height = std::max(startPos.height, endPos.height);
                    hit.altText = expansion;
                    hit.isAbbreviation = true;
                    outHitRects.push_back(hit);
                }
                pos += abbr.size();
            }
        }
    }

    // BuildLineLayout removed in Step 8b — the single layout cache is populated by
    // MakeLineLayout via UpdateLineLayouts, which also applies syntax highlighting.

    // RecalculateDisplayLines / GetDisplayLineForCursor / GetDisplayLineCount removed in Step 8b —
    // wrapping and line-range queries go through each ITextLayout's GetLineByteRanges directly,
    // and vertical scroll is pixel-based from lineLayouts[i]->bounds.y.

    void UltraCanvasTextArea::RebuildText() {
        textContent.clear();
        std::string eolSeq = LineEndingSequence(lineEndingType);
        for (size_t i = 0; i < lines.size(); i++) {
            textContent.append(lines[i]);
            if (i < lines.size() - 1) {
                textContent.append(eolSeq);
            }
        }
        InvalidateGraphemeCache();
        markdownIndexDirty = true;
        isNeedRecalculateVisibleArea = true;
        RequestRedraw();

        if (onTextChanged) {
            onTextChanged(textContent);
        }
    }

    int UltraCanvasTextArea::MeasureTextWidth(const std::string& txt) const {
        auto context = GetRenderContext();
        if (!context || txt.empty()) return 0;

        context->PushState();
        context->SetFontStyle(style.fontStyle);
        context->SetFontWeight(FontWeight::Normal);
        int width = context->GetTextLineWidth(txt);
        context->PopState();
        return width;
    }



    bool UltraCanvasTextArea::IsNeedVerticalScrollbar() {
        if (editingMode == TextAreaEditingMode::Hex) {
            return hexTotalRows > hexMaxVisibleRows;
        }
        // Total content height > viewport height.
        int contentHeight = 0;
        if (!lineLayouts.empty() && lineLayouts.back()) {
            contentHeight = lineLayouts.back()->bounds.y + lineLayouts.back()->bounds.height;
        } else {
            contentHeight = (int)lines.size() * std::max(1, computedLineHeight);
        }
        return contentHeight > visibleTextArea.height;
    }

    bool UltraCanvasTextArea::IsNeedHorizontalScrollbar() {
        if (editingMode == TextAreaEditingMode::Hex) return false;
        if (wordWrap) return false;
        return maxLineWidth > visibleTextArea.width;
    }

    int UltraCanvasTextArea::GetMaxLineWidth() {
        maxLineWidth = 0;
        for (const auto& line : lines) {
            maxLineWidth = std::max(maxLineWidth, MeasureTextWidth(line));
        }
        return maxLineWidth;
    }

    const TokenStyle& UltraCanvasTextArea::GetStyleForTokenType(TokenType type) const {
        switch (type) {
            case TokenType::Keyword: return style.tokenStyles.keywordStyle;
            case TokenType::Function: return style.tokenStyles.functionStyle;
            case TokenType::String: return style.tokenStyles.stringStyle;
            case TokenType::Character: return style.tokenStyles.characterStyle;
            case TokenType::Comment: return style.tokenStyles.commentStyle;
            case TokenType::Number: return style.tokenStyles.numberStyle;
            case TokenType::Identifier: return style.tokenStyles.identifierStyle;
            case TokenType::Operator: return style.tokenStyles.operatorStyle;
            case TokenType::Constant: return style.tokenStyles.constantStyle;
            case TokenType::Preprocessor: return style.tokenStyles.preprocessorStyle;
            case TokenType::Punctuation: return style.tokenStyles.punctuationStyle;
            case TokenType::Builtin: return style.tokenStyles.builtinStyle;
            case TokenType::Assembly: return style.tokenStyles.assemblyStyle;
            case TokenType::Register: return style.tokenStyles.registerStyle;
            default: return style.tokenStyles.defaultStyle;
        }
    }

    void UltraCanvasTextArea::Invalidate() {
        isNeedRecalculateVisibleArea = true;
        RequestRedraw();
    }

// ===== SCROLLING =====

    void UltraCanvasTextArea::ScrollTo(int line) {
        // Pixel scroll: find the content Y of the requested line from the layout cache.
        if (line < 0) line = 0;
        int targetY = 0;
        auto lineLayout = GetActualLineLayout(line);
        if (lineLayout) {
            targetY = lineLayout->bounds.y;
        }
        verticalScrollOffset = std::max(0, targetY);
        RequestRedraw();
    }

    void UltraCanvasTextArea::ScrollUp(int lineCount) {
        int h = static_cast<int>(style.fontStyle.fontSize * 1.3f);
        verticalScrollOffset = std::max(0, verticalScrollOffset - lineCount * h);
        RequestRedraw();
    }

    void UltraCanvasTextArea::ScrollDown(int lineCount) {
        int h = static_cast<int>(style.fontStyle.fontSize * 1.3f);
        verticalScrollOffset += lineCount * h;
        if (verticalScrollOffset < 0) verticalScrollOffset = 0;
        RequestRedraw();
    }

    void UltraCanvasTextArea::ScrollLeft(int chars) {
        if (wordWrap) return; // No horizontal scrolling when word wrap is on
        horizontalScrollOffset = std::max(0, horizontalScrollOffset - chars * 10);
        RequestRedraw();
    }

    void UltraCanvasTextArea::ScrollRight(int chars) {
        if (wordWrap) return; // No horizontal scrolling when word wrap is on
        int maxOffset = std::max(0, maxLineWidth - visibleTextArea.width);
        horizontalScrollOffset = std::min(maxOffset, horizontalScrollOffset + chars * 10);
        RequestRedraw();
    }

    void UltraCanvasTextArea::SetFirstVisibleLine(int line) {
        // Public API: pixel-scroll to the given logical line's top.
        ScrollTo(line);
    }
// ===== UNDO/REDO =====

    void UltraCanvasTextArea::SaveState() {
        TextState state;
        state.text           = textContent;
        state.cursorPosition = cursorPosition;
        state.selectionStart = selectionStart;
        state.selectionEnd   = selectionEnd;

        undoStack.push_back(state);
        if (undoStack.size() > maxUndoStackSize) {
            undoStack.erase(undoStack.begin());
        }
        redoStack.clear();
    }

    void UltraCanvasTextArea::Undo() {
        if (editingMode == TextAreaEditingMode::Hex) { HexUndo(); return; }
        if (undoStack.empty()) return;

        TextState currentState;
        currentState.text           = textContent;
        currentState.cursorPosition = cursorPosition;
        currentState.selectionStart = selectionStart;
        currentState.selectionEnd   = selectionEnd;
        redoStack.push_back(currentState);

        TextState previousState = undoStack.back();
        undoStack.pop_back();

        SetText(previousState.text);
        cursorPosition   = previousState.cursorPosition;
        selectionStart   = previousState.selectionStart;
        selectionEnd     = previousState.selectionEnd;
        RequestRedraw();
    }

    void UltraCanvasTextArea::Redo() {
        if (editingMode == TextAreaEditingMode::Hex) { HexRedo(); return; }
        if (redoStack.empty()) return;

        TextState currentState;
        currentState.text           = textContent;
        currentState.cursorPosition = cursorPosition;
        currentState.selectionStart = selectionStart;
        currentState.selectionEnd   = selectionEnd;
        undoStack.push_back(currentState);

        TextState nextState = redoStack.back();
        redoStack.pop_back();

        SetText(nextState.text);
        cursorPosition   = nextState.cursorPosition;
        selectionStart   = nextState.selectionStart;
        selectionEnd     = nextState.selectionEnd;
        RequestRedraw();
    }

    bool UltraCanvasTextArea::CanUndo() const {
        if (editingMode == TextAreaEditingMode::Hex) return !hexUndoStack.empty();
        return !undoStack.empty();
    }
    bool UltraCanvasTextArea::CanRedo() const {
        if (editingMode == TextAreaEditingMode::Hex) return !hexRedoStack.empty();
        return !redoStack.empty();
    }

// ===== SYNTAX HIGHLIGHTING =====

    void UltraCanvasTextArea::SetHighlightSyntax(bool on) {
        style.highlightSyntax = on;
        if (on && !syntaxTokenizer) {
            syntaxTokenizer = std::make_unique<SyntaxTokenizer>();
        }
        RequestRedraw();
    }

    void UltraCanvasTextArea::SetProgrammingLanguage(const std::string& language) {
        if (style.highlightSyntax) {
            if (!syntaxTokenizer) {
                syntaxTokenizer = std::make_unique<SyntaxTokenizer>();
            }
            syntaxTokenizer->SetLanguage(language);
            RequestRedraw();
        }
    }

    bool UltraCanvasTextArea::SetProgrammingLanguageByExtension(const std::string& extension) {
        if (!syntaxTokenizer) {
            syntaxTokenizer = std::make_unique<SyntaxTokenizer>();
        }
        auto result = syntaxTokenizer->SetLanguageByExtension(extension);
        if (style.highlightSyntax) {
            RequestRedraw();
        }
        return result;
    }

    const std::string UltraCanvasTextArea::GetCurrentProgrammingLanguage() {
        if (!syntaxTokenizer) return "Plain text";
        return syntaxTokenizer->GetCurrentProgrammingLanguage();
    }

    std::vector<std::string> UltraCanvasTextArea::GetSupportedLanguages() {
        if (!syntaxTokenizer) {
            syntaxTokenizer = std::make_unique<SyntaxTokenizer>();
        }
        return syntaxTokenizer->GetSupportedLanguages();
    }

    void UltraCanvasTextArea::SetSyntaxTheme(const std::string& theme) {
        // Theme application would go here
        RequestRedraw();
    }

// ===== THEMES =====

    void UltraCanvasTextArea::ApplyDarkTheme() {
        style.backgroundColor = {30, 30, 30, 255};
        style.fontColor = {210, 210, 210, 255};
        style.currentLineColor = {60, 60, 60, 255};
        style.lineNumbersColor = {80, 80, 80, 255};           // Dimmer — less visual noise in dark mode
        style.lineNumbersBackgroundColor = {35, 35, 35, 255}; // Dark gutter background
        style.selectionColor = {60, 90, 150, 100};
        style.cursorColor = {255, 255, 255, 255};

        style.tokenStyles.keywordStyle.color = {0x4c, 0xbb, 0xc9, 255};
        style.tokenStyles.functionStyle.color = {0xdc, 0xd6, 0xa2, 255};
        style.tokenStyles.stringStyle.color = {0xce, 0x91, 0x78, 255};
        style.tokenStyles.characterStyle.color = {0xce, 0x91, 0x78, 255};
        style.tokenStyles.commentStyle.color = {0x68, 0x86, 0x42, 255};
        style.tokenStyles.numberStyle.color = {0xa2, 0xcc, 0x9d, 255};
        style.tokenStyles.identifierStyle.color = {0xdc, 0xd6, 0xa2, 255};
        style.tokenStyles.operatorStyle.color = {0xce, 0xbd, 0x88, 255};
        style.tokenStyles.constantStyle.color = {0xa2, 0xcc, 0x9d, 255};
        style.tokenStyles.preprocessorStyle.color = {0xb5, 0x89, 0xbd, 255};
        style.tokenStyles.builtinStyle.color = {0x4c, 0xbb, 0xc9, 255};
        style.tokenStyles.defaultStyle.color = {210, 210, 210, 255};

        RequestRedraw();
    }
    void UltraCanvasTextArea::ApplyLightTheme() {
        style.fontColor = {0, 0, 0, 255};
        style.backgroundColor = {255, 255, 255, 255};
        style.borderColor = {200, 200, 200, 255};
        style.selectionColor = {51, 153, 255, 100};
        style.cursorColor = {0, 0, 0, 255};
        style.currentLineColor = {240, 240, 240, 255};
        style.lineNumbersColor = {128, 128, 128, 255};
        style.lineNumbersBackgroundColor = {248, 248, 248, 255};
        style.currentLineHighlightColor = {255, 255, 0, 30};
        style.scrollbarTrackColor = {128, 128, 128, 255};
        style.scrollbarColor = {200, 200, 200, 255};

        // Syntax highlighting colors
        style.tokenStyles.keywordStyle.color = {0, 0, 255, 255};
        style.tokenStyles.functionStyle.color = {128, 0, 128, 255};
        style.tokenStyles.stringStyle.color = {0, 128, 0, 255};
        style.tokenStyles.characterStyle.color = {0, 128, 0, 255};
        style.tokenStyles.commentStyle.color = {128, 128, 128, 255};
        style.tokenStyles.numberStyle.color = {255, 128, 0, 255};
        style.tokenStyles.identifierStyle.color = {0, 128, 128, 255};
        style.tokenStyles.operatorStyle.color = {128, 0, 0, 255};
        style.tokenStyles.constantStyle.color = {0, 0, 128, 255};
        style.tokenStyles.preprocessorStyle.color = {64, 128, 128, 255};
        style.tokenStyles.builtinStyle.color = {128, 0, 255, 255};
    }

    void UltraCanvasTextArea::ApplyCustomTheme(const TextAreaStyle& customStyle) {
        style = customStyle;
        RequestRedraw();
    }

    void UltraCanvasTextArea::ApplyCodeStyle(const std::string& language) {
        SetHighlightSyntax(true);
        SetShowLineNumbers(true);
        SetHighlightCurrentLine(true);
        SetProgrammingLanguage(language);
    }

    void UltraCanvasTextArea::ApplyDarkCodeStyle(const std::string& language) {
        ApplyDarkTheme();
        ApplyCodeStyle(language);
    }

    void UltraCanvasTextArea::ApplyPlainTextStyle() {
        SetHighlightSyntax(false);
        SetShowLineNumbers(false);
        SetHighlightCurrentLine(false);
    }

// ===== SEARCH =====

    void UltraCanvasTextArea::SetTextToFind(const std::string& searchText, bool caseSensitive) {
        lastSearchText = searchText;
        lastSearchCaseSensitive = caseSensitive;
        if (editingMode == TextAreaEditingMode::Hex) {
            lastSearchPosition = hexCursorByteOffset;
        } else {
            lastSearchPosition = GetPositionFromLineColumn(cursorPosition.lineIndex, cursorPosition.columnIndex);
        }
    }

    void UltraCanvasTextArea::FindFirst() {
        lastSearchPosition = -1;
        FindNext();
    }

    void UltraCanvasTextArea::FindNext() {
        if (lastSearchText.empty()) return;

        if (editingMode == TextAreaEditingMode::Hex) {
            std::string haystack(hexBuffer.begin(), hexBuffer.end());
            std::string needle = lastSearchText;
            if (!lastSearchCaseSensitive) {
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            }
            size_t startFrom = (lastSearchPosition >= 0) ? lastSearchPosition + 1 : 0;
            size_t found = haystack.find(needle, startFrom);
            if (found == std::string::npos && lastSearchPosition > 0) {
                found = haystack.find(needle, 0);
            }
            if (found != std::string::npos) {
                int pos = static_cast<int>(found);
                int len = static_cast<int>(lastSearchText.size());
                hexSelectionStart = pos;
                hexSelectionEnd = pos + len;
                hexCursorByteOffset = pos;
                lastSearchPosition = pos;
                HexEnsureCursorVisible();
                RequestRedraw();
            }
            return;
        }

        int foundPos = utf8_find(textContent, lastSearchText, lastSearchPosition + 1, lastSearchCaseSensitive);

        if (foundPos < 0 && lastSearchPosition > 0) {
            foundPos = utf8_find(textContent, lastSearchText, 0, lastSearchCaseSensitive);
        }

        if (foundPos >= 0) {
            int searchLen = utf8_length(lastSearchText);
            auto [sLine, sCol] = GetLineColumnFromPosition(foundPos);
            auto [eLine, eCol] = GetLineColumnFromPosition(foundPos + searchLen);
            selectionStart = {sLine, sCol};
            selectionEnd   = {eLine, eCol};
            cursorPosition = selectionEnd;
            lastSearchPosition = foundPos;
            EnsureCursorVisible();
            RequestRedraw();
        }
    }

    void UltraCanvasTextArea::FindPrevious() {
        if (lastSearchText.empty()) return;

        if (editingMode == TextAreaEditingMode::Hex) {
            std::string haystack(hexBuffer.begin(), hexBuffer.end());
            std::string needle = lastSearchText;
            if (!lastSearchCaseSensitive) {
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            }
            size_t found = std::string::npos;
            if (lastSearchPosition > 0) {
                found = haystack.rfind(needle, lastSearchPosition - 1);
            }
            if (found == std::string::npos) {
                found = haystack.rfind(needle);
                if (found != std::string::npos && static_cast<int>(found) == lastSearchPosition) {
                    return; // Only one match, already selected
                }
            }
            if (found != std::string::npos) {
                int pos = static_cast<int>(found);
                int len = static_cast<int>(lastSearchText.size());
                hexSelectionStart = pos;
                hexSelectionEnd = pos + len;
                hexCursorByteOffset = pos;
                lastSearchPosition = pos;
                HexEnsureCursorVisible();
                RequestRedraw();
            }
            return;
        }

        int foundPos = -1;

        // Search backwards from position BEFORE the current match start
        if (lastSearchPosition > 0) {
            foundPos = utf8_rfind(textContent, lastSearchText, lastSearchPosition - 1, lastSearchCaseSensitive);
        }

        // Wrap around to end of document if nothing found before current position
        if (foundPos < 0) {
            foundPos = utf8_rfind(textContent, lastSearchText, -1, lastSearchCaseSensitive);

            // Don't accept if it's the same position we started from (no other match exists)
            if (foundPos >= 0 && foundPos == lastSearchPosition) {
                return; // Only one match in document, already selected
            }
        }

        if (foundPos >= 0) {
            int searchLen = utf8_length(lastSearchText);
            auto [sLine, sCol] = GetLineColumnFromPosition(foundPos);
            auto [eLine, eCol] = GetLineColumnFromPosition(foundPos + searchLen);
            selectionStart = {sLine, sCol};
            selectionEnd   = {eLine, eCol};
            cursorPosition = selectionStart;
            lastSearchPosition = foundPos;
            EnsureCursorVisible();
            RequestRedraw();
        }
    }

    void UltraCanvasTextArea::ReplaceText(const std::string& findText, const std::string& replaceText, bool all) {
        if (findText.empty()) return;

        SaveState();
        int findLen = utf8_length(findText);
        int replaceLen = utf8_length(replaceText);

        if (all) {
            int pos = 0;
            while ((pos = utf8_find(textContent, findText, pos, lastSearchCaseSensitive)) >= 0) {
                utf8_replace(textContent, pos, findLen, replaceText);
                pos += replaceLen;
            }
            SetText(textContent);
        } else {
            if (HasSelection()) {
                std::string selected = GetSelectedText();
                // Case-insensitive comparison for single replace
                bool match;
                if (lastSearchCaseSensitive) {
                    match = (selected == findText);
                } else {
                    gchar* lSel = g_utf8_strdown(selected.c_str(), -1);
                    gchar* lFind = g_utf8_strdown(findText.c_str(), -1);
                    match = (strcmp(lSel, lFind) == 0);
                    g_free(lSel);
                    g_free(lFind);
                }
                if (match) {
                    DeleteSelection();
                    InsertText(replaceText);
                }
            }
            FindNext();
        }
    }

    void UltraCanvasTextArea::HighlightMatches(const std::string& searchText) {
        searchHighlights.clear();
        if (searchText.empty()) {
            RequestRedraw();
            return;
        }

        if (editingMode == TextAreaEditingMode::Hex) {
            std::string haystack(hexBuffer.begin(), hexBuffer.end());
            std::string needle = searchText;
            if (!lastSearchCaseSensitive) {
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            }
            int searchLen = static_cast<int>(searchText.size());
            size_t pos = 0;
            while ((pos = haystack.find(needle, pos)) != std::string::npos) {
                searchHighlights.push_back({static_cast<int>(pos), static_cast<int>(pos) + searchLen});
                pos += searchLen;
            }
            RequestRedraw();
            return;
        }

        int searchLen = utf8_length(searchText);
        int pos = 0;
        while ((pos = utf8_find(textContent, searchText, pos, lastSearchCaseSensitive)) >= 0) {
            searchHighlights.push_back({pos, pos + searchLen});
            pos += searchLen;
        }
        RequestRedraw();
    }

    void UltraCanvasTextArea::ClearHighlights() {
        searchHighlights.clear();
        RequestRedraw();
    }

// ===== BOOKMARKS =====

    void UltraCanvasTextArea::ToggleBookmark(int lineIndex) {
        auto it = std::find(bookmarks.begin(), bookmarks.end(), lineIndex);
        if (it != bookmarks.end()) {
            bookmarks.erase(it);
        } else {
            bookmarks.push_back(lineIndex);
            std::sort(bookmarks.begin(), bookmarks.end());
        }
        RequestRedraw();
    }

    void UltraCanvasTextArea::NextBookmark() {
        if (bookmarks.empty()) return;
        for (int bm : bookmarks) {
            if (bm > cursorPosition.lineIndex) {
                GoToLine(bm + 1);
                return;
            }
        }
        GoToLine(bookmarks[0] + 1);
    }

    void UltraCanvasTextArea::PreviousBookmark() {
        if (bookmarks.empty()) return;
        for (auto it = bookmarks.rbegin(); it != bookmarks.rend(); ++it) {
            if (*it < cursorPosition.lineIndex) {
                GoToLine(*it + 1);
                return;
            }
        }
        GoToLine(bookmarks.back() + 1);
    }

    void UltraCanvasTextArea::ClearAllBookmarks() {
        bookmarks.clear();
        RequestRedraw();
    }

// ===== MARKERS =====

    void UltraCanvasTextArea::AddErrorMarker(int lineIndex, const std::string& message) {
        markers.push_back({Marker::Error, lineIndex, message});
        RequestRedraw();
    }

    void UltraCanvasTextArea::AddWarningMarker(int lineIndex, const std::string& message) {
        markers.push_back({Marker::Warning, lineIndex, message});
        RequestRedraw();
    }

    void UltraCanvasTextArea::ClearMarkers() {
        markers.clear();
        RequestRedraw();
    }

// ===== AUTO-COMPLETE =====

    void UltraCanvasTextArea::ShowAutoComplete(const std::vector<std::string>& suggestions) {
        // Implementation placeholder
    }

    void UltraCanvasTextArea::HideAutoComplete() {
        // Implementation placeholder
    }

    void UltraCanvasTextArea::AcceptAutoComplete() {
        // Implementation placeholder
    }

// ===== BRACKET MATCHING =====

    void UltraCanvasTextArea::HighlightMatchingBrackets() {
        // Implementation placeholder
    }

    void UltraCanvasTextArea::JumpToMatchingBracket() {
        // Implementation placeholder
    }

// ===== INDENTATION =====

    void UltraCanvasTextArea::IndentSelection() {
        if (!HasSelection()) return;

        LineColumnIndex a = selectionStart, b = selectionEnd;
        if (a.lineIndex > b.lineIndex ||
            (a.lineIndex == b.lineIndex && a.columnIndex > b.columnIndex)) std::swap(a, b);
        int startLine = a.lineIndex, endLine = b.lineIndex;

        SaveState();
        std::string indent(tabSize, ' ');
        for (int i = startLine; i <= endLine; i++) {
            lines[i].insert(0, indent);
            InvalidateLineLayout(i);
        }
        InvalidateGraphemeCache();
        RebuildText();
    }

    void UltraCanvasTextArea::UnindentSelection() {
        if (!HasSelection()) return;

        LineColumnIndex a = selectionStart, b = selectionEnd;
        if (a.lineIndex > b.lineIndex ||
            (a.lineIndex == b.lineIndex && a.columnIndex > b.columnIndex)) std::swap(a, b);
        int startLine = a.lineIndex, endLine = b.lineIndex;

        SaveState();
        
        for (int i = startLine; i <= endLine; i++) {
            int spacesToRemove = 0;
            for (int j = 0; j < tabSize && j < utf8_length(lines[i]); j++) {
                std::string ch = utf8_char_at(lines[i], j);
                if (ch == " " || ch == "\t") {
                    spacesToRemove++;
                } else {
                    break;
                }
            }
            if (spacesToRemove > 0) {
                // Leading whitespace is ASCII, so byte erase at 0 is safe
                lines[i].erase(0, spacesToRemove);
                InvalidateLineLayout(i);
            }
        }

        InvalidateGraphemeCache();
        RebuildText();
    }

    int UltraCanvasTextArea::CalculateLineNumbersWidth(IRenderContext* ctx) {
        if (!style.showLineNumbers) return 0;

        // Calculate the maximum line number we need to display:
        // current line count + 50 lines of headroom so the gutter doesn't
        // constantly resize as the user types near the threshold
        int maxLineNumber = static_cast<int>(lines.size()) + 50;

        // Count digits needed
        int digits = 1;
        int temp = maxLineNumber;
        while (temp >= 10) {
            digits++;
            temp /= 10;
        }

        // Minimum 2 digits (lines 1-99 should not produce a tiny gutter)
        digits = std::max(digits, 2);

        // Measure the width of the widest digit string at current font
        // Use '9' repeated since it's typically the widest digit
        std::string maxNumberStr(digits, '9');

        ctx->PushState();
        ctx->SetFontStyle(style.fontStyle);
        ctx->SetFontWeight(FontWeight::Normal);
        int textWidth = ctx->GetTextLineWidth(maxNumberStr);
        ctx->PopState();

        // Add padding: left margin + right margin before the separator line
        int padding = 8; // 4px left + 4px right
        return textWidth + padding;
    }

    void UltraCanvasTextArea::AutoIndentLine(int lineIndex) {
        // Implementation placeholder
    }

// ===== COMMENTS =====

    void UltraCanvasTextArea::ToggleLineComment() {
        // Implementation placeholder
    }

    void UltraCanvasTextArea::ToggleBlockComment() {
        // Implementation placeholder
    }

    int UltraCanvasTextArea::CountMatches(const std::string& searchText, bool caseSensitive) const {
        if (searchText.empty()) return 0;

        if (editingMode == TextAreaEditingMode::Hex) {
            std::string haystack(hexBuffer.begin(), hexBuffer.end());
            std::string needle = searchText;
            if (!caseSensitive) {
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            }
            int count = 0;
            size_t pos = 0;
            while ((pos = haystack.find(needle, pos)) != std::string::npos) {
                count++;
                pos += needle.size();
            }
            return count;
        }

        int count = 0;
        int pos = 0;
        int searchLen = utf8_length(searchText);

        while ((pos = utf8_find(textContent, searchText, pos, caseSensitive)) >= 0) {
            count++;
            pos += searchLen;
        }
        return count;
    }

    int UltraCanvasTextArea::GetCurrentMatchIndex(const std::string& searchText, bool caseSensitive) const {
        if (searchText.empty()) return 0;

        if (editingMode == TextAreaEditingMode::Hex) {
            if (hexSelectionStart < 0 || hexSelectionEnd < 0) return 0;
            int currentPos = std::min(hexSelectionStart, hexSelectionEnd);

            std::string haystack(hexBuffer.begin(), hexBuffer.end());
            std::string needle = searchText;
            if (!caseSensitive) {
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            }
            int index = 0;
            size_t pos = 0;
            while ((pos = haystack.find(needle, pos)) != std::string::npos) {
                index++;
                if (static_cast<int>(pos) == currentPos) return index;
                pos += needle.size();
            }
            return 0;
        }

        if (!HasSelection()) return 0;

        // The current match is the one at the lesser selection endpoint.
        int currentPos = GetSelectionMinGrapheme();
        if (currentPos < 0) currentPos = 0;

        int index = 0;
        int pos = 0;
        int searchLen = utf8_length(searchText);

        while ((pos = utf8_find(textContent, searchText, pos, caseSensitive)) >= 0) {
            index++;
            if (pos == currentPos) {
                return index;
            }
            pos += searchLen;
        }
        return 0; // Current selection doesn't match any occurrence
    }

// ===== EDITING MODE SWITCHING =====

    void UltraCanvasTextArea::SetEditingMode(TextAreaEditingMode mode) {
        if (editingMode == mode) return;

        TextAreaEditingMode oldMode = editingMode;

        // Leaving hex mode: convert buffer back to text
        if (oldMode == TextAreaEditingMode::Hex && mode != TextAreaEditingMode::Hex) {
            textContent = std::string(hexBuffer.begin(), hexBuffer.end());
            lineEndingType = DetectLineEnding(textContent);
            lines = utf8_split_lines(textContent);
            if (lines.empty()) lines.push_back(std::string());

            cursorPosition = {0, 0};
            selectionStart = LineColumnIndex::INVALID;
            selectionEnd   = LineColumnIndex::INVALID;
            hexUndoStack.clear();
            hexRedoStack.clear();
        }

        // Entering hex mode: convert text to buffer
        if (mode == TextAreaEditingMode::Hex && oldMode != TextAreaEditingMode::Hex) {
            hexBuffer.assign(textContent.begin(), textContent.end());
            hexCursorByteOffset = 0;
            hexCursorInAsciiPanel = false;
            hexCursorNibble = 0;
            hexSelectionStart = -1;
            hexSelectionEnd = -1;
            hexFirstVisibleRow = 0;
            hexUndoStack.clear();
            hexRedoStack.clear();
        }

        // Entering markdown mode: set up syntax highlighting for raw markdown
        if (mode == TextAreaEditingMode::MarkdownHybrid && oldMode != TextAreaEditingMode::MarkdownHybrid) {
            SetHighlightSyntax(true);
            if (syntaxTokenizer) {
                syntaxTokenizer->SetLanguage("Markdown");
            }
        }

        editingMode = mode;
        isNeedRecalculateVisibleArea = true;
        RequestRedraw();
    }

    // Line Layouts — all coordinates in these helpers are SCREEN coords (relative to the root
    // canvas), not widget-local or content-local. The translation chain is:
    //   screen.x = visibleTextArea.x + lineLayout.bounds.x + lineLayout.layoutShift.x
    //   screen.y = visibleTextArea.y + lineLayout.bounds.y + lineLayout.layoutShift.y
    //              - verticalScrollOffset
    // Real-line codepoints translate to layout-text bytes via:
    //   layoutCp = columnIndex - layoutTextStartIndex  (clamped to [0, layout-cp-count])
    //   layoutByte = utf8_cp_to_byte(layout.GetText(), layoutCp)
    LineLayoutBase *UltraCanvasTextArea::GetLineLayoutForPos(const Point2Di& pos) {
        int contentY = pos.y - visibleTextArea.y + verticalScrollOffset;
        for (auto const& ll : lineLayouts) {
            if (!ll) continue;
            if (ll->bounds.y > contentY) break;
            if (contentY < ll->bounds.y + ll->bounds.height) return ll.get();
        }
        return nullptr;
    }

    Rect2Di UltraCanvasTextArea::LineColumnToCursorPos(const LineColumnIndex& idx) {
        if (!idx.IsValid()) {
            return Rect2Di::INVALID;
        }
        LineLayoutBase* line = GetActualLineLayout(idx.lineIndex);

        // make plain line for formatted line as plain line will be edited then
        std::unique_ptr<LineLayoutBase> tmplPLainLineLayoutPtr;
        if (line->layoutType != LineLayoutType::PlainLine) {
            tmplPLainLineLayoutPtr = MakePlainLineLayout(GetRenderContext(), idx.lineIndex);
            tmplPLainLineLayoutPtr->bounds.x = line->bounds.x;
            tmplPLainLineLayoutPtr->bounds.y = line->bounds.y;
            line = tmplPLainLineLayoutPtr.get();
        }

        if (!line || !line->layout) return Rect2Di::INVALID;

        std::string layoutText = line->layout->GetText();
        int layoutCpCount = utf8_length(layoutText);
        int layoutCol = std::max(0, std::min(idx.columnIndex - line->layoutTextStartIndex,
                                             layoutCpCount));
        int byteIndex = static_cast<int>(utf8_cp_to_byte(layoutText, layoutCol));

        Rect2Di cursorRect = line->layout->GetCursorPos(byteIndex).strongPos;
        cursorRect.x += visibleTextArea.x + line->bounds.x + line->layoutShift.x
                        - horizontalScrollOffset;
        cursorRect.y += visibleTextArea.y + line->bounds.y + line->layoutShift.y
                        - verticalScrollOffset;
        return cursorRect;
    }

    LineColumnIndex UltraCanvasTextArea::PosToLineColumn(const Point2Di& pos) {
        // Convert screen → content coords (pre-scroll, relative to visibleTextArea top-left).
        int contentY = pos.y - visibleTextArea.y + verticalScrollOffset;
        int contentX = pos.x - visibleTextArea.x + horizontalScrollOffset;
        for (int i = 0; i < (int)lineLayouts.size(); i++) {
            auto* ll = GetActualLineLayout(i);
            if (!ll) continue;
            if (ll->bounds.y > contentY) {
                // Clicked above this line but past all prior lines → snap to start of this line.
                return {i, ll->layoutTextStartIndex};
            }
            if (contentY >= ll->bounds.y + ll->bounds.height) continue;
            if (!ll->layout) return {i, ll->layoutTextStartIndex};

            // make plain line for formatted line as plain line will be edited then
            std::unique_ptr<LineLayoutBase> lineLayoutPtr;
            if (ll->layoutType != LineLayoutType::PlainLine) {
                lineLayoutPtr = MakePlainLineLayout(GetRenderContext(), i);
                lineLayoutPtr->bounds.y = ll->bounds.y;
                lineLayoutPtr->bounds.x = ll->bounds.x;
                ll = lineLayoutPtr.get();
            }
            // Translate to layout-local coords.
            int layoutX = contentX - (ll->bounds.x + ll->layoutShift.x);
            int layoutY = contentY - (ll->bounds.y + ll->layoutShift.y);
            auto hit = ll->layout->XYToIndex(std::max(0, layoutX), std::max(0, layoutY));

            std::string layoutText = ll->layout->GetText();
            int layoutCp = utf8_byte_to_cp(layoutText, hit.index) + hit.trailing;
            return {i, layoutCp + ll->layoutTextStartIndex};
        }
        // Past the last line — land at the end of the document.
        if (!lineLayouts.empty() && lineLayouts.back()) {
            int last = (int)lineLayouts.size() - 1;
            return {last, utf8_length(lines[last])};
        }
        return LineColumnIndex::INVALID;
    }

    LineLayoutBase* UltraCanvasTextArea::GetActualLineLayout(int idx) {
        LineLayoutBase* line;
        if (currentLine && cursorPosition.lineIndex == idx) {
            return currentLine.get();
        }
        if (idx >= 0 && idx <= (int)lineLayouts.size()) {
            return lineLayouts[idx].get();
        }
        return nullptr;
    }

    void UltraCanvasTextArea::UpdateLineLayouts(IRenderContext* ctx) {
        // Refresh the markdown-wide index (abbreviations / footnotes / anchors / backlinks)
        // before line layouts query it. Lazy — only reruns when content changed.
        if (editingMode == TextAreaEditingMode::MarkdownHybrid && markdownIndexDirty) {
            RebuildMarkdownIndex();
            // Index shifts may change which headings show the ↩ icon or which abbreviations
            // apply; throw all MD layouts including the edit-mode stash.
            for (auto& ll : lineLayouts) ll.reset();
        }

        ReconcileLayoutState();

        // If the wrap width changed (resize, word-wrap toggle), every cached layout's line-break
        // positions and heights are stale. Invalidate the whole cache before rebuilding.
        int currentWrapWidth = wordWrap ? visibleTextArea.width : 0;
        if (currentWrapWidth != lineLayoutsWrapWidth) {
            for (auto& ll : lineLayouts) ll.reset();
            lineLayoutsWrapWidth = currentWrapWidth;
        }

        lineLayouts.resize(lines.size());

        // Pass 1: build any missing line layouts. Track contiguous table row spans so we can
        // normalize their column widths in a second pass (each table's cell widths depend on
        // the widest content across all its rows, not just one line).
        //
        // Grouping uses the RAW line text (trimmed leading `|`) rather than the cached layout
        // type. Under MarkdownHybrid, the cursor line is built as PlainLine (raw-edit mode),
        // which would otherwise split a table into two groups — one above and one below the
        // cursor — and they'd negotiate different column widths. Using raw text keeps the
        // table contiguous; NormalizeTableGroupWidths skips rows that aren't TableLineLayout.
        std::vector<std::pair<int,int>> tableSpans;   // inclusive [start, end] line indices
        int groupStart = -1;
        auto isTableRowByText = [&](int idx) -> bool {
            if (idx < 0 || idx >= (int)lines.size()) return false;
            std::string t = TrimWhitespace(lines[idx]);
            return !t.empty() && t[0] == '|';
        };

        for(int i = 0; i < (int)lines.size(); i++) {
            if (!lineLayouts[i]) {
                lineLayouts[i] = MakeLineLayout(ctx, i);
            }
            bool inTable = isTableRowByText(i);
            if (inTable) {
                if (groupStart < 0) groupStart = i;
            } else if (groupStart >= 0) {
                tableSpans.push_back({groupStart, i - 1});
                groupStart = -1;
            }
        }

        if (cursorPosition.lineIndex >= 0) {
            currentLine = MakePlainLineLayout(ctx, cursorPosition.lineIndex);
        } else {
            currentLine.reset();
        }

        if (groupStart >= 0) tableSpans.push_back({groupStart, (int)lines.size() - 1});

        // Pass 2: normalize per-table column widths. Updates each cell's layout wrap width,
        // bounds.x / width, and the row's bounds.height (so Pass 3 sees correct heights).
        for (auto [s, e] : tableSpans) {
            NormalizeTableGroupWidths(s, e);
        }

        // Pass 3: cascade cumulative Y positions. Must run after table heights are finalized.
        int prevLineBottomPos = 0;
        for (int i = 0; i < (int)lineLayouts.size(); i++) {
            if (!lineLayouts[i]) continue;

            lineLayouts[i]->bounds.y = prevLineBottomPos;
            if (cursorPosition.lineIndex == i) {
                currentLine->bounds.y = prevLineBottomPos;
                prevLineBottomPos += currentLine->bounds.height;
            } else {
                prevLineBottomPos += lineLayouts[i]->bounds.height;
            }
        }
    }

    // Normalize column widths across every row of a single table. Measures each non-separator
    // cell's natural (unwrapped) width, picks per-column max, then either distributes extra
    // space evenly if the natural total fits, or proportionally shrinks if it doesn't — both
    // paths absorb any rounding delta into the last column so the row width matches the viewport
    // exactly. Per-column alignment from the separator row is copied + applied to every row.
    void UltraCanvasTextArea::NormalizeTableGroupWidths(int startLine, int endLine) {
        constexpr int cellPadding = 4;
        constexpr int rowVerticalPad = 4;
        const int availWidth = std::max(100, visibleTextArea.width);

        int colCount = 0;
        for (int i = startLine; i <= endLine; i++) {
            auto* tbl = dynamic_cast<TableLineLayout*>(lineLayouts[i].get());
            if (tbl) colCount = std::max(colCount, tbl->tableColumnCount);
        }
        if (colCount == 0) return;

        // Locate the separator row (if any) and pull its per-column alignment markers. Extend to
        // colCount with Left as the fallback if the separator had fewer cells than the widest row.
        std::vector<TextAlignment> alignments(colCount, TextAlignment::Left);
        for (int i = startLine; i <= endLine; i++) {
            auto* tbl = dynamic_cast<TableLineLayout*>(lineLayouts[i].get());
            if (tbl && tbl->layoutType == LineLayoutType::TableSeparatorRow) {
                for (int c = 0; c < (int)tbl->columnAlignments.size() && c < colCount; c++) {
                    alignments[c] = tbl->columnAlignments[c];
                }
                break;
            }
        }

        // Measure: per-column max of natural width (unwrapped) across all non-separator rows.
        std::vector<int> colNatural(colCount, 0);
        for (int i = startLine; i <= endLine; i++) {
            auto* tbl = dynamic_cast<TableLineLayout*>(lineLayouts[i].get());
            if (!tbl || tbl->layoutType == LineLayoutType::TableSeparatorRow) continue;
            for (int c = 0; c < (int)tbl->cellsLayouts.size() && c < colCount; c++) {
                auto& cell = tbl->cellsLayouts[c];
                if (!cell || !cell->layout) continue;
                cell->layout->SetExplicitWidth(-1);   // clear wrap to get natural width
                colNatural[c] = std::max(colNatural[c], cell->layout->GetLayoutWidth());
            }
        }

        // Compute final per-column widths (including padding). Fit case: natural widths + leftover
        // distributed evenly. Overflow case: proportional shrink with a 20px floor. Both cases
        // assign the rounding remainder to the last column so `sum(colWidth) == availWidth`.
        int naturalTotal = 0;
        for (int c = 0; c < colCount; c++) naturalTotal += colNatural[c] + 2 * cellPadding;

        std::vector<int> colWidth(colCount, 0);
        if (naturalTotal > 0 && naturalTotal <= availWidth) {
            int extra = (availWidth - naturalTotal) / colCount;
            for (int c = 0; c < colCount; c++) colWidth[c] = colNatural[c] + 2 * cellPadding + extra;
        } else if (naturalTotal > 0) {
            for (int c = 0; c < colCount; c++) {
                int w = (int)((int64_t)(colNatural[c] + 2 * cellPadding) * availWidth / naturalTotal);
                colWidth[c] = std::max(20, w);
            }
        } else {
            int w = availWidth / colCount;
            for (int c = 0; c < colCount; c++) colWidth[c] = w;
        }
        // Exact-fit: last column absorbs the rounding delta so total == availWidth.
        {
            int sum = 0;
            for (int c = 0; c < colCount - 1; c++) sum += colWidth[c];
            colWidth[colCount - 1] = std::max(20, availWidth - sum);
        }

        // Apply: set ExplicitWidth + alignment per cell, re-measure heights, update row bounds.
        // Every row (incl. header and data rows) gets the alignment vector copied so renderers
        // and future callers can query it uniformly.
        for (int i = startLine; i <= endLine; i++) {
            auto* tbl = dynamic_cast<TableLineLayout*>(lineLayouts[i].get());
            if (!tbl) continue;
            bool isSep = (tbl->layoutType == LineLayoutType::TableSeparatorRow);
            tbl->columnAlignments = alignments;
            int x = 0;
            int maxCellHeight = 0;
            for (int c = 0; c < colCount; c++) {
                if (c >= (int)tbl->cellsLayouts.size()) { x += colWidth[c]; continue; }
                auto& cell = tbl->cellsLayouts[c];
                if (!cell) { x += colWidth[c]; continue; }
                int cw = colWidth[c];
                cell->bounds.x = x + cellPadding;
                cell->bounds.y = rowVerticalPad;
                cell->bounds.width = cw - 2 * cellPadding;
                if (cell->layout && !isSep) {
                    cell->layout->SetExplicitWidth(cell->bounds.width);
                    cell->layout->SetWrap(TextWrap::WrapWordChar);
                    cell->layout->SetAlignment(alignments[c]);
                    cell->bounds.height = cell->layout->GetLayoutHeight();
                    maxCellHeight = std::max(maxCellHeight, cell->bounds.height);
                }
                x += cw;
            }
            tbl->tableColumnCount = colCount;
            tbl->bounds.width  = x;
            // Separator row needs enough vertical room that the line-number gutter doesn't
            // squish two numbers together — 8px is the minimum that stays visually distinct.
            int lh = computedLineHeight > 0
                        ? computedLineHeight
                        : static_cast<int>(style.fontStyle.fontSize * 1.3f);
            tbl->bounds.height = isSep ? lh : (maxCellHeight + 2 * rowVerticalPad);
        }
    }

    void UltraCanvasTextArea::UpdateGeometry(IRenderContext* ctx) {
        // Base class sets needsUpdateGeometry = false via RequestUpdateGeometry tracking;
        // we only need to drive our own caches here. Parent bounds changes land through
        // SetBounds → isNeedRecalculateVisibleArea, which the legacy Render path consumes first.
        UpdateLineLayouts(ctx);

        if (isNeedRecalculateVisibleArea) {
            ctx->PushState();
            if (editingMode == TextAreaEditingMode::Hex) {
                CalculateHexLayout();
            } else {
                CalculateVisibleArea();
            }
            ctx->PopState();
            // Wrap width probably changed — let UpdateLineLayouts detect and reset.
        }
        UltraCanvasUIElement::UpdateGeometry(ctx);
    }

    std::unique_ptr<LineLayoutBase> UltraCanvasTextArea::MakePlainLineLayout(IRenderContext* ctx, int lineIndex) {
        auto ll = std::make_unique<LineLayoutBase>();
        const std::string& txt = lines[lineIndex];

        ll->layoutType = LineLayoutType::PlainLine;
        ll->layout = ctx->CreateTextLayout(txt, false);
        ll->layout->SetFontStyle(style.fontStyle);
        ll->layout->SetLineSpacing(style.lineHeight);
        if (wordWrap && visibleTextArea.width > 0) {
            ll->layout->SetExplicitWidth(visibleTextArea.width);
            ll->layout->SetWrap(TextWrap::WrapWordChar);
        }

        if (style.highlightSyntax && syntaxTokenizer) {
            auto tokens = syntaxTokenizer->TokenizeLine(txt);
            int currentByte = 0;
            for (const auto& token : tokens) {
                int startByte = currentByte;
                int endByte = currentByte + static_cast<int>(token.text.size());
                const TokenStyle& tokenStyle = GetStyleForTokenType(token.type);

                auto fgAttr = TextAttributeFactory::CreateForeground(tokenStyle.color);
                fgAttr->SetRange(startByte, endByte);
                ll->layout->InsertAttribute(std::move(fgAttr));

                if (tokenStyle.bold) {
                    auto wAttr = TextAttributeFactory::CreateWeight(FontWeight::Bold);
                    wAttr->SetRange(startByte, endByte);
                    ll->layout->InsertAttribute(std::move(wAttr));
                }
                if (tokenStyle.italic) {
                    auto sAttr = TextAttributeFactory::CreateStyle(FontSlant::Italic);
                    sAttr->SetRange(startByte, endByte);
                    ll->layout->InsertAttribute(std::move(sAttr));
                }
                if (tokenStyle.underline) {
                    auto uAttr = TextAttributeFactory::CreateUnderline(UCUnderlineType::UnderlineSingle);
                    uAttr->SetRange(startByte, endByte);
                    ll->layout->InsertAttribute(std::move(uAttr));
                }
                currentByte = endByte;
            }
        }
        ApplyLineSelectionBackground(ll.get(), lineIndex);

        ll->bounds.width = ll->layout->GetLayoutWidth();
        ll->bounds.height = ll->layout->GetLayoutHeight();
        return ll;
    }

    std::unique_ptr<LineLayoutBase> UltraCanvasTextArea::MakeLineLayout(IRenderContext* ctx, int lineIndex) {
        if (lineIndex < 0 || lineIndex >= (int)lines.size()) return nullptr;

        if (editingMode == TextAreaEditingMode::MarkdownHybrid) {
            auto md = MakeMarkdownLineLayout(ctx, lineIndex);
            ApplyLineSelectionBackground(md.get(), lineIndex);
            return md;
        }

        // PlainText path: full-line layout with optional syntax highlighting attributes.
        return MakePlainLineLayout(ctx, lineIndex);
    }

    // Build a LineLayoutBase (or derived) for one line in MarkdownHybrid mode.
    // Detects block-level type (heading / fenced code / blockquote / list / HR / paragraph),
    // strips the markup prefix from the layout text, sets layoutTextStartIndex & layoutShift so
    // cursor/selection math can round-trip to the real line text. Fenced-code state is inferred
    // from lineLayouts[lineIndex-1]'s type — callers must invalidate subsequent lines when a
    // structural edit changes fence/list/quote state (Step 8 will tighten this).
    std::unique_ptr<LineLayoutBase> UltraCanvasTextArea::MakeMarkdownLineLayout(IRenderContext* ctx, int lineIndex) {
        const std::string& rawLine = lines[lineIndex];
        std::string trimmed = TrimWhitespace(rawLine);

        // Pixel constants mirroring MarkdownHybridStyle defaults in UltraCanvasTextArea_Markdown.cpp.
        constexpr int quoteIndent = 26;
        constexpr int quoteNestingStep = 20;
        constexpr int listIndent = 20;
        // Horizontal rule height — minimum so the line-number gutter has room for the
        // row's number without bleeding into the neighbors.
        int hrHeight = computedLineHeight > 0
                    ? computedLineHeight
                    : static_cast<int>(style.fontStyle.fontSize * 1.2f);
        constexpr float headerMultipliers[6] = {1.8f, 1.5f, 1.3f, 1.2f, 1.1f, 1.0f};

        // Shared layout configuration applied to every non-null ITextLayout we return.
        auto configureLayout = [&](ITextLayout* layout, int shiftX) {
            layout->SetFontStyle(style.fontStyle);
            layout->SetLineSpacing(style.lineHeight);
            if (wordWrap && visibleTextArea.width > 0) {
                int w = std::max(1, visibleTextArea.width - shiftX);
                layout->SetExplicitWidth(w);
                layout->SetWrap(TextWrap::WrapWordChar);
            }
        };

        // Helper: parse `payload` for inline markdown, build an ITextLayout with the visible
        // (markup-stripped) text, apply configureLayout(shiftX), run the inline attribute pass,
        // and append hit rects to `outHitRects`. Returns the visible text + the built layout.
        auto buildInlineStyledLayout = [&](const std::string& payload, int shiftX,
                                           std::vector<MarkdownHitRect>& outHitRects,
                                           std::string* outVisibleText = nullptr)
            -> std::unique_ptr<ITextLayout>
        {
            std::string visibleText;
            std::vector<InlineRun> runs;
            ParseInlineMarkdownRuns(payload, visibleText, runs);
            auto layout = ctx->CreateTextLayout(visibleText, false);
            configureLayout(layout.get(), shiftX);
            ApplyInlineRunsAndAbbreviations(layout.get(), visibleText, runs, outHitRects);
            if (outVisibleText) *outVisibleText = std::move(visibleText);
            return layout;
        };

        auto makeParagraph = [&]() -> std::unique_ptr<LineLayoutBase> {
            auto ll = std::make_unique<LineLayoutBase>();
            ll->layoutType = LineLayoutType::MarkDownLine;
            ll->layout = buildInlineStyledLayout(rawLine, 0, ll->hitRects);
            // layoutTextStartIndex = 0 for paragraphs — inline markers are scattered so precise
            // codepoint accounting isn't attempted; selection clamps to visible text.
            ll->layoutTextStartIndex = 0;
            ll->bounds.width  = ll->layout->GetLayoutWidth();
            ll->bounds.height = ll->layout->GetLayoutHeight();
            return ll;
        };

        // State from previous layout: open fenced code block?
        LineLayoutBase* prevLayout = (lineIndex > 0 && (lineIndex - 1) < (int)lineLayouts.size())
            ? lineLayouts[lineIndex - 1].get() : nullptr;
        CodeLayout* prevCode = dynamic_cast<CodeLayout*>(prevLayout);
        bool prevIsOpenCode = prevCode && !prevCode->isIndentedCode &&
            (prevCode->layoutType == LineLayoutType::CodeblockStart ||
             prevCode->layoutType == LineLayoutType::CodeblockContent);

        bool isFence = trimmed.size() >= 3 &&
            (trimmed.compare(0, 3, "```") == 0 || trimmed.compare(0, 3, "~~~") == 0);

        // Inside an open fence: every line until a closing fence is code content.
        if (prevIsOpenCode) {
            auto cl = std::make_unique<CodeLayout>();
            cl->codeblockLanguage = prevCode->codeblockLanguage;
            if (isFence) {
                cl->layoutType = LineLayoutType::CodeblockEnd;
                cl->layout = ctx->CreateTextLayout("", false);
            } else {
                cl->layoutType = LineLayoutType::CodeblockContent;
                cl->layout = ctx->CreateTextLayout(rawLine, false);
            }
            cl->layout->SetFontStyle(style.fixedFontStyle);
            cl->layout->SetLineSpacing(style.lineHeight);
            if (wordWrap && visibleTextArea.width > 0) {
                cl->layout->SetExplicitWidth(visibleTextArea.width - style.padding * 2);
                cl->layout->SetWrap(TextWrap::WrapWordChar);
            }

            // Apply per-token syntax highlighting on code-content lines using the shared
            // codeBlockTokenizer (lazy-created, language switched only when it changes).
            if (cl->layoutType == LineLayoutType::CodeblockContent &&
                !cl->codeblockLanguage.empty()) {
                if (!codeBlockTokenizer) {
                    codeBlockTokenizer = std::make_unique<SyntaxTokenizer>();
                }
                if (codeBlockTokenizerLang != cl->codeblockLanguage) {
                    if (!codeBlockTokenizer->SetLanguage(cl->codeblockLanguage)) {
                        codeBlockTokenizer->SetLanguageByExtension(cl->codeblockLanguage);
                    }
                    codeBlockTokenizerLang = cl->codeblockLanguage;
                }
                auto tokens = codeBlockTokenizer->TokenizeLine(rawLine);
                int currentByte = 0;
                for (const auto& token : tokens) {
                    int startByte = currentByte;
                    int endByte = currentByte + static_cast<int>(token.text.size());
                    const TokenStyle& ts = GetStyleForTokenType(token.type);
                    auto fg = TextAttributeFactory::CreateForeground(ts.color);
                    fg->SetRange(startByte, endByte);
                    cl->layout->InsertAttribute(std::move(fg));
                    if (ts.bold) {
                        auto a = TextAttributeFactory::CreateWeight(FontWeight::Bold);
                        a->SetRange(startByte, endByte);
                        cl->layout->InsertAttribute(std::move(a));
                    }
                    if (ts.italic) {
                        auto a = TextAttributeFactory::CreateStyle(FontSlant::Italic);
                        a->SetRange(startByte, endByte);
                        cl->layout->InsertAttribute(std::move(a));
                    }
                    currentByte = endByte;
                }
            }

            cl->bounds.width = cl->layout->GetLayoutWidth() + style.padding * 2;
            cl->bounds.height = std::max(1, cl->layout->GetLayoutHeight());
            cl->layoutShift.x = style.padding;
            return cl;
        }

        // Opening fence (not already in code).
        if (isFence) {
            auto cl = std::make_unique<CodeLayout>();
            cl->layoutType = LineLayoutType::CodeblockStart;
            std::string lang = trimmed.substr(3);
            while (!lang.empty() && std::isspace(static_cast<unsigned char>(lang.back()))) lang.pop_back();
            cl->codeblockLanguage = lang;
            cl->layout = ctx->CreateTextLayout("", false);
            cl->layout->SetFontStyle(style.fixedFontStyle);
            cl->bounds.width = visibleTextArea.width > 0 ? visibleTextArea.width : 0;
            cl->bounds.height = std::max(1, cl->layout->GetLayoutHeight());
            return cl;
        }

        // Horizontal rule: 3+ repeated `-`/`*`/`_` (with whitespace allowed).
        if (!trimmed.empty()) {
            char c0 = trimmed[0];
            if (c0 == '-' || c0 == '*' || c0 == '_') {
                bool allSame = true;
                int count = 0;
                for (char c : trimmed) {
                    if (c == c0) count++;
                    else if (c == ' ' || c == '\t') continue;
                    else { allSame = false; break; }
                }
                if (allSame && count >= 3) {
                    auto hr = std::make_unique<LineLayoutBase>();
                    hr->layoutType = LineLayoutType::HorizontalLine;
                    hr->layout = nullptr;  // Renderer draws the rule directly when layout is null.
                    hr->bounds.width = visibleTextArea.width > 0 ? visibleTextArea.width : 0;
                    hr->bounds.height = hrHeight;
                    return hr;
                }
            }
        }

        // Heading (H1-H6).
        if (!trimmed.empty() && trimmed[0] == '#') {
            int level = 0;
            while (level < 6 && level < (int)trimmed.size() && trimmed[level] == '#') level++;
            if (level > 0 && level < (int)trimmed.size() && trimmed[level] == ' ') {
                std::string payload = trimmed.substr(level + 1);

                // Strip explicit `{#id}` anchor suffix from the payload text. Record the id so
                // we can optionally append a ↩ return icon if the anchor is referenced elsewhere.
                std::string anchorId;
                if (!payload.empty() && payload.back() == '}') {
                    auto open = payload.rfind("{#");
                    if (open != std::string::npos && open + 2 < payload.size() - 1) {
                        anchorId = payload.substr(open + 2, payload.size() - 1 - (open + 2));
                        size_t end = open;
                        while (end > 0 &&
                               (payload[end - 1] == ' ' || payload[end - 1] == '\t')) end--;
                        payload.erase(end);
                    }
                }

                int leadingWs = 0;
                while (leadingWs < (int)rawLine.size() &&
                       (rawLine[leadingWs] == ' ' || rawLine[leadingWs] == '\t')) leadingWs++;
                int startCp = utf8_byte_to_cp(rawLine, leadingWs + level + 1);

                auto ll = std::make_unique<LineLayoutBase>();
                ll->layoutType = LineLayoutType::MarkDownLine;
                std::string visibleText;
                ll->layout = buildInlineStyledLayout(payload, 0, ll->hitRects, &visibleText);
                ll->layoutTextStartIndex = startCp;

                // Heading font-size + weight apply to the entire visible text (pre-↩ suffix).
                int headingEndByte = (int)visibleText.size();
                auto sizeAttr = TextAttributeFactory::CreateSize(
                    style.fontStyle.fontSize * headerMultipliers[level - 1]);
                sizeAttr->SetRange(0, headingEndByte);
                ll->layout->InsertAttribute(std::move(sizeAttr));
                auto weightAttr = TextAttributeFactory::CreateWeight(FontWeight::Bold);
                weightAttr->SetRange(0, headingEndByte);
                ll->layout->InsertAttribute(std::move(weightAttr));

                // Append the ↩ return icon if the anchor has at least one referring link.
                // Appending post-parse means the icon isn't styled as heading-sized — keep it
                // at base size so it reads as a subtle adornment.
                if (!anchorId.empty()) {
                    auto it = markdownAnchorBacklinks.find(anchorId);
                    if (it != markdownAnchorBacklinks.end()) {
                        int backlinkSourceLine = it->second;
                        std::string existing = ll->layout->GetText();
                        int iconStart = (int)existing.size();
                        existing.append(" \xe2\x86\xa9");  // U+21A9 LEFTWARDS ARROW WITH HOOK
                        int iconEnd = (int)existing.size();
                        ll->layout->SetText(existing);
                        Rect2Di s = ll->layout->IndexToPos(iconStart);
                        Rect2Di e = ll->layout->IndexToPos(iconEnd);
                        MarkdownHitRect hit;
                        hit.bounds.x = s.x;
                        hit.bounds.y = s.y;
                        hit.bounds.width  = std::max(1, e.x - s.x);
                        hit.bounds.height = std::max(s.height, e.height);
                        hit.isAnchorReturn = true;
                        hit.url = std::to_string(backlinkSourceLine);
                        hit.altText = "Return to reference";
                        ll->hitRects.push_back(hit);
                    }
                }

                ll->bounds.width = ll->layout->GetLayoutWidth();
                ll->bounds.height = ll->layout->GetLayoutHeight();
                return ll;
            }
        }

        // Blockquote: allow up to 3 leading spaces then one or more '>'.
        {
            int pos = 0;
            int leadSp = 0;
            while (pos < (int)rawLine.size() && rawLine[pos] == ' ' && leadSp < 3) { pos++; leadSp++; }
            if (pos < (int)rawLine.size() && rawLine[pos] == '>') {
                int depth = 0;
                while (pos < (int)rawLine.size() && rawLine[pos] == '>') {
                    depth++;
                    pos++;
                    if (pos < (int)rawLine.size() && rawLine[pos] == ' ') pos++;
                }
                std::string payload = rawLine.substr(pos);
                int startCp = utf8_byte_to_cp(rawLine, pos);

                auto bq = std::make_unique<BlockquoteLayout>();
                bq->layoutType = LineLayoutType::Blockquote;
                bq->quoteLevel = depth;
                bq->layoutTextStartIndex = startCp;
                bq->layoutShift.x = (depth - 1) * quoteNestingStep + quoteIndent;
                bq->layout = buildInlineStyledLayout(payload, bq->layoutShift.x, bq->hitRects);
                bq->bounds.width  = bq->layoutShift.x + bq->layout->GetLayoutWidth();
                bq->bounds.height = bq->layout->GetLayoutHeight();
                return bq;
            }
        }

        // Table row: trimmed line begins with `|` AND (this line is a separator, the next line
        // is a separator, or a neighboring line is also a table row). The pattern keeps single
        // pipe-leading lines (e.g. a paragraph starting with `|`) out of table mode.
        {
            auto isTableRowLine = [](const std::string& t) {
                return !t.empty() && t[0] == '|';
            };
            auto isSeparatorRow = [](const std::string& t) -> bool {
                if (t.size() < 3 || t[0] != '|') return false;
                int dashCount = 0;
                for (char c : t) {
                    if (c == '-') dashCount++;
                    else if (c == '|' || c == ':' || c == ' ' || c == '\t') continue;
                    else return false;
                }
                return dashCount >= 1;
            };
            if (isTableRowLine(trimmed)) {
                bool thisIsSep = isSeparatorRow(trimmed);
                std::string nextTrim = (lineIndex + 1 < (int)lines.size())
                    ? TrimWhitespace(lines[lineIndex + 1]) : std::string();
                std::string prevTrim = (lineIndex > 0)
                    ? TrimWhitespace(lines[lineIndex - 1]) : std::string();
                bool nextIsSep = isSeparatorRow(nextTrim);
                bool prevIsTable = isTableRowLine(prevTrim);
                bool nextIsTable = isTableRowLine(nextTrim);

                if (thisIsSep || nextIsSep || prevIsTable) {
                    // Split by `|`, honoring `\|` escapes and dropping leading/trailing empty cells.
                    auto splitCells = [](const std::string& line) {
                        std::vector<std::string> cells;
                        std::string cur;
                        size_t i = 0;
                        if (!line.empty() && line[0] == '|') i = 1;
                        for (; i < line.size(); i++) {
                            if (line[i] == '\\' && i + 1 < line.size()) {
                                cur.push_back(line[i + 1]);
                                i++;
                                continue;
                            }
                            if (line[i] == '|') {
                                auto from = cur.find_first_not_of(" \t");
                                auto to   = cur.find_last_not_of(" \t");
                                cells.push_back(from == std::string::npos ? ""
                                    : cur.substr(from, to - from + 1));
                                cur.clear();
                            } else {
                                cur.push_back(line[i]);
                            }
                        }
                        if (!cur.empty()) {
                            auto from = cur.find_first_not_of(" \t");
                            auto to   = cur.find_last_not_of(" \t");
                            if (from != std::string::npos)
                                cells.push_back(cur.substr(from, to - from + 1));
                        }
                        return cells;
                    };

                    auto tl = std::make_unique<TableLineLayout>();
                    tl->layoutType = thisIsSep ? LineLayoutType::TableSeparatorRow
                                   : nextIsSep ? LineLayoutType::TableHederRow
                                               : LineLayoutType::TableRow;
                    auto cells = splitCells(trimmed);
                    tl->tableColumnCount = (int)cells.size();
                    tl->lastTableRow = !nextIsTable;
                    tl->layout = nullptr;  // rendered via cellsLayouts
                    tl->layoutTextStartIndex = 0;

                    // On a separator row, parse per-cell alignment markers.
                    // `:---` → Left, `:---:` → Center, `---:` → Right, `---` → Left (default).
                    if (thisIsSep) {
                        tl->columnAlignments.reserve(cells.size());
                        for (const auto& raw : cells) {
                            std::string t;
                            for (char c : raw) if (c != ' ' && c != '\t') t.push_back(c);
                            bool lColon = !t.empty() && t.front() == ':';
                            bool rColon = !t.empty() && t.back() == ':';
                            if (lColon && rColon)      tl->columnAlignments.push_back(TextAlignment::Center);
                            else if (rColon)           tl->columnAlignments.push_back(TextAlignment::Right);
                            else                       tl->columnAlignments.push_back(TextAlignment::Left);
                        }
                    }

                    // Build each cell's layout at its natural (unwrapped) width; NormalizeTable-
                    // GroupWidths will set per-column widths after measuring across all rows.
                    for (int ci = 0; ci < (int)cells.size(); ci++) {
                        auto cell = std::make_unique<LineLayoutBase>();
                        cell->layoutType = LineLayoutType::MarkDownLine;
                        if (thisIsSep) {
                            cell->layout = ctx->CreateTextLayout("", false);
                            cell->layout->SetFontStyle(style.fontStyle);
                        } else {
                            cell->layout = buildInlineStyledLayout(cells[ci], 0, cell->hitRects);
                        }
                        tl->cellsLayouts.push_back(std::move(cell));
                    }

                    // Provisional bounds — Normalize overwrites width / height / per-cell geometry.
                    tl->bounds.width  = std::max(100, visibleTextArea.width);
                    tl->bounds.height = 1;
                    return tl;
                }
            }
        }

        // Ordered list: "N. " or "N) " with optional leading whitespace.
        {
            int pos = 0;
            int leadSp = 0;
            while (pos < (int)rawLine.size() &&
                   (rawLine[pos] == ' ' || rawLine[pos] == '\t')) { pos++; leadSp++; }
            int numStart = pos;
            while (pos < (int)rawLine.size() &&
                   std::isdigit(static_cast<unsigned char>(rawLine[pos]))) pos++;
            if (pos > numStart && pos < (int)rawLine.size() &&
                (rawLine[pos] == '.' || rawLine[pos] == ')') &&
                pos + 1 < (int)rawLine.size() && rawLine[pos + 1] == ' ') {
                int literalNumber = std::atoi(rawLine.substr(numStart, pos - numStart).c_str());
                int markerEnd = pos + 2;
                std::string payload = rawLine.substr(markerEnd);
                int startCp = utf8_byte_to_cp(rawLine, markerEnd);
                int depth = std::max(1, leadSp / 2 + 1);

                // Auto-renumber inside a contiguous ordered-list block at the same depth —
                // only the first item's marker sets the starting number; following items
                // increment regardless of what they typed (so `1. … 1. … 1.` becomes 1,2,3).
                int number = literalNumber;
                if (auto* prevOl = dynamic_cast<OrderedListItemLayout*>(prevLayout);
                    prevOl && prevOl->listDepth == depth) {
                    number = prevOl->orderedItemNumber + 1;
                }

                auto ol = std::make_unique<OrderedListItemLayout>();
                ol->layoutType = LineLayoutType::OrderedListItem;
                ol->orderedItemNumber = number;
                ol->listDepth = depth;
                ol->layoutTextStartIndex = startCp;
                ol->layoutShift.x = depth * listIndent;
                ol->layout = buildInlineStyledLayout(payload, ol->layoutShift.x, ol->hitRects);
                ol->bounds.width  = ol->layoutShift.x + ol->layout->GetLayoutWidth();
                ol->bounds.height = ol->layout->GetLayoutHeight();
                return ol;
            }
        }

        // Unordered list: `-` / `*` / `+` followed by space (with optional leading whitespace).
        {
            int pos = 0;
            int leadSp = 0;
            while (pos < (int)rawLine.size() &&
                   (rawLine[pos] == ' ' || rawLine[pos] == '\t')) { pos++; leadSp++; }
            if (pos + 1 < (int)rawLine.size() &&
                (rawLine[pos] == '-' || rawLine[pos] == '*' || rawLine[pos] == '+') &&
                rawLine[pos + 1] == ' ') {
                int markerEnd = pos + 2;
                std::string payload = rawLine.substr(markerEnd);
                int startCp = utf8_byte_to_cp(rawLine, markerEnd);
                int depth = std::max(1, leadSp / 2 + 1);

                auto ul = std::make_unique<UnorderedListItemLayout>();
                ul->layoutType = LineLayoutType::UnorderedListItem;
                ul->listDepth = depth;
                ul->layoutTextStartIndex = startCp;
                ul->layoutShift.x = depth * listIndent;
                ul->layout = buildInlineStyledLayout(payload, ul->layoutShift.x, ul->hitRects);
                ul->bounds.width  = ul->layoutShift.x + ul->layout->GetLayoutWidth();
                ul->bounds.height = ul->layout->GetLayoutHeight();
                return ul;
            }
        }

        // Definition list continuation: line starts with `: ` (optional leading spaces). The
        // preceding non-blank line is the definition term. This is recognized as a block with
        // a left indent; the term is rendered bold by the previous iteration detecting its
        // following `: ` continuation.
        {
            int pos = 0;
            while (pos < (int)rawLine.size() &&
                   (rawLine[pos] == ' ' || rawLine[pos] == '\t')) pos++;
            if (pos + 1 < (int)rawLine.size() && rawLine[pos] == ':' && rawLine[pos + 1] == ' ') {
                int markerEnd = pos + 2;
                std::string payload = rawLine.substr(markerEnd);
                int startCp = utf8_byte_to_cp(rawLine, markerEnd);
                constexpr int defIndent = 30;

                auto dl = std::make_unique<LineLayoutBase>();
                dl->layoutType = LineLayoutType::DefinitionContinuation;
                dl->layoutTextStartIndex = startCp;
                dl->layoutShift.x = defIndent;
                dl->layout = buildInlineStyledLayout(payload, dl->layoutShift.x, dl->hitRects);
                dl->bounds.width  = dl->layoutShift.x + dl->layout->GetLayoutWidth();
                dl->bounds.height = dl->layout->GetLayoutHeight();
                return dl;
            }
        }

        // Indented code block: line prefixed by ≥4 spaces (or a tab = 4 cols) with content
        // after the indent. Per CommonMark, it cannot interrupt a paragraph — so the
        // previous non-blank line must be a block break (HR, heading, blank, another
        // indented code line, etc.), not an open `MarkDownLine` / list item / definition.
        {
            int indentBytes = 0;
            int indentCols = 0;
            while (indentBytes < (int)rawLine.size() && indentCols < 4) {
                char c = rawLine[indentBytes];
                if (c == '\t') { indentBytes++; indentCols += 4; }
                else if (c == ' ') { indentBytes++; indentCols += 1; }
                else break;
            }
            bool hasContent = indentBytes < (int)rawLine.size();
            bool atLeast4Cols = indentCols >= 4;
            bool prevBlocks = false;
            if (prevLayout) {
                std::string prevTrim = TrimWhitespace(lines[lineIndex - 1]);
                if (!prevTrim.empty()) {
                    switch (prevLayout->layoutType) {
                        case LineLayoutType::MarkDownLine:
                        case LineLayoutType::UnorderedListItem:
                        case LineLayoutType::OrderedListItem:
                        case LineLayoutType::DefinitionTerm:
                        case LineLayoutType::DefinitionContinuation:
                        case LineLayoutType::Blockquote:
                            prevBlocks = true;
                            break;
                        default:
                            break;
                    }
                }
            }
            if (atLeast4Cols && hasContent && !prevBlocks) {
                auto cl = std::make_unique<CodeLayout>();
                cl->layoutType = LineLayoutType::CodeblockContent;
                cl->isIndentedCode = true;
                std::string content = rawLine.substr(indentBytes);
                cl->layout = ctx->CreateTextLayout(content, false);
                cl->layout->SetFontStyle(style.fixedFontStyle);
                cl->layout->SetLineSpacing(style.lineHeight);
                if (wordWrap && visibleTextArea.width > 0) {
                    cl->layout->SetExplicitWidth(
                        std::max(1, visibleTextArea.width - style.padding * 2));
                    cl->layout->SetWrap(TextWrap::WrapWordChar);
                }
                cl->layoutTextStartIndex = utf8_byte_to_cp(rawLine, indentBytes);
                cl->layoutShift.x = style.padding;
                cl->bounds.width  = cl->layout->GetLayoutWidth() + style.padding * 2;
                cl->bounds.height = std::max(1, cl->layout->GetLayoutHeight());
                return cl;
            }
        }

        // Definition term: a non-empty line followed (after any blank lines) by a `: continuation`
        // line. The term is rendered bold. We look ahead at most a few lines.
        if (!trimmed.empty()) {
            for (int ahead = lineIndex + 1; ahead < (int)lines.size(); ahead++) {
                std::string nextTrim = TrimWhitespace(lines[ahead]);
                if (nextTrim.empty()) continue;
                if (nextTrim.size() >= 2 && nextTrim[0] == ':' && nextTrim[1] == ' ') {
                    auto dt = std::make_unique<LineLayoutBase>();
                    dt->layoutType = LineLayoutType::DefinitionTerm;
                    std::string visibleText;
                    dt->layout = buildInlineStyledLayout(rawLine, 0, dt->hitRects, &visibleText);
                    int endByte = (int)visibleText.size();
                    auto w = TextAttributeFactory::CreateWeight(FontWeight::Bold);
                    w->SetRange(0, endByte);
                    dt->layout->InsertAttribute(std::move(w));
                    dt->bounds.width  = dt->layout->GetLayoutWidth();
                    dt->bounds.height = dt->layout->GetLayoutHeight();
                    return dt;
                }
                break;  // first non-blank line after this isn't a def continuation.
            }
        }

        // Fallback: plain paragraph (includes tables and anything else not matched above).
        return makeParagraph();
    }

    void UltraCanvasTextArea::RenderLineLayout(IRenderContext *ctx, LineLayoutBase* line) {
        if (!line) return;

        // Style constants mirroring MarkdownHybridStyle defaults.
        const int quoteBarWidth = 3;
        const int quoteNestingStep = 20;
        const int listIndentPx = 20;
        const Color quoteBarColor(200, 200, 200);
        const Color codeBgColor(248, 248, 248);
        const Color hrColor(200, 200, 200);
        const Color markerColor(80, 80, 80);

        // bounds.x/y are content-relative (top of text area, before scroll). verticalScrollOffset
        // is the pixel Y-offset into the content that corresponds to the top of visibleTextArea.
        Point2Di posOrigin{visibleTextArea.x + line->bounds.x - horizontalScrollOffset,
                              visibleTextArea.y + line->bounds.y - verticalScrollOffset};
        Point2Di layoutOrigin{posOrigin.x + line->layoutShift.x,
                              posOrigin.y + line->layoutShift.y};

        auto drawLayout = [&]() {
            if (line->layout) {
                ctx->SetCurrentPaint(style.fontColor);
                ctx->DrawTextLayout(*line->layout,
                    Point2Df(static_cast<float>(layoutOrigin.x),
                             static_cast<float>(layoutOrigin.y)));
            }
        };

        switch (line->layoutType) {
            case LineLayoutType::PlainLine:
            case LineLayoutType::MarkDownLine: {
                drawLayout();
                return;
            }
            case LineLayoutType::HorizontalLine: {
                // Horizontal rule: draw a single line centered in the bounds.
                int centerY = posOrigin.y + line->bounds.height / 2;
                ctx->DrawLine(
                        Point2Df(static_cast<float>(posOrigin.x),
                                 static_cast<float>(centerY)),
                        Point2Df(static_cast<float>(posOrigin.x + line->bounds.width),
                                 static_cast<float>(centerY)),
                        hrColor);
                return;
            }
            case LineLayoutType::Blockquote: {
                auto bq = dynamic_cast<BlockquoteLayout*>(line);
                int depth = bq ? bq->quoteLevel : 1;
                for (int d = 1; d <= depth; d++) {
                    int barX = posOrigin.x + (d - 1) * quoteNestingStep;
                    ctx->DrawFilledRectangle(
                        Rect2Df(static_cast<float>(barX),
                                static_cast<float>(posOrigin.y),
                                static_cast<float>(quoteBarWidth),
                                static_cast<float>(line->bounds.height)),
                        quoteBarColor, 0.0f, Colors::Transparent);
                }
                drawLayout();
                return;
            }
            case LineLayoutType::CodeblockStart:
            case LineLayoutType::CodeblockEnd:
            case LineLayoutType::CodeblockContent: {
                // Background always spans the full visible text area width, not just the
                // measured content width — so contiguous code rows visually merge into one
                // uniform block regardless of per-line text length.

                double borderTop = (line->layoutType == LineLayoutType::CodeblockStart) ? 1 : 0;
                double borderBottom = (line->layoutType == LineLayoutType::CodeblockEnd) ? 1 : 0;
                double y = (line->layoutType == LineLayoutType::CodeblockStart) ? (posOrigin.y + line->bounds.height / 2 + 2) : posOrigin.y;
                ctx->SetFillPaint(codeBgColor);
                ctx->DrawRoundedRectangleWidthBorders(
                    Rect2Df(visibleTextArea.x,
                            y,
                            visibleTextArea.width,
                            line->bounds.height),
                    true, 
                    1, 1, borderTop, borderBottom,
                    Colors::LightGray, Colors::LightGray,
                    Colors::LightGray, Colors::LightGray,
                    0,0,0,0,
                    UCDashPattern::EMPTY, UCDashPattern::EMPTY, 
                    UCDashPattern::EMPTY, UCDashPattern::EMPTY
                );
                if (line->layoutType == LineLayoutType::CodeblockStart) {
                    auto cl = dynamic_cast<CodeLayout*>(line);
                    if (cl && !cl->codeblockLanguage.empty()) {
                        ctx->PushState();
                        ctx->SetTextPaint(Color(100, 100, 100));
                        auto cblangLayout = ctx->CreateTextLayout(" "+cl->codeblockLanguage+" ", false);
                        cblangLayout->SetFontStyle(style.fixedFontStyle);
                        cblangLayout->InsertAttribute(TextAttributeFactory::CreateBackground(codeBgColor));
                        ctx->DrawTextLayout(*cblangLayout.get(),
                                            Point2Df(posOrigin.x + 4,
                                                     posOrigin.y)
                        );
                        ctx->PopState();
                    }
                } else {
                    drawLayout();
                }
                return;
            }
            case LineLayoutType::UnorderedListItem: {
                auto ul = dynamic_cast<UnorderedListItemLayout*>(line);
                int depth = ul ? ul->listDepth : 1;
                static const char* const bullets[3] = {
                    "\xe2\x80\xa2",  // U+2022 BULLET
                    "\xe2\x97\xa6",  // U+25E6 WHITE BULLET
                    "\xe2\x96\xaa"   // U+25AA BLACK SMALL SQUARE
                };
                const char* bullet = bullets[std::min(std::max(depth - 1, 0), 2)];
                int markerX = posOrigin.x + (depth - 1) * listIndentPx;
                ctx->PushState();
                ctx->SetFontStyle(style.fontStyle);
                ctx->SetTextPaint(markerColor);
                ctx->DrawText(bullet, Point2Df(static_cast<float>(markerX),
                                               static_cast<float>(posOrigin.y)));
                ctx->PopState();
                drawLayout();
                return;
            }
            case LineLayoutType::OrderedListItem: {
                auto ol = dynamic_cast<OrderedListItemLayout*>(line);
                int number = ol ? ol->orderedItemNumber : 1;
                int depth = ol ? ol->listDepth : 1;
                std::string numStr = std::to_string(number) + ".";
                int markerX = posOrigin.x + (depth - 1) * listIndentPx;
                ctx->PushState();
                ctx->SetFontStyle(style.fontStyle);
                ctx->SetTextPaint(markerColor);
                ctx->DrawText(numStr, Point2Df(static_cast<float>(markerX),
                                               static_cast<float>(posOrigin.y)));
                ctx->PopState();
                drawLayout();
                return;
            }
            case LineLayoutType::TableHederRow:
            case LineLayoutType::TableSeparatorRow:
            case LineLayoutType::TableRow: {
                auto tbl = dynamic_cast<TableLineLayout*>(line);
                if (!tbl) { drawLayout(); return; }
                const Color tableBorderColor(200, 200, 200);
                const Color tableHeaderBg(240, 240, 240);

                // Header row background tint.
                if (line->layoutType == LineLayoutType::TableHederRow) {
                    ctx->DrawFilledRectangle(
                        Rect2Df(static_cast<float>(posOrigin.x),
                                static_cast<float>(posOrigin.y),
                                static_cast<float>(tbl->bounds.width),
                                static_cast<float>(tbl->bounds.height)),
                        tableHeaderBg, 0.0f, Colors::Transparent);
                }

                if (line->layoutType == LineLayoutType::TableSeparatorRow) {
                    int midY = posOrigin.y + tbl->bounds.height / 2;
                    ctx->DrawLine(
                        Point2Df(static_cast<float>(posOrigin.x),
                                 static_cast<float>(midY)),
                        Point2Df(static_cast<float>(posOrigin.x + tbl->bounds.width),
                                 static_cast<float>(midY)),
                        tableBorderColor);
                } else {
                    // Render each cell at its layout-local origin.
                    for (const auto& cell : tbl->cellsLayouts) {
                        if (!cell || !cell->layout) continue;
                        ctx->DrawTextLayout(*cell->layout,
                            Point2Df(static_cast<float>(posOrigin.x + cell->bounds.x),
                                     static_cast<float>(posOrigin.y + cell->bounds.y)));
                    }
                }

                // Vertical dividers between columns. Each cell's right edge (in layout-local
                // coords) is `cell->bounds.x + cell->bounds.width + cellPadding`; we place a
                // divider there for every cell except the last.
                constexpr int cellPadding = 4;
                for (size_t c = 0; c + 1 < tbl->cellsLayouts.size(); c++) {
                    const auto& cell = tbl->cellsLayouts[c];
                    if (!cell) continue;
                    int bx = posOrigin.x + cell->bounds.x + cell->bounds.width + cellPadding;
                    ctx->DrawLine(
                        Point2Df(static_cast<float>(bx),
                                 static_cast<float>(posOrigin.y)),
                        Point2Df(static_cast<float>(bx),
                                 static_cast<float>(posOrigin.y + tbl->bounds.height))
                        );
                }
                if (line->layoutType != LineLayoutType::TableSeparatorRow) {
                    // Top border on every row + bottom border on the last row.
//                    ctx->DrawLine(
//                            Point2Df(static_cast<float>(boundsOrigin.x),
//                                     static_cast<float>(boundsOrigin.y)),
//                            Point2Df(static_cast<float>(boundsOrigin.x + tbl->bounds.width),
//                                     static_cast<float>(boundsOrigin.y)),
//                            tableBorderColor);
                    if (tbl->lastTableRow) {
                        int by = posOrigin.y + tbl->bounds.height;
                        ctx->DrawLine(
                                Point2Df(static_cast<float>(posOrigin.x),
                                         static_cast<float>(by)),
                                Point2Df(static_cast<float>(posOrigin.x + tbl->bounds.width),
                                         static_cast<float>(by)),
                                tableBorderColor);
                    }
                }
                return;
            }
            case LineLayoutType::DefinitionTerm:
            case LineLayoutType::DefinitionContinuation:
                // Definition term / continuation share the paragraph render path. The term has
                // a bold-weight attribute applied at build time; the continuation uses layoutShift
                // for its indent. No additional decoration needed.
                drawLayout();
                return;
        }
    }


    // ===== FACTORY FUNCTIONS =====
    std::shared_ptr<UltraCanvasTextArea> CreateMarkdownEditor(
            const std::string& name, int id, int x, int y, int width, int height) {
        
        auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
        
        // Enable hybrid markdown mode
        editor->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        
        // Apply markdown-friendly styling
        editor->ApplyPlainTextStyle();
        editor->SetWordWrap(true);
        
        return editor;
    }

    std::shared_ptr<UltraCanvasTextArea> CreateCodeEditor(const std::string& name, int id,
                                                          int x, int y, int width, int height,
                                                          const std::string& language) {
        auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
        editor->ApplyCodeStyle(language);
        return editor;
    }

    std::shared_ptr<UltraCanvasTextArea> CreateDarkCodeEditor(const std::string& name, int id,
                                                              int x, int y, int width, int height,
                                                              const std::string& language) {
        auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
        editor->ApplyDarkCodeStyle(language);
        return editor;
    }

    std::shared_ptr<UltraCanvasTextArea> CreatePlainTextEditor(const std::string& name, int id,
                                                               int x, int y, int width, int height) {
        auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
        editor->ApplyPlainTextStyle();
        return editor;
    }

    // std::shared_ptr<UltraCanvasTextArea> CreateMarkdownEditor(const std::string& name, int id,
    //                                                            int x, int y, int width, int height) {
    //     auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
    //     editor->ApplyCodeStyle("Markdown");
    //     return editor;
    // }

    std::shared_ptr<UltraCanvasTextArea> CreateJSONEditor(const std::string& name, int id,
                                                           int x, int y, int width, int height) {
        auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
        editor->ApplyCodeStyle("JSON");
        return editor;
    }

    std::shared_ptr<UltraCanvasTextArea> CreateXMLEditor(const std::string& name, int id,
                                                          int x, int y, int width, int height) {
        auto editor = std::make_shared<UltraCanvasTextArea>(name, id, x, y, width, height);
        editor->ApplyCodeStyle("XML");
        return editor;
    }
} // namespace UltraCanvas