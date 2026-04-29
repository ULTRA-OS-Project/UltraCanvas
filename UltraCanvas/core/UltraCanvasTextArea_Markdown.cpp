// UltraCanvas/core/UltraCanvasTextArea_Markdown.cpp
// Markdown hybrid rendering enhancement for TextArea
// Shows current line as plain text, all other lines as formatted markdown
// Version: 2.5.1
// Last Modified: 2026-04-16
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
    // Maps :shortcode: names to their UTF-8 emoji. Mirrors the table in
    // UltraCanvasTextArea_Markdown.cpp — kept duplicated to keep the inline scanner
    // self-contained rather than introducing a shared header for a single lookup.
    static const std::string& LookupEmojiShortcode(const std::string& code) {
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
    static const std::vector<Emoticon>& TypographyTable() {
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
    static const std::vector<Emoticon>& EmoticonTable() {
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
    static std::string SubstituteGreekLetters(const std::string& input) {
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
            std::string line = GetLineContent(i);
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

        for (const auto& run : runs) {
            if (run.startByte >= run.endByte) continue;
            switch (run.kind) {
                case InlineRun::Bold: {
                    auto a = TextAttributeFactory::CreateFontWeight(FontWeight::Bold);
                    a->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(a));
                    break;
                }
                case InlineRun::Italic: {
                    auto a = TextAttributeFactory::CreateFontStyle(FontSlant::Italic);
                    a->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(a));
                    break;
                }
                case InlineRun::Code: {
                    auto fs = TextAttributeFactory::CreateFontStyle(style.fixedFontStyle);
                    fs->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(fs));
                    auto fg = TextAttributeFactory::CreateForeground(markdownStyle.codeTextColor);
                    fg->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(fg));
                    auto bg = TextAttributeFactory::CreateBackground(markdownStyle.codeBackgroundColor);
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
                    auto italic = TextAttributeFactory::CreateFontStyle(FontSlant::Italic);
                    italic->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(italic));
                    auto fg = TextAttributeFactory::CreateForeground(markdownStyle.mathTextColor);
                    fg->SetRange(run.startByte, run.endByte);
                    layout->InsertAttribute(std::move(fg));
                    break;
                }
                case InlineRun::Link:
                case InlineRun::Image:
                case InlineRun::Footnote: {
                    const Color& runColor = (run.kind == InlineRun::Footnote)
                                            ? markdownStyle.footnoteRefColor
                                            : markdownStyle.linkColor;
                    auto fg = TextAttributeFactory::CreateForeground(runColor);
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
        std::vector<double> colNatural(colCount, 0);
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
            double maxCellHeight = 0;
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

    // Build a LineLayoutBase (or derived) for one line in MarkdownHybrid mode.
    // Detects block-level type (heading / fenced code / blockquote / list / HR / paragraph),
    // strips the markup prefix from the layout text, sets layoutTextStartIndex & layoutShift so
    // cursor/selection math can round-trip to the real line text. Fenced-code state is inferred
    // from lineLayouts[lineIndex-1]'s type — callers must invalidate subsequent lines when a
    // structural edit changes fence/list/quote state (Step 8 will tighten this).
    std::unique_ptr<LineLayoutBase> UltraCanvasTextArea::MakeMarkdownLineLayout(IRenderContext* ctx, int lineIndex) {
        // Strip trailing \n so neither Pango layouts nor markdown parsing see the line terminator.
        std::string rawLine = GetLineContent(lineIndex);
        std::string trimmed = TrimWhitespace(rawLine);

        // Pixel constants sourced from markdownStyle (instead of duplicated literals)
        // so SetMarkdownStyle() can adjust indents/header sizes consistently between
        // layout-build and render paths.
        const int quoteIndent      = markdownStyle.quoteIndent;
        const int quoteNestingStep = markdownStyle.quoteNestingStep;
        const int listIndent       = markdownStyle.listIndent;
        // Horizontal rule height — minimum so the line-number gutter has room for the
        // row's number without bleeding into the neighbors.
        int hrHeight = computedLineHeight > 0
                       ? computedLineHeight
                       : static_cast<int>(style.fontStyle.fontSize * 1.2f);
        const auto& headerMultipliers = markdownStyle.headerSizeMultipliers;

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
                        auto a = TextAttributeFactory::CreateFontWeight(FontWeight::Bold);
                        a->SetRange(startByte, endByte);
                        cl->layout->InsertAttribute(std::move(a));
                    }
                    if (ts.italic) {
                        auto a = TextAttributeFactory::CreateFontStyle(FontSlant::Italic);
                        a->SetRange(startByte, endByte);
                        cl->layout->InsertAttribute(std::move(a));
                    }
                    currentByte = endByte;
                }
            }

            cl->bounds.width = cl->layout->GetLayoutWidth() + style.padding * 2;
            cl->bounds.height = std::max(1.0, cl->layout->GetLayoutHeight());
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
            if (!lang.empty()) {
                cl->layout = ctx->CreateTextLayout(" "+lang+" ", false);
                cl->layout->SetFontStyle(style.fixedFontStyle);
                cl->layout->InsertAttribute(TextAttributeFactory::CreateBackground(markdownStyle.codeBackgroundColor));
                cl->layoutShift = {5,0};
                cl->bounds.height = std::max(1.0, cl->layout->GetLayoutHeight());
            } else {
                cl->bounds.height = computedLineHeight;
            }
            cl->bounds.width = visibleTextArea.width > 0 ? visibleTextArea.width : 0;
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
                auto sizeAttr = TextAttributeFactory::CreateFontSize(
                        style.fontStyle.fontSize * headerMultipliers[level - 1]);
                sizeAttr->SetRange(0, headingEndByte);
                ll->layout->InsertAttribute(std::move(sizeAttr));
                auto weightAttr = TextAttributeFactory::CreateFontWeight(FontWeight::Bold);
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
                                       ? TrimWhitespace(GetLineContent(lineIndex + 1)) : std::string();
                std::string prevTrim = (lineIndex > 0)
                                       ? TrimWhitespace(GetLineContent(lineIndex - 1)) : std::string();
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
                std::string prevTrim = TrimWhitespace(GetLineContent(lineIndex - 1));
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
                cl->bounds.height = std::max(1.0, cl->layout->GetLayoutHeight());
                return cl;
            }
        }

        // Definition term: a non-empty line followed (after any blank lines) by a `: continuation`
        // line. The term is rendered bold. We look ahead at most a few lines.
        if (!trimmed.empty()) {
            for (int ahead = lineIndex + 1; ahead < (int)lines.size(); ahead++) {
                std::string nextTrim = TrimWhitespace(GetLineContent(ahead));
                if (nextTrim.empty()) continue;
                if (nextTrim.size() >= 2 && nextTrim[0] == ':' && nextTrim[1] == ' ') {
                    auto dt = std::make_unique<LineLayoutBase>();
                    dt->layoutType = LineLayoutType::DefinitionTerm;
                    std::string visibleText;
                    dt->layout = buildInlineStyledLayout(rawLine, 0, dt->hitRects, &visibleText);
                    int endByte = (int)visibleText.size();
                    auto w = TextAttributeFactory::CreateFontWeight(FontWeight::Bold);
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
