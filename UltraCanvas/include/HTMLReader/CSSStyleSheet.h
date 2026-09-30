// include/HTMLReader/CSSStyleSheet.h
// CSS-subset stylesheet model and parser for the HTMLReader module.
// Covers the CSS that real eBooks use: element/class/id selectors with
// descendant chains, the box-model / typography / color properties, and a
// specificity-ordered cascade. Framework-independent: value types here are
// plain structs; HTMLElementBuilder maps them onto CSSLayout/widget types.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace UltraCanvas {
namespace HTML {

// ---- Values ----

struct CssColor {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    // #rgb / #rrggbb / #rrggbbaa, rgb()/rgba(), and the common named colors.
    static std::optional<CssColor> Parse(const std::string& text);
};

enum class CssUnit {
    Px,
    Em,
    Rem,
    Percent,
    Pt,
    Number,   // unitless (line-height multiplier, etc.)
    Auto
};

struct CssLength {
    float value = 0.f;
    CssUnit unit = CssUnit::Px;

    static std::optional<CssLength> Parse(const std::string& text);

    // percentBase is the length a percentage resolves against (0 when the
    // caller has nothing meaningful; percent then resolves to 0).
    float ToPx(float emPx, float remPx, float percentBase = 0.f) const;
};

// ---- Rules ----

struct Declaration {
    std::string property;   // lowercase
    std::string value;      // raw value text, trimmed, !important removed
    bool important = false;
};

// One compound selector: tag, classes and id that must all match one element.
struct SimpleSelector {
    std::string tag;                  // empty or "*" = any element
    std::vector<std::string> classes;
    std::string id;
    // :link / :any-link - an <a href>. Links are all unvisited here, so a
    // :visited rule never matches and :hover / :active / :focus rules are
    // dropped (a static render is never hovered).
    bool link = false;
};

// A descendant chain: "div.chapter p.first" — path.back() matches the element
// itself; earlier entries must match ancestors in order. Child combinator '>'
// is accepted and treated as descendant (documented approximation).
struct Selector {
    std::vector<SimpleSelector> path;

    int Specificity() const;   // id*10000 + class*100 + tag
};

struct Rule {
    std::vector<Selector> selectors;
    std::vector<Declaration> declarations;
    int sourceOrder = 0;
};

class StyleSheet {
public:
    std::vector<Rule> rules;

    // Parse css text and append its rules (cascade order is preserved across
    // multiple calls). An @media block's rules apply when its query matches
    // the media width (below); other at-rules (@font-face, @import, ...) are
    // skipped whole. Selectors using unsupported syntax (most pseudo-classes,
    // attribute selectors, sibling combinators) are dropped individually so
    // the rest of the rule still applies.
    void ParseAppend(const std::string& css);

    void Clear() { rules.clear(); nextOrder = 0; }

    // The viewport width, in CSS px, that @media queries are answered for
    // (min-width / max-width / width). Set it before ParseAppend.
    void SetMediaWidth(float px) { mediaWidth = px; }
    float GetMediaWidth() const { return mediaWidth; }

    // Whether a media query list ("only screen and (min-width:480px), print")
    // holds on a screen `widthPx` wide. Unknown media features do not match.
    static bool MediaMatches(const std::string& query, float widthPx);

    // Parse a bare declaration list — the content of a style="" attribute.
    static std::vector<Declaration> ParseDeclarationList(const std::string& text);

private:
    int nextOrder = 0;
    float mediaWidth = 800.f;
};

// Lowercase-trim helper shared by parser and resolver.
std::string TrimLower(const std::string& text);

} // namespace HTML
} // namespace UltraCanvas
