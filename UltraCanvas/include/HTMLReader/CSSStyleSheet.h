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
    // multiple calls). At-rules (@media, @font-face, ...) are skipped whole;
    // selectors using unsupported syntax (pseudo-classes, attribute selectors,
    // sibling combinators) are dropped individually so the rest of the rule
    // still applies.
    void ParseAppend(const std::string& css);

    void Clear() { rules.clear(); nextOrder = 0; }

    // Parse a bare declaration list — the content of a style="" attribute.
    static std::vector<Declaration> ParseDeclarationList(const std::string& text);

private:
    int nextOrder = 0;
};

// Lowercase-trim helper shared by parser and resolver.
std::string TrimLower(const std::string& text);
std::string Trim(const std::string& text);

} // namespace HTML
} // namespace UltraCanvas
