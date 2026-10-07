// include/HTMLReader/CSSStyleSheet.h
// CSS-subset stylesheet model and parser for the HTMLReader module.
// Covers the CSS that real eBooks use: element/class/id selectors with
// descendant chains, the box-model / typography / color properties, and a
// specificity-ordered cascade. Framework-independent: value types here are
// plain structs; HTMLElementBuilder maps them onto CSSLayout/widget types.
// Version: 1.3.0 - selector matching lives here, for any tree: SelectorMatches /
//                  CompoundMatches / MatchingRules over a Traits type (the HTML
//                  DOM's is NodeSelectorTraits; the SVG reader supplies its own)
// Version: 1.2.0 - structural pseudo-classes (:first-child, :last-child,
//                  :nth-child() and the -of-type forms, :only-child, :root, :empty)
// Version: 1.1.0 - attribute selectors ([a], [a=v], ~= ^= $= *= |=)
// Version: 1.0.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework
#pragma once

#include <algorithm>
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
// [name], [name=value] and the ~= ^= $= *= |= forms; mail templates style by
// them (Mailchimp: td[class=mcnTextContent], table[id=templateBody]).
struct AttributeSelector {
    std::string name;                 // lower-case
    char op = 0;                      // 0 = present, '=', '~', '^', '$', '*', '|'
    std::string value;
    bool ignoreCase = false;          // [name=value i]
};

// Structural pseudo-classes, all as an+b positions: :first-child is
// nth-child(1), :last-child nth-last-child(1), :only-child both. ofType
// counts only siblings with the same tag; fromEnd counts from the last.
struct PseudoClass {
    enum class Kind { Nth, Root, Empty };
    Kind kind = Kind::Nth;
    int a = 0, b = 1;                 // matches positions a*n + b, n >= 0 (1-based)
    bool fromEnd = false;
    bool ofType = false;
};

struct SimpleSelector {
    std::string tag;                  // empty or "*" = any element
    std::vector<std::string> classes;
    std::string id;
    std::vector<AttributeSelector> attributes;
    std::vector<PseudoClass> pseudos;
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

// ---- Matching ----
//
// Whether a selector matches an element is decided here, once, for every
// tree the framework styles: the HTML DOM (HTMLStyleResolver, through
// NodeSelectorTraits in HTMLStyleResolver.h) and any other tree of elements
// with names, ids, classes and attributes - the Vector plugin's SVG reader
// holds tinyxml2 elements. A tree takes part by supplying a Traits type with
// these static members; nothing here depends on the tree's node type.
//
//   using Element = <the tree's element type>;
//   static bool TagIs(const Element&, const std::string& lowerTag);
//       The element's name, without a namespace prefix, is this name. The
//       parser lower-cases type selectors, so a camelCase vocabulary (SVG's
//       linearGradient) compares case-insensitively.
//   static bool IdIs(const Element&, const std::string& id);
//   static bool HasClass(const Element&, const std::string& name);
//       Exact: class names and ids keep their case, in CSS and here.
//   static bool GetAttribute(const Element&, const std::string& lowerName,
//                            std::string& value);
//       False when the attribute is absent. The parser lower-cases the name.
//   static bool IsLink(const Element&);     // <a href>: :link / :any-link
//   static bool IsRoot(const Element&);     // the document element: :root
//   static bool IsEmpty(const Element&);    // no child elements, no text: :empty
//   static bool SiblingPosition(const Element&, bool ofType, int& index, int& count);
//       The element's 1-based position among its parent's element children
//       (those of the same name when ofType) and how many there are; false
//       when it has no parent.
//   static const Element* Parent(const Element&);   // nullptr at the top
//
// The attribute operators and the an+b arithmetic are plain functions, so
// the templates stay small and that logic exists once.

// Whether `value` satisfies the selector's operator ([a=v], ~= ^= $= *= |=,
// the `i` flag); a bare [a] is satisfied by any value.
bool AttributeValueMatches(const AttributeSelector& attr, const std::string& value);

// Whether the 1-based position `index` of `count` siblings is one of the
// positions an :nth-* pseudo-class names (from the end for its -last- forms).
bool NthPositionMatches(const PseudoClass& pc, int index, int count);

// One compound selector ("p.first[lang]") against the element itself.
template <class Traits>
bool CompoundMatches(const SimpleSelector& part, const typename Traits::Element& e) {
    if (!part.tag.empty() && part.tag != "*" && !Traits::TagIs(e, part.tag)) return false;
    if (!part.id.empty() && !Traits::IdIs(e, part.id)) return false;
    for (const auto& cls : part.classes) {
        if (!Traits::HasClass(e, cls)) return false;
    }
    if (part.link && !Traits::IsLink(e)) return false;
    for (const auto& pc : part.pseudos) {
        if (pc.kind == PseudoClass::Kind::Root) {
            if (!Traits::IsRoot(e)) return false;
            continue;
        }
        if (pc.kind == PseudoClass::Kind::Empty) {
            if (!Traits::IsEmpty(e)) return false;
            continue;
        }
        int index = 0, count = 0;
        if (!Traits::SiblingPosition(e, pc.ofType, index, count)) return false;
        if (!NthPositionMatches(pc, index, count)) return false;
    }
    for (const auto& attr : part.attributes) {
        std::string value;
        if (!Traits::GetAttribute(e, attr.name, value)) return false;
        if (!AttributeValueMatches(attr, value)) return false;
    }
    return true;
}

// A whole selector: its last compound against the element, the ones before
// it against ancestors, nearest last, in order (the parser reads `>` as a
// descendant combinator, so this is the only relation).
template <class Traits>
bool SelectorMatches(const Selector& selector, const typename Traits::Element& e) {
    if (selector.path.empty()) return false;
    if (!CompoundMatches<Traits>(selector.path.back(), e)) return false;
    int index = static_cast<int>(selector.path.size()) - 2;
    for (const typename Traits::Element* ancestor = Traits::Parent(e);
         index >= 0 && ancestor; ancestor = Traits::Parent(*ancestor)) {
        if (CompoundMatches<Traits>(selector.path[static_cast<size_t>(index)], *ancestor)) --index;
    }
    return index < 0;
}

// The rules of `sheet` that match `e`, in cascade order: weakest first, by
// the specificity of the rule's best matching selector, then by source order.
// Applying their declarations in this order, the normal ones first and the
// !important ones after, is the cascade within the author origin; what
// style="" adds on top is the caller's (see HTMLStyleResolver.cpp).
template <class Traits>
std::vector<const Rule*> MatchingRules(const StyleSheet& sheet, const typename Traits::Element& e) {
    struct Match {
        int specificity;
        int order;
        const Rule* rule;
    };
    std::vector<Match> matches;
    for (const auto& rule : sheet.rules) {
        int best = -1;
        for (const auto& selector : rule.selectors) {
            if (SelectorMatches<Traits>(selector, e)) best = std::max(best, selector.Specificity());
        }
        if (best >= 0) matches.push_back({best, rule.sourceOrder, &rule});
    }
    std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
        return a.specificity != b.specificity ? a.specificity < b.specificity : a.order < b.order;
    });
    std::vector<const Rule*> rules;
    rules.reserve(matches.size());
    for (const auto& m : matches) rules.push_back(m.rule);
    return rules;
}

} // namespace HTML
} // namespace UltraCanvas
