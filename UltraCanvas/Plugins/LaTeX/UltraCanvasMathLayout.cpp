// Plugins/LaTeX/UltraCanvasMathLayout.cpp
// Box builder of the native math engine. See UltraCanvasMathLayout.h.
//
// Rule numbers in comments refer to The TeXbook, Appendix G; the constants
// are the OpenType MATH ones (UltraCanvasMathFont.h), which map onto TeX's
// sigma/xi parameters as the MATH specification documents.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "Plugins/LaTeX/UltraCanvasMathLayout.h"
#include "Plugins/LaTeX/UltraCanvasMathSymbols.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace UltraCanvas {

namespace {

// The typesetting environment while walking the tree.
struct Env {
    MathStyle style = MathStyle::Display();
    float userScale = 1.f;                 // \small etc.
    MathColor color = kMathColorNone;      // current foreground (None = default)
    bool textRun = false;                  // inside \text: no math spacing
};

// Inter-atom spacing, TeXbook p.170. 0 = none, 1 = thin, 2 = medium, 3 = thick;
// negative = only in non-script styles.
const int kSpacing[8][8] = {
    //        Ord  Op   Bin  Rel  Open Close Punct Inner
    /*Ord*/  { 0,   1,  -2,  -3,   0,   0,    0,   -1},
    /*Op*/   { 1,   1,   0,  -3,   0,   0,    0,   -1},
    /*Bin*/  {-2,  -2,   0,   0,  -2,   0,    0,   -2},
    /*Rel*/  {-3,  -3,   0,   0,  -3,   0,    0,   -3},
    /*Open*/ { 0,   0,   0,   0,   0,   0,    0,    0},
    /*Close*/{ 0,   1,  -2,  -3,   0,   0,    0,   -1},
    /*Punct*/{-1,  -1,   0,  -1,  -1,  -1,   -1,   -1},
    /*Inner*/{-1,   1,  -2,  -3,  -1,   0,   -1,   -1},
};

// An HList under construction.
struct HBuilder {
    MathBoxPtr box = MathBox::MakeList();
    float x = 0.f;
    void Add(const MathBoxPtr& child, float dy = 0.f) {
        if (!child) return;
        box->Add(child, x, dy);
        x += child->width;
        box->height = std::max(box->height, child->height - dy);
        box->depth = std::max(box->depth, child->depth + dy);
    }
    void Kern(float w) { x += w; }
    MathBoxPtr Finish() {
        box->width = x;
        return box;
    }
};

// Boxes stacked vertically; `Add` places a child with its left edge at dx
// and returns the child's baseline offset used.
struct VBuilder {
    std::vector<MathBoxChild> items;
    float width = 0.f;
    // Places `child` so that its top is `gap` below the current bottom.
    float cursor = 0.f;    // current bottom (positive down) relative to a provisional origin
    bool first = true;
    void Add(const MathBoxPtr& child, float dx, float gap = 0.f) {
        if (!child) return;
        const float baseline = first ? 0.f : cursor + gap + child->height;
        if (first) cursor = child->depth;
        else cursor = baseline + child->depth;
        first = false;
        items.push_back({dx, baseline, child});
        width = std::max(width, dx + child->width);
    }
    // Builds the list with the baseline of item `baselineIndex` as the box baseline.
    MathBoxPtr Finish(int baselineIndex) {
        auto box = MathBox::MakeList();
        if (items.empty()) return box;
        const float ref = items[std::max(0, std::min<int>(baselineIndex, static_cast<int>(items.size()) - 1))].dy;
        float top = 1e9f, bottom = -1e9f;
        for (auto& it : items) {
            it.dy -= ref;
            top = std::min(top, it.dy - it.box->height);
            bottom = std::max(bottom, it.dy + it.box->depth);
            box->children.push_back(it);
        }
        box->width = width;
        box->height = -top;
        box->depth = bottom;
        return box;
    }
};

MathBoxPtr Shifted(const MathBoxPtr& child, float dy) {
    auto box = MathBox::MakeList();
    box->Add(child, 0.f, dy);
    box->width = child->width;
    box->height = child->height - dy;
    box->depth = child->depth + dy;
    return box;
}

MathBoxPtr Wrap(const MathBoxPtr& child) {
    auto box = MathBox::MakeList();
    box->Add(child, 0.f, 0.f);
    box->width = child->width;
    box->height = child->height;
    box->depth = child->depth;
    return box;
}

bool IsSingleChar(const MathList& list, const MathAtom*& atom) {
    if (list.size() == 1 && list.front()->kind == MathAtomKind::Char) { atom = list.front().get(); return true; }
    if (list.size() == 1 && list.front()->kind == MathAtomKind::Row) return IsSingleChar(list.front()->body, atom);
    return false;
}

} // namespace

// =============================================================================
// Impl
// =============================================================================

struct UltraCanvasMathLayout::Impl {
    const UltraCanvasMathFont& font;
    MathLayoutOptions options;
    std::vector<MathDiagnostic>& diags;
    float upem;

    Impl(const UltraCanvasMathFont& f, const MathLayoutOptions& o, std::vector<MathDiagnostic>& d)
        : font(f), options(o), diags(d), upem(static_cast<float>(std::max(1, f.GetUnitsPerEm()))) {}

    // ----- sizes and constants -----
    float StyleFactor(const MathStyle& s) const {
        switch (s.kind) {
            case MathStyleKind::Script:
                return std::max(0.3f, font.GetConstant(MathConstant::ScriptPercentScaleDown) / 100.f);
            case MathStyleKind::ScriptScript:
                return std::max(0.2f, font.GetConstant(MathConstant::ScriptScriptPercentScaleDown) / 100.f);
            default:
                return 1.f;
        }
    }
    float Size(const Env& env) const { return options.fontSize * env.userScale * StyleFactor(env.style); }
    float Em(const Env& env, float em) const { return em * Size(env); }
    float C(MathConstant c, const Env& env) const { return font.GetConstant(c) * Size(env) / upem; }
    float Units(int fontUnits, const Env& env) const { return fontUnits * Size(env) / upem; }
    float Axis(const Env& env) const { return C(MathConstant::AxisHeight, env); }
    float RuleThickness(const Env& env) const {
        const float t = C(MathConstant::FractionRuleThickness, env);
        return t > 0 ? t : Em(env, 0.04f);
    }

    // ----- glyph boxes -----
    MathBoxPtr GlyphBox(uint32_t gid, const Env& env) const {
        auto box = std::make_shared<MathBox>();
        box->type = MathBoxType::Glyph;
        box->font = &font;
        box->glyph = gid;
        box->fontSize = Size(env);
        MathGlyphMetrics m;
        if (font.GetGlyphMetrics(gid, m)) {
            box->width = Units(m.advance, env);
            box->height = Units(m.Height(), env);
            box->depth = Units(m.Depth(), env);
        }
        box->italicsCorrection = Units(font.GetItalicsCorrection(gid), env);
        box->topAccentAttachment = font.HasTopAccentAttachment(gid) ? Units(font.GetTopAccentAttachment(gid), env) : -1.f;
        return box;
    }

    // The glyph for a character in a font style, with the unicode-math
    // alphabet mapping; falls back to the plain code point.
    uint32_t GlyphFor(char32_t cp, const MathFontStyle& style, bool textChar) const {
        MathFontStyle s = style;
        if (textChar && s.shape == MathFontShape::Auto) s.shape = MathFontShape::Upright;
        const char32_t mapped = MapMathAlphanumeric(cp, s);
        uint32_t gid = font.GetGlyphIndex(mapped);
        if (gid == 0 && mapped != cp) gid = font.GetGlyphIndex(cp);
        return gid;
    }

    MathBoxPtr PlaceholderBox(const Env& env) const {
        // A visible "missing glyph" box, like TeX's tofu.
        auto box = MathBox::MakeList();
        const float w = Em(env, 0.5f), h = Em(env, 0.65f);
        const float t = Em(env, 0.04f);
        box->Add(MathBox::MakeRule(w, t, 0), 0, -h + t);
        box->Add(MathBox::MakeRule(w, 0, t), 0, 0);
        box->Add(MathBox::MakeRule(t, h, 0), 0, 0);
        box->Add(MathBox::MakeRule(t, h, 0), w - t, 0);
        box->width = w + Em(env, 0.05f);
        box->height = h;
        return box;
    }

    // A text run the math font lacks, measured by the host.
    MathBoxPtr FallbackTextBox(const std::string& utf8, const MathFontStyle& style, const Env& env) const {
        float w = 0, a = 0, d = 0;
        if (options.textFallback && options.textFallback->MeasureText(utf8, style, Size(env), w, a, d)) {
            auto box = std::make_shared<MathBox>();
            box->type = MathBoxType::Text;
            box->text = utf8;
            box->textStyle = style;
            box->fontSize = Size(env);
            box->width = w; box->height = a; box->depth = d;
            return box;
        }
        // No host: one placeholder per character.
        HBuilder hb;
        size_t p = 0;
        while (p < utf8.size()) { DecodeUtf8(utf8, p); hb.Add(PlaceholderBox(env)); }
        return hb.Finish();
    }

    MathBoxPtr CharBox(const MathAtom& atom, const Env& env) const {
        if (atom.codepoint == ' ' || atom.codepoint == 0x00A0) {
            const uint32_t sp = font.GetGlyphIndex(' ');
            MathGlyphMetrics m;
            float w = Em(env, 0.333f);
            if (sp && font.GetGlyphMetrics(sp, m) && m.advance > 0) w = Units(m.advance, env);
            return MathBox::MakeKern(w);
        }
        const uint32_t gid = GlyphFor(atom.codepoint, atom.fontStyle, atom.isTextChar);
        if (gid == 0) {
            std::string s;
            AppendUtf8(s, atom.codepoint);
            return FallbackTextBox(s, atom.fontStyle, env);
        }
        auto box = GlyphBox(gid, env);
        box->sourceStart = atom.sourceStart;
        box->sourceEnd = atom.sourceEnd;
        return box;
    }

    // ----- stretching -----
    // The smallest glyph (base, then variants) whose extent in `direction`
    // reaches `target` pixels; null when only an assembly can.
    MathBoxPtr VariantBox(uint32_t gid, float target, MathStretchDirection dir, const Env& env, bool& exact) const {
        exact = true;
        MathGlyphMetrics m;
        const float scale = Size(env) / upem;
        if (font.GetGlyphMetrics(gid, m)) {
            const float extent = dir == MathStretchDirection::Vertical ? (m.Height() + m.Depth()) * scale : m.advance * scale;
            if (extent >= target) return GlyphBox(gid, env);
        }
        MathBoxPtr best;
        for (const auto& v : font.GetGlyphVariants(gid, dir)) {
            MathGlyphMetrics vm;
            if (!font.GetGlyphMetrics(v.glyph, vm)) continue;
            const float extent = dir == MathStretchDirection::Vertical ? (vm.Height() + vm.Depth()) * scale : vm.advance * scale;
            best = GlyphBox(v.glyph, env);
            if (extent >= target) return best;
        }
        exact = false;
        return best;   // the largest available, or null
    }

    // A vertical or horizontal assembly at least `target` long.
    MathBoxPtr AssemblyBox(uint32_t gid, float target, MathStretchDirection dir, const Env& env) const {
        MathGlyphAssembly asm_;
        if (!font.GetGlyphAssembly(gid, dir, asm_)) return nullptr;
        const float scale = Size(env) / upem;
        const float minOverlap = font.GetMinConnectorOverlap() * scale;
        // Parts with extenders repeated n times.
        struct Part { uint32_t glyph; float advance; float startConn, endConn; bool ext; };
        std::vector<Part> base;
        for (const auto& p : asm_.parts) {
            MathGlyphMetrics pm;
            font.GetGlyphMetrics(p.glyph, pm);
            const float adv = dir == MathStretchDirection::Vertical ? (pm.Height() + pm.Depth()) * scale : pm.advance * scale;
            base.push_back({p.glyph, adv > 0 ? adv : p.fullAdvance * scale, p.startConnectorLength * scale, p.endConnectorLength * scale, p.isExtender});
        }
        if (base.empty()) return nullptr;
        std::vector<Part> parts;
        float overlap = minOverlap;
        for (int n = 0; n <= 64; ++n) {
            parts.clear();
            for (const auto& p : base) {
                if (p.ext) { for (int k = 0; k < n; ++k) parts.push_back(p); }
                else parts.push_back(p);
            }
            if (parts.empty()) return nullptr;
            float total = 0.f;
            for (const auto& p : parts) total += p.advance;
            // Maximum overlap allowed between neighbours.
            float maxOverlap = 1e9f;
            for (size_t i = 0; i + 1 < parts.size(); ++i) maxOverlap = std::min(maxOverlap, std::min(parts[i].endConn, parts[i + 1].startConn));
            if (parts.size() == 1) maxOverlap = 0.f;
            const float minTotal = total - (parts.size() - 1) * std::max(minOverlap, std::min(maxOverlap, 1e9f));
            const float maxTotal = total - (parts.size() - 1) * minOverlap;
            (void)minTotal;
            if (maxTotal >= target || n == 64) {
                // Distribute: overlap as large as needed, at least the minimum.
                const float excess = total - target;
                overlap = parts.size() > 1 ? std::max(minOverlap, std::min(excess / (parts.size() - 1), std::max(maxOverlap, minOverlap))) : 0.f;
                if (parts.size() > 1 && total - (parts.size() - 1) * overlap < target - 0.01f && n < 64) continue;
                break;
            }
        }
        auto box = MathBox::MakeList();
        if (dir == MathStretchDirection::Vertical) {
            // Parts are listed bottom to top. Stack from the bottom at depth 0
            // (the caller re-centres the result).
            float cursor = 0.f;   // y of the current bottom edge (positive down); start at 0
            float top = 0.f;
            for (size_t i = 0; i < parts.size(); ++i) {
                auto g = GlyphBox(parts[i].glyph, env);
                // place ink bottom at cursor
                const float dy = cursor - g->depth;
                box->Add(g, 0.f, dy);
                box->width = std::max(box->width, g->width);
                top = dy - g->height;
                cursor = top + overlap;
            }
            box->height = -top;
            box->depth = 0.f;
        } else {
            float x = 0.f;
            for (size_t i = 0; i < parts.size(); ++i) {
                auto g = GlyphBox(parts[i].glyph, env);
                box->Add(g, x, 0.f);
                box->height = std::max(box->height, g->height);
                box->depth = std::max(box->depth, g->depth);
                x += g->width - overlap;
            }
            box->width = x + overlap;
        }
        return box;
    }

    // A delimiter of at least `target` total height, centred on the axis
    // (rule 19). A null delimiter (cp 0) is a kern of \nulldelimiterspace.
    MathBoxPtr Delimiter(uint32_t cp, float target, const Env& env, bool centre = true) const {
        if (cp == 0) return MathBox::MakeKern(Em(env, 0.12f));
        const uint32_t gid = font.GetGlyphIndex(cp);
        if (gid == 0) {
            MathAtom tmp; tmp.codepoint = cp;
            return CharBox(tmp, env);
        }
        bool exact = false;
        MathBoxPtr box = VariantBox(gid, target, MathStretchDirection::Vertical, env, exact);
        if (!exact) {
            if (MathBoxPtr a = AssemblyBox(gid, target, MathStretchDirection::Vertical, env)) box = a;
        }
        if (!box) box = GlyphBox(gid, env);
        if (!centre) return box;
        const float shift = (box->height - box->depth) / 2.f - Axis(env);
        return Shifted(box, shift);
    }

    // A horizontally stretched glyph (accents, braces, arrows) at least `target` wide.
    MathBoxPtr Stretched(uint32_t cp, float target, const Env& env) const {
        const uint32_t gid = font.GetGlyphIndex(cp);
        if (gid == 0) { MathAtom tmp; tmp.codepoint = cp; return CharBox(tmp, env); }
        bool exact = false;
        MathBoxPtr box = VariantBox(gid, target, MathStretchDirection::Horizontal, env, exact);
        if (!exact) {
            if (MathBoxPtr a = AssemblyBox(gid, target, MathStretchDirection::Horizontal, env)) box = a;
        }
        if (!box) box = GlyphBox(gid, env);
        return box;
    }

    // ----- lists -----
    struct Item {
        const MathAtom* atom;
        Env env;
        MathAtomClass cls;
        MathBoxPtr box;
    };

    // Flattens Style / Color scopes into the row so spacing crosses them.
    void Collect(const MathList& list, const Env& env, std::vector<Item>& items) {
        for (const auto& a : list) {
            if (!a) continue;
            if (a->kind == MathAtomKind::Style) {
                Env e = env;
                if (a->hasStyle) e.style = a->style;
                if (a->sizeScale > 0.f) e.userScale = a->sizeScale;
                Collect(a->body, e, items);
                continue;
            }
            if (a->kind == MathAtomKind::Color) {
                Env e = env;
                e.color = a->color;
                Collect(a->body, e, items);
                continue;
            }
            items.push_back({a.get(), env, a->atomClass, nullptr});
        }
    }

    MathBoxPtr LayoutList(const MathList& list, const Env& env) {
        std::vector<Item> items;
        Collect(list, env, items);
        return LayoutItems(items, env);
    }

    MathBoxPtr LayoutItems(std::vector<Item>& items, const Env& env) {
        // Rules 5 and 6: a Bin that cannot be binary becomes Ord.
        const bool spacing = !env.textRun;
        if (spacing) {
            MathAtomClass prev = MathAtomClass::None;
            for (size_t i = 0; i < items.size(); ++i) {
                if (items[i].cls == MathAtomClass::None) continue;
                if (items[i].cls == MathAtomClass::Bin) {
                    const bool badPrev = prev == MathAtomClass::None || prev == MathAtomClass::Bin || prev == MathAtomClass::Op ||
                                         prev == MathAtomClass::Rel || prev == MathAtomClass::Open || prev == MathAtomClass::Punct;
                    MathAtomClass next = MathAtomClass::None;
                    for (size_t j = i + 1; j < items.size(); ++j) if (items[j].cls != MathAtomClass::None) { next = items[j].cls; break; }
                    const bool badNext = next == MathAtomClass::None || next == MathAtomClass::Rel || next == MathAtomClass::Close || next == MathAtomClass::Punct;
                    if (badPrev || badNext) items[i].cls = MathAtomClass::Ord;
                }
                prev = items[i].cls;
            }
        }
        // Boxes
        for (auto& it : items) {
            it.box = LayoutAtom(*it.atom, it.env);
            if (it.box && it.env.color != kMathColorNone && it.env.color != env.color) {
                auto c = std::make_shared<MathBox>();
                c->type = MathBoxType::Color;
                c->color = it.env.color;
                c->Add(it.box, 0, 0);
                c->width = it.box->width; c->height = it.box->height; c->depth = it.box->depth;
                it.box = c;
            }
        }
        // Assemble with spacing and italic corrections (rules 17, 20).
        HBuilder hb;
        MathAtomClass prev = MathAtomClass::None;
        for (size_t i = 0; i < items.size(); ++i) {
            const Item& it = items[i];
            if (!it.box) continue;
            if (spacing && it.cls != MathAtomClass::None && prev != MathAtomClass::None) {
                int sp = kSpacing[static_cast<int>(prev)][static_cast<int>(it.cls)];
                if (sp < 0) sp = it.env.style.IsScript() ? 0 : -sp;
                const float mu = Size(it.env) / 18.f;
                if (sp == 1) hb.Kern(3 * mu);
                else if (sp == 2) hb.Kern(4 * mu);
                else if (sp == 3) hb.Kern(5 * mu);
            }
            hb.Add(it.box);
            // Italic correction after a character (rule 17), unless it carries a subscript.
            if (!env.textRun && it.box->type == MathBoxType::Glyph && it.box->italicsCorrection > 0.f &&
                it.atom->kind == MathAtomKind::Char && !it.atom->isTextChar) {
                hb.Kern(it.box->italicsCorrection);
            }
            if (it.cls != MathAtomClass::None) prev = it.cls;
        }
        return hb.Finish();
    }

    // ----- atoms -----
    MathBoxPtr LayoutAtom(const MathAtom& atom, const Env& env) {
        MathBoxPtr box;
        switch (atom.kind) {
            case MathAtomKind::Char:
                if (atom.opIsLargeOperator) box = LargeOperatorBox(atom, env);
                else box = CharBox(atom, env);
                break;
            case MathAtomKind::Row:       box = LayoutList(atom.body, env); break;
            case MathAtomKind::Space:     box = MathBox::MakeKern(atom.spaceIsMu ? atom.spaceMu * Size(env) / 18.f : Em(env, atom.spaceEm)); break;
            case MathAtomKind::Fraction:  box = LayoutFraction(atom, env); break;
            case MathAtomKind::Radical:   box = LayoutRadical(atom, env); break;
            case MathAtomKind::Scripts:   box = LayoutScripts(atom, env); break;
            case MathAtomKind::Accent:    box = LayoutAccent(atom, env); break;
            case MathAtomKind::OverUnder: box = LayoutOverUnder(atom, env); break;
            case MathAtomKind::Fence:     box = LayoutFence(atom, env); break;
            case MathAtomKind::BigDelim:  box = LayoutBigDelim(atom, env); break;
            case MathAtomKind::Style:
            case MathAtomKind::Color: {
                Env e = env;
                if (atom.kind == MathAtomKind::Style) { if (atom.hasStyle) e.style = atom.style; if (atom.sizeScale > 0) e.userScale = atom.sizeScale; }
                else e.color = atom.color;
                box = LayoutList(atom.body, e);
                if (atom.kind == MathAtomKind::Color) {
                    auto c = std::make_shared<MathBox>();
                    c->type = MathBoxType::Color; c->color = atom.color;
                    c->Add(box, 0, 0); c->width = box->width; c->height = box->height; c->depth = box->depth;
                    box = c;
                }
                break;
            }
            case MathAtomKind::Phantom: {
                MathBoxPtr inner = LayoutList(atom.body, env);
                box = MathBox::MakeList();
                box->width = atom.phantomWidth ? inner->width : 0.f;
                box->height = atom.phantomHeight ? inner->height : 0.f;
                box->depth = atom.phantomHeight ? inner->depth : 0.f;
                break;
            }
            case MathAtomKind::Smash: {
                MathBoxPtr inner = LayoutList(atom.body, env);
                box = Wrap(inner);
                if (atom.phantomHeight) box->height = 0.f;
                if (atom.phantomWidth) box->depth = 0.f;
                break;
            }
            case MathAtomKind::Lap: {
                MathBoxPtr inner = LayoutList(atom.body, env);
                box = MathBox::MakeList();
                const float dx = atom.lapKind == MathLapKind::Right ? 0.f : atom.lapKind == MathLapKind::Left ? -inner->width : -inner->width / 2.f;
                box->Add(inner, dx, 0.f);
                box->width = 0.f; box->height = inner->height; box->depth = inner->depth;
                break;
            }
            case MathAtomKind::Box:       box = LayoutBoxAtom(atom, env); break;
            case MathAtomKind::Cancel: {
                MathBoxPtr inner = LayoutList(atom.body, env);
                box = std::make_shared<MathBox>();
                box->type = MathBoxType::Cancel;
                box->cancelKind = atom.cancelKind;
                box->lineWidth = Em(env, 0.05f);
                box->Add(inner, 0, 0);
                box->width = inner->width; box->height = inner->height; box->depth = inner->depth;
                break;
            }
            case MathAtomKind::Text: {
                Env e = env; e.textRun = true;
                box = LayoutText(atom.body, e);
                break;
            }
            case MathAtomKind::Array:     box = LayoutArray(atom, env); break;
            case MathAtomKind::Sideset:   box = LayoutSideset(atom, env); break;
            case MathAtomKind::Transform: box = LayoutTransform(atom, env); break;
            case MathAtomKind::Rule:
                box = MathBox::MakeRule(Em(env, atom.ruleWidthEm), Em(env, atom.ruleHeightEm), Em(env, atom.ruleDepthEm));
                break;
            case MathAtomKind::Not: {
                MathBoxPtr inner = LayoutList(atom.body, env);
                box = Wrap(inner);
                // Overlay a combining long solidus (or a plain slash) centred on the body.
                const uint32_t overlay = font.GetGlyphIndex(0x0338);
                const uint32_t slash = overlay ? overlay : font.GetGlyphIndex('/');
                if (slash) {
                    auto s = GlyphBox(slash, env);
                    MathGlyphMetrics sm;
                    float inkCentre = s->width / 2.f;
                    if (font.GetGlyphMetrics(slash, sm)) inkCentre = (sm.xMin + sm.xMax) / 2.f * Size(env) / upem;
                    box->Add(s, inner->width / 2.f - inkCentre, 0.f);
                    box->height = std::max(box->height, s->height);
                    box->depth = std::max(box->depth, s->depth);
                }
                break;
            }
            case MathAtomKind::LongDiv:   box = LayoutLongDiv(atom, env); break;
            case MathAtomKind::Error: {
                box = std::make_shared<MathBox>();
                box->type = MathBoxType::Error;
                box->text = atom.text;
                box->fontSize = Size(env);
                size_t n = 0; for (size_t p = 0; p < atom.text.size();) { DecodeUtf8(atom.text, p); ++n; }
                box->width = Em(env, 0.55f) * static_cast<float>(n);
                box->height = Em(env, 0.7f);
                box->depth = Em(env, 0.2f);
                break;
            }
        }
        if (box) {
            if (box->sourceStart < 0) { box->sourceStart = atom.sourceStart; box->sourceEnd = atom.sourceEnd; }
        }
        return box;
    }

    // ----- text runs -----
    MathBoxPtr LayoutText(const MathList& list, const Env& env) {
        // Consecutive characters the font lacks are measured as one run.
        std::vector<Item> items;
        Collect(list, env, items);
        HBuilder hb;
        std::string pendingRun;
        MathFontStyle pendingStyle;
        Env pendingEnv = env;
        auto flush = [&]() {
            if (pendingRun.empty()) return;
            hb.Add(FallbackTextBox(pendingRun, pendingStyle, pendingEnv));
            pendingRun.clear();
        };
        for (auto& it : items) {
            const MathAtom& a = *it.atom;
            if (a.kind == MathAtomKind::Char && a.codepoint != ' ' && a.codepoint != 0x00A0 &&
                GlyphFor(a.codepoint, a.fontStyle, a.isTextChar) == 0) {
                if (!pendingRun.empty() && !(pendingStyle == a.fontStyle)) flush();
                pendingStyle = a.fontStyle;
                pendingEnv = it.env;
                AppendUtf8(pendingRun, a.codepoint);
                continue;
            }
            if (a.kind == MathAtomKind::Char && (a.codepoint == ' ' || a.codepoint == 0x00A0) && !pendingRun.empty()) {
                pendingRun.push_back(' ');
                continue;
            }
            flush();
            Env e = it.env;
            e.textRun = a.kind == MathAtomKind::Row ? false : true;   // $...$ inside text is math again
            if (a.kind == MathAtomKind::Row) e.style = env.style.kind == MathStyleKind::Display ? MathStyle::Text() : env.style;
            MathBoxPtr b = LayoutAtom(a, e);
            if (b && it.env.color != kMathColorNone && it.env.color != env.color) {
                auto c = std::make_shared<MathBox>();
                c->type = MathBoxType::Color; c->color = it.env.color;
                c->Add(b, 0, 0); c->width = b->width; c->height = b->height; c->depth = b->depth;
                b = c;
            }
            hb.Add(b);
        }
        flush();
        return hb.Finish();
    }

    // ----- fractions (rule 15) -----
    MathBoxPtr LayoutFraction(const MathAtom& atom, const Env& env) {
        Env base = env;
        if (atom.fracDisplayStyle) base.style = MathStyle{MathStyleKind::Display, env.style.cramped};
        if (atom.fracTextStyle) base.style = MathStyle{MathStyleKind::Text, env.style.cramped};
        Env numEnv = base; numEnv.style = base.style.Numerator();
        Env denEnv = base; denEnv.style = base.style.Denominator();
        if (atom.fracDisplayStyle) { numEnv.style = MathStyle{MathStyleKind::Text, false}; denEnv.style = MathStyle{MathStyleKind::Text, true}; }
        MathBoxPtr num = LayoutList(atom.numerator, numEnv);
        MathBoxPtr den = LayoutList(atom.denominator, denEnv);

        const bool display = base.style.IsDisplay();
        const float theta = atom.ruleThicknessEm < 0.f ? RuleThickness(base) : Em(base, atom.ruleThicknessEm);
        const float axis = Axis(base);
        float u, v;
        if (theta > 0.f) {
            u = display ? C(MathConstant::FractionNumeratorDisplayStyleShiftUp, base) : C(MathConstant::FractionNumeratorShiftUp, base);
            v = display ? C(MathConstant::FractionDenominatorDisplayStyleShiftDown, base) : C(MathConstant::FractionDenominatorShiftDown, base);
            const float gapNum = display ? C(MathConstant::FractionNumDisplayStyleGapMin, base) : C(MathConstant::FractionNumeratorGapMin, base);
            const float gapDen = display ? C(MathConstant::FractionDenomDisplayStyleGapMin, base) : C(MathConstant::FractionDenominatorGapMin, base);
            const float numBottom = u - num->depth;
            const float ruleTop = axis + theta / 2.f;
            if (numBottom - ruleTop < gapNum) u += gapNum - (numBottom - ruleTop);
            const float denTop = den->height - v;
            const float ruleBottom = axis - theta / 2.f;
            if (ruleBottom - denTop < gapDen) v += gapDen - (ruleBottom - denTop);
        } else {
            u = display ? C(MathConstant::StackTopDisplayStyleShiftUp, base) : C(MathConstant::StackTopShiftUp, base);
            v = display ? C(MathConstant::StackBottomDisplayStyleShiftDown, base) : C(MathConstant::StackBottomShiftDown, base);
            const float gapMin = display ? C(MathConstant::StackDisplayStyleGapMin, base) : C(MathConstant::StackGapMin, base);
            const float gap = (u - num->depth) - (den->height - v);
            if (gap < gapMin) { u += (gapMin - gap) / 2.f; v += (gapMin - gap) / 2.f; }
        }
        const float w = std::max(num->width, den->width);
        auto body = MathBox::MakeList();
        body->Add(num, (w - num->width) / 2.f, -u);
        body->Add(den, (w - den->width) / 2.f, v);
        if (theta > 0.f) body->Add(MathBox::MakeRule(w, theta / 2.f, theta / 2.f), 0.f, -axis);
        body->width = w;
        body->height = u + num->height;
        body->depth = v + den->depth;

        if (atom.fracLeftDelim == 0 && atom.fracRightDelim == 0) {
            // TeX adds a thin kern (\nulldelimiterspace) on both sides.
            HBuilder hb;
            hb.Kern(Em(base, 0.12f));
            hb.Add(body);
            hb.Kern(Em(base, 0.12f));
            return hb.Finish();
        }
        const float delimSize = display ? C(MathConstant::DelimitedSubFormulaMinHeight, base) : Em(base, 1.01f);
        HBuilder hb;
        hb.Add(Delimiter(atom.fracLeftDelim, delimSize, base));
        hb.Add(body);
        hb.Add(Delimiter(atom.fracRightDelim, delimSize, base));
        return hb.Finish();
    }

    // ----- radicals (rule 11) -----
    MathBoxPtr LayoutRadical(const MathAtom& atom, const Env& env) {
        Env inner = env; inner.style = env.style.Cramped();
        MathBoxPtr body = LayoutList(atom.body, inner);
        const float theta = C(MathConstant::RadicalRuleThickness, env) > 0 ? C(MathConstant::RadicalRuleThickness, env) : RuleThickness(env);
        float gap = env.style.IsDisplay() ? C(MathConstant::RadicalDisplayStyleVerticalGap, env) : C(MathConstant::RadicalVerticalGap, env);
        const float extra = C(MathConstant::RadicalExtraAscender, env);
        const float target = body->height + body->depth + gap + theta;
        const uint32_t surdGid = font.GetGlyphIndex(0x221A);
        MathBoxPtr surd;
        {
            bool exact = false;
            surd = surdGid ? VariantBox(surdGid, target, MathStretchDirection::Vertical, env, exact) : nullptr;
            if (surdGid && !exact) { if (MathBoxPtr a = AssemblyBox(surdGid, target, MathStretchDirection::Vertical, env)) surd = a; }
            if (!surd) { surd = MathBox::MakeRule(theta, target, 0); }
        }
        // Excess of the surd over what is needed goes to the gap (rule 11).
        const float surdTotal = surd->height + surd->depth;
        if (surdTotal > target) gap += (surdTotal - target) / 2.f;
        // The rule's top is `extra` below the top of the result; the surd's
        // top is aligned with the rule's top.
        const float ruleTop = body->height + gap + theta;   // above baseline
        const float surdDy = -(ruleTop - surd->height);     // baseline offset so surd top = ruleTop
        auto box = MathBox::MakeList();
        float x = 0.f;
        // Degree
        if (!atom.degree.empty()) {
            Env degEnv = env; degEnv.style = MathStyle{MathStyleKind::ScriptScript, false};
            MathBoxPtr deg = LayoutList(atom.degree, degEnv);
            const float kernBefore = C(MathConstant::RadicalKernBeforeDegree, env);
            const float kernAfter = C(MathConstant::RadicalKernAfterDegree, env);
            const float raise = font.GetConstant(MathConstant::RadicalDegreeBottomRaisePercent) / 100.f;
            const float surdBottom = surdDy + surd->depth;
            const float degBaseline = surdBottom - raise * (surd->height + surd->depth) - deg->depth;
            box->Add(deg, kernBefore, degBaseline);
            x = std::max(0.f, kernBefore + deg->width + kernAfter);
            box->height = std::max(box->height, -(degBaseline - deg->height));
        }
        box->Add(surd, x, surdDy);
        x += surd->width;
        box->Add(MathBox::MakeRule(body->width, theta, 0.f), x, -(ruleTop - theta));
        box->Add(body, x, 0.f);
        box->width = x + body->width;
        box->height = std::max({box->height, ruleTop + extra, surd->height - surdDy});
        box->depth = std::max(body->depth, surd->depth + surdDy);
        return box;
    }

    // ----- scripts (rules 13, 18) -----
    static bool IsIntegral(char32_t cp) {
        return (cp >= 0x222B && cp <= 0x2233) || (cp >= 0x2A0B && cp <= 0x2A1C);
    }

    bool UsesLimits(const MathAtom& atom, const Env& env) const {
        if (atom.limits == MathLimits::Limits) return true;
        if (atom.limits == MathLimits::NoLimits) return false;
        if (atom.atomClass != MathAtomClass::Op) return false;
        // Integrals take \nolimits by default, like LaTeX's \int.
        const MathAtom* single = nullptr;
        if (atom.nucleus) {
            if (atom.nucleus->kind == MathAtomKind::Char) single = atom.nucleus.get();
            else if (atom.nucleus->kind == MathAtomKind::Row) IsSingleChar(atom.nucleus->body, single);
        }
        if (single && IsIntegral(single->codepoint)) return false;
        return env.style.IsDisplay();
    }

    // Rule 13: a large operator takes its display-size variant in display
    // style and is centred on the axis in every style.
    MathBoxPtr LargeOperatorBox(const MathAtom& single, const Env& env) {
        const uint32_t gid = GlyphFor(single.codepoint, single.fontStyle, false);
        MathBoxPtr box;
        if (env.style.IsDisplay() && gid) {
            const float minH = C(MathConstant::DisplayOperatorMinHeight, env);
            bool exact = false;
            box = VariantBox(gid, minH, MathStretchDirection::Vertical, env, exact);
            // The base glyph already reaches the size: still prefer the next variant, like TeX.
            if (box && box->glyph == gid) {
                const auto variants = font.GetGlyphVariants(gid, MathStretchDirection::Vertical);
                for (const auto& v : variants) if (v.glyph != gid) { box = GlyphBox(v.glyph, env); break; }
            }
        }
        if (!box) box = CharBox(single, env);
        const float shift = (box->height - box->depth) / 2.f - Axis(env);
        return Shifted(box, shift);
    }

    MathBoxPtr NucleusBox(const MathAtom& atom, const Env& env, const MathAtom*& glyphAtom) {
        glyphAtom = nullptr;
        if (!atom.nucleus) return MathBox::MakeList();
        const MathAtom& n = *atom.nucleus;
        const MathAtom* single = nullptr;
        if (n.kind == MathAtomKind::Char) single = &n;
        else if (n.kind == MathAtomKind::Row && IsSingleChar(n.body, single)) {}
        if (single && single->opIsLargeOperator) {
            glyphAtom = single;
            return LargeOperatorBox(*single, env);
        }
        MathBoxPtr box = LayoutAtom(n, env);
        if (single && box && box->type == MathBoxType::Glyph) glyphAtom = single;
        return box;
    }

    MathBoxPtr LayoutScripts(const MathAtom& atom, const Env& env) {
        const MathAtom* glyphAtom = nullptr;
        MathBoxPtr nucleus = NucleusBox(atom, env, glyphAtom);
        if (!atom.hasSub && !atom.hasSup) {
            // Italic correction is appended by the row for plain glyphs; for
            // a large operator nucleus keep it as a kern.
            return nucleus;
        }
        Env supEnv = env; supEnv.style = env.style.Sup();
        Env subEnv = env; subEnv.style = env.style.Sub();
        MathBoxPtr sup = atom.hasSup ? LayoutList(atom.superscript, supEnv) : nullptr;
        MathBoxPtr sub = atom.hasSub ? LayoutList(atom.subscript, subEnv) : nullptr;

        if (UsesLimits(atom, env)) {
            // Rule 13a: limits above and below, centred, with italic correction.
            float ic = 0.f;
            if (glyphAtom) {
                const uint32_t gid = GlyphFor(glyphAtom->codepoint, glyphAtom->fontStyle, false);
                // the nucleus box may be a Shifted wrapper: find the glyph box
                MathBoxPtr g = nucleus;
                while (g && g->type == MathBoxType::List && g->children.size() == 1) g = g->children.front().box;
                if (g && g->type == MathBoxType::Glyph) ic = g->italicsCorrection;
                (void)gid;
            }
            const float w = std::max({nucleus->width, sup ? sup->width : 0.f, sub ? sub->width : 0.f});
            auto box = MathBox::MakeList();
            box->Add(nucleus, (w - nucleus->width) / 2.f, 0.f);
            box->width = w;
            box->height = nucleus->height;
            box->depth = nucleus->depth;
            if (sup) {
                const float gap = std::max(C(MathConstant::UpperLimitGapMin, env),
                                           C(MathConstant::UpperLimitBaselineRiseMin, env) - sup->depth);
                const float dy = -(nucleus->height + gap + sup->depth);
                box->Add(sup, (w - sup->width) / 2.f + ic / 2.f, dy);
                box->height = -dy + sup->height;
            }
            if (sub) {
                const float gap = std::max(C(MathConstant::LowerLimitGapMin, env),
                                           C(MathConstant::LowerLimitBaselineDropMin, env) - sub->height);
                const float dy = nucleus->depth + gap + sub->height;
                box->Add(sub, (w - sub->width) / 2.f - ic / 2.f, dy);
                box->depth = dy + sub->depth;
            }
            // Italic correction of a large operator without limits is folded
            // into the width by the row; with limits the width is the max.
            return box;
        }

        // Rule 18.
        float u = 0.f, v = 0.f, ic = 0.f;
        MathBoxPtr glyphBox = nucleus;
        while (glyphBox && glyphBox->type == MathBoxType::List && glyphBox->children.size() == 1) glyphBox = glyphBox->children.front().box;
        const bool isGlyph = glyphBox && glyphBox->type == MathBoxType::Glyph && glyphAtom;
        if (isGlyph) {
            ic = glyphBox->italicsCorrection;
        } else {
            u = nucleus->height - C(MathConstant::SuperscriptBaselineDropMax, env);
            v = nucleus->depth + C(MathConstant::SubscriptBaselineDropMin, env);
        }
        // Math kerning (rule 18, OpenType extension)
        float supKern = 0.f, subKern = 0.f;
        if (sup) {
            u = std::max({u, env.style.cramped ? C(MathConstant::SuperscriptShiftUpCramped, env) : C(MathConstant::SuperscriptShiftUp, env),
                          sup->depth + C(MathConstant::SuperscriptBottomMin, env)});
        }
        if (sub && !sup) {
            v = std::max({v, C(MathConstant::SubscriptShiftDown, env), sub->height - C(MathConstant::SubscriptTopMax, env)});
        }
        if (sub && sup) {
            v = std::max(v, C(MathConstant::SubscriptShiftDown, env));
            const float gapMin = C(MathConstant::SubSuperscriptGapMin, env);
            const float gap = (u - sup->depth) - (sub->height - v);
            if (gap < gapMin) v += gapMin - gap;
            const float psi = C(MathConstant::SuperscriptBottomMaxWithSubscript, env) - (u - sup->depth);
            if (psi > 0.f) { u += psi; v -= psi; }
        }
        if (isGlyph && glyphAtom) {
            const uint32_t gid = glyphBox->glyph;
            if (sup) {
                const float scale = Size(env) / upem;
                const int h = static_cast<int>((u - sup->depth) / scale);
                supKern = Units(font.GetMathKernValue(gid, MathKernCorner::TopRight, h), env);
                (void)supKern;
            }
            if (sub) {
                const float scale = Size(env) / upem;
                const int h = static_cast<int>((sub->height - v) / scale);
                subKern = Units(font.GetMathKernValue(gid, MathKernCorner::BottomRight, h), env);
            }
        }
        auto box = MathBox::MakeList();
        box->Add(nucleus, 0.f, 0.f);
        box->width = nucleus->width;
        box->height = nucleus->height;
        box->depth = nucleus->depth;
        const float spaceAfter = C(MathConstant::SpaceAfterScript, env);
        float right = nucleus->width;
        if (sup) {
            const float x = nucleus->width + ic + supKern;
            box->Add(sup, x, -u);
            right = std::max(right, x + sup->width);
            box->height = std::max(box->height, u + sup->height);
            box->depth = std::max(box->depth, sup->depth - u);
        }
        if (sub) {
            const float x = nucleus->width + subKern;
            box->Add(sub, x, v);
            right = std::max(right, x + sub->width);
            box->depth = std::max(box->depth, v + sub->depth);
            box->height = std::max(box->height, sub->height - v);
        }
        box->width = right + spaceAfter;
        return box;
    }

    // ----- accents (rule 12) -----
    MathBoxPtr LayoutAccent(const MathAtom& atom, const Env& env) {
        Env inner = env; inner.style = env.style.Cramped();
        MathBoxPtr body = LayoutList(atom.body, inner);
        const MathAtom* single = nullptr;
        float skew = body->width / 2.f;
        if (IsSingleChar(atom.body, single)) {
            MathBoxPtr g = body;
            while (g && g->type == MathBoxType::List && !g->children.empty()) g = g->children.front().box;
            if (g && g->type == MathBoxType::Glyph && g->topAccentAttachment >= 0.f) skew = g->topAccentAttachment;
        }
        const uint32_t accentGid = font.GetGlyphIndex(atom.codepoint);
        if (accentGid == 0) return body;
        MathBoxPtr accent;
        if (atom.accentStretchy) {
            // The widest variant that is not wider than the base.
            accent = GlyphBox(accentGid, env);
            MathGlyphMetrics am; font.GetGlyphMetrics(accentGid, am);
            const float scale = Size(env) / upem;
            for (const auto& v : font.GetGlyphVariants(accentGid, MathStretchDirection::Horizontal)) {
                MathGlyphMetrics vm;
                if (!font.GetGlyphMetrics(v.glyph, vm)) continue;
                if ((vm.xMax - vm.xMin) * scale <= body->width * 1.05f) accent = GlyphBox(v.glyph, env);
                else break;
            }
            if (body->width > (am.xMax - am.xMin) * scale * 1.5f && accent->glyph == accentGid) {
                if (MathBoxPtr a = AssemblyBox(accentGid, body->width, MathStretchDirection::Horizontal, env)) accent = a;
            }
        } else {
            accent = GlyphBox(accentGid, env);
        }
        // Horizontal: align the accent's attachment point with the base's.
        MathGlyphMetrics am;
        float accentCentre = accent->width / 2.f;
        if (accent->type == MathBoxType::Glyph) {
            if (accent->topAccentAttachment >= 0.f) accentCentre = accent->topAccentAttachment;
            else if (font.GetGlyphMetrics(accent->glyph, am)) accentCentre = (am.xMin + am.xMax) / 2.f * Size(env) / upem;
        }
        const float dx = skew - accentCentre;
        // Vertical: the accent sits on AccentBaseHeight; raise it by the excess.
        const float delta = std::min(body->height, C(MathConstant::AccentBaseHeight, env));
        const float dy = -(body->height - delta);
        auto box = MathBox::MakeList();
        box->Add(body, 0.f, 0.f);
        box->Add(accent, dx, dy);
        box->width = body->width;
        box->height = std::max(body->height, accent->height - dy);
        box->depth = body->depth;
        return box;
    }

    // ----- over / under material -----
    MathBoxPtr LayoutOverUnder(const MathAtom& atom, const Env& env) {
        using K = MathOverUnderKind;
        const K k = atom.overUnderKind;
        if (k == K::XArrow) {
            Env scriptEnv = env; scriptEnv.style = env.style.Sup();
            MathBoxPtr over = atom.over.empty() ? nullptr : LayoutList(atom.over, scriptEnv);
            MathBoxPtr under = atom.under.empty() ? nullptr : LayoutList(atom.under, scriptEnv);
            const float pad = Em(env, 0.35f);
            const float target = std::max({over ? over->width : 0.f, under ? under->width : 0.f}) + 2 * pad;
            MathBoxPtr arrow = Stretched(atom.codepoint, std::max(target, Em(env, 1.2f)), env);
            const float w = std::max(arrow->width, target);
            auto box = MathBox::MakeList();
            const float arrowShift = (arrow->height - arrow->depth) / 2.f - Axis(env);
            box->Add(arrow, (w - arrow->width) / 2.f, arrowShift);
            box->width = w;
            box->height = arrow->height - arrowShift;
            box->depth = arrow->depth + arrowShift;
            const float gap = Em(env, 0.12f);
            if (over) {
                const float dy = -(box->height + gap + over->depth);
                box->Add(over, (w - over->width) / 2.f, dy);
                box->height = -dy + over->height;
            }
            if (under) {
                const float dy = box->depth + gap + under->height;
                box->Add(under, (w - under->width) / 2.f, dy);
                box->depth = dy + under->depth;
            }
            return box;
        }
        if (k == K::Overline || k == K::Underline) {
            Env inner = env; if (k == K::Overline) inner.style = env.style.Cramped();
            MathBoxPtr body = LayoutList(atom.body, inner);
            const float theta = k == K::Overline ? (C(MathConstant::OverbarRuleThickness, env) > 0 ? C(MathConstant::OverbarRuleThickness, env) : RuleThickness(env))
                                                 : (C(MathConstant::UnderbarRuleThickness, env) > 0 ? C(MathConstant::UnderbarRuleThickness, env) : RuleThickness(env));
            auto box = MathBox::MakeList();
            box->Add(body, 0.f, 0.f);
            box->width = body->width; box->height = body->height; box->depth = body->depth;
            if (k == K::Overline) {
                const float gap = C(MathConstant::OverbarVerticalGap, env);
                const float ruleBottom = body->height + gap;
                box->Add(MathBox::MakeRule(body->width, theta, 0.f), 0.f, -ruleBottom);
                box->height = ruleBottom + theta + C(MathConstant::OverbarExtraAscender, env);
            } else {
                const float gap = C(MathConstant::UnderbarVerticalGap, env);
                const float ruleTop = body->depth + gap;
                box->Add(MathBox::MakeRule(body->width, 0.f, theta), 0.f, ruleTop);
                box->depth = ruleTop + theta + C(MathConstant::UnderbarExtraDescender, env);
            }
            return box;
        }
        if (k == K::Overset || k == K::Underset) {
            MathBoxPtr body = LayoutList(atom.body, env);
            Env scriptEnv = env; scriptEnv.style = env.style.Sup();
            MathBoxPtr over = atom.over.empty() ? nullptr : LayoutList(atom.over, scriptEnv);
            MathBoxPtr under = atom.under.empty() ? nullptr : LayoutList(atom.under, scriptEnv);
            return StackLimits(body, over, under, env, 0.f);
        }
        // Stretchy braces, brackets, parens, arrows.
        const bool above = k == K::Overbrace || k == K::Overbracket || k == K::Overparen || k == K::OverArrow;
        Env inner = env; if (above) inner.style = env.style.Cramped();
        MathBoxPtr body = LayoutList(atom.body, inner);
        MathBoxPtr glyph = Stretched(atom.codepoint, body->width, env);
        // amsmath keeps 3pt between a brace and its body; arrows sit closer.
        const float gap = (k == K::OverArrow || k == K::UnderArrow) ? Em(env, 0.08f) : Em(env, 0.2f);
        auto box = MathBox::MakeList();
        const float w = std::max(body->width, glyph->width);
        box->Add(body, (w - body->width) / 2.f, 0.f);
        box->width = w; box->height = body->height; box->depth = body->depth;
        if (above) {
            const float dy = -(body->height + gap + glyph->depth);
            box->Add(glyph, (w - glyph->width) / 2.f, dy);
            box->height = -dy + glyph->height;
        } else {
            const float dy = body->depth + gap + glyph->height;
            box->Add(glyph, (w - glyph->width) / 2.f, dy);
            box->depth = dy + glyph->depth;
        }
        return box;
    }

    // Material centred above / below a box with limit-style gaps.
    MathBoxPtr StackLimits(const MathBoxPtr& body, const MathBoxPtr& over, const MathBoxPtr& under, const Env& env, float ic) {
        const float w = std::max({body->width, over ? over->width : 0.f, under ? under->width : 0.f});
        auto box = MathBox::MakeList();
        box->Add(body, (w - body->width) / 2.f, 0.f);
        box->width = w; box->height = body->height; box->depth = body->depth;
        if (over) {
            const float gap = std::max(C(MathConstant::UpperLimitGapMin, env), C(MathConstant::UpperLimitBaselineRiseMin, env) - over->depth);
            const float dy = -(body->height + gap + over->depth);
            box->Add(over, (w - over->width) / 2.f + ic / 2.f, dy);
            box->height = -dy + over->height;
        }
        if (under) {
            const float gap = std::max(C(MathConstant::LowerLimitGapMin, env), C(MathConstant::LowerLimitBaselineDropMin, env) - under->height);
            const float dy = body->depth + gap + under->height;
            box->Add(under, (w - under->width) / 2.f - ic / 2.f, dy);
            box->depth = dy + under->depth;
        }
        return box;
    }

    // ----- fences (rule 19) -----
    float DelimiterTarget(const MathBoxPtr& body, const Env& env) const {
        const float axis = Axis(env);
        const float delta = std::max(body->height - axis, body->depth + axis);
        // \delimiterfactor 901, \delimitershortfall 5pt
        return std::max(2.f * delta * 0.901f, 2.f * delta - Em(env, 0.5f));
    }

    MathBoxPtr LayoutFence(const MathAtom& atom, const Env& env) {
        // Lay out the body in segments split at \middle delimiters.
        std::vector<MathBoxPtr> segments;
        std::vector<uint32_t> middles;
        MathList current;
        size_t next = 0;
        for (size_t i = 0; i < atom.body.size(); ++i) {
            if (next < atom.middleIndices.size() && atom.middleIndices[next] == i) {
                segments.push_back(LayoutList(current, env));
                current.clear();
                middles.push_back(atom.body[i]->codepoint);
                ++next;
                continue;
            }
            current.push_back(atom.body[i]);
        }
        segments.push_back(LayoutList(current, env));
        float h = 0.f, d = 0.f;
        for (const auto& s : segments) { h = std::max(h, s->height); d = std::max(d, s->depth); }
        auto probe = MathBox::MakeList(); probe->height = h; probe->depth = d;
        const float target = DelimiterTarget(probe, env);
        HBuilder hb;
        hb.Add(Delimiter(atom.leftDelim, target, env));
        for (size_t i = 0; i < segments.size(); ++i) {
            hb.Add(segments[i]);
            if (i < middles.size()) hb.Add(Delimiter(middles[i], target, env));
        }
        hb.Add(Delimiter(atom.rightDelim, target, env));
        return hb.Finish();
    }

    MathBoxPtr LayoutBigDelim(const MathAtom& atom, const Env& env) {
        static const float heights[] = {0.85f, 1.15f, 1.45f, 1.75f};
        const float h = Em(env, heights[std::max(0, std::min(3, atom.bigSize - 1))]);
        auto probe = MathBox::MakeList(); probe->height = h; probe->depth = 0.f;
        return Delimiter(atom.codepoint, DelimiterTarget(probe, env), env);
    }

    // ----- boxes -----
    MathBoxPtr LayoutBoxAtom(const MathAtom& atom, const Env& env) {
        Env inner = env;
        MathBoxPtr body = LayoutList(atom.body, inner);
        const float sep = Em(env, 0.3f);        // \fboxsep 3pt
        const float rule = Em(env, 0.04f);      // \fboxrule 0.4pt
        auto box = std::make_shared<MathBox>();
        box->type = atom.boxKind == MathBoxKind::ColorBox ? MathBoxType::Background : MathBoxType::Frame;
        box->frameStyle = atom.boxKind == MathBoxKind::Shadow ? MathFrameStyle::Shadow : atom.boxKind == MathBoxKind::Double ? MathFrameStyle::Double
                          : atom.boxKind == MathBoxKind::Oval ? MathFrameStyle::Oval : MathFrameStyle::Single;
        box->lineWidth = atom.boxKind == MathBoxKind::Oval ? Em(env, 0.06f) : rule;
        box->borderColor = atom.border != kMathColorNone ? atom.border : (atom.boxKind == MathBoxKind::ColorBox ? kMathColorNone : kMathColorNone);
        box->color = atom.background;
        float padL = sep + rule, padR = sep + rule, padT = sep + rule, padB = sep + rule;
        if (atom.boxKind == MathBoxKind::Double) { padL = padR = padT = padB = sep + rule * 4.f; }
        if (atom.boxKind == MathBoxKind::Shadow) { box->shadowOffset = Em(env, 0.4f); padR += box->shadowOffset; padB += box->shadowOffset; }
        if (atom.boxKind == MathBoxKind::Oval) {
            const float innerH = body->height + body->depth + 2 * sep;
            box->cornerRadius = std::max(0.f, std::min(1.f, atom.cornerSize)) * std::min(body->width + 2 * sep, innerH) / 2.f;
        }
        box->Add(body, padL, 0.f);
        box->width = padL + body->width + padR;
        box->height = body->height + padT;
        box->depth = body->depth + padB;
        return box;
    }

    // ----- transforms -----
    MathBoxPtr LayoutTransform(const MathAtom& atom, const Env& env) {
        MathBoxPtr body = LayoutList(atom.body, env);
        auto box = std::make_shared<MathBox>();
        box->type = MathBoxType::Transform;
        float a = 1.f, b = 0.f, c = 0.f, d = 1.f;
        if (atom.reflect) a = -1.f;
        if (atom.scaleX != 1.f || atom.scaleY != 1.f) { a *= atom.scaleX; d *= atom.scaleY; }
        if (atom.resizeWidthEm > 0.f || atom.resizeHeightEm > 0.f) {
            const float tw = Em(env, atom.resizeWidthEm), th = Em(env, atom.resizeHeightEm);
            float sx = tw > 0.f && body->width > 0.f ? tw / body->width : 0.f;
            float sy = th > 0.f && body->height + body->depth > 0.f ? th / (body->height + body->depth) : 0.f;
            if (sx == 0.f) sx = sy;
            if (sy == 0.f) sy = sx;
            if (sx == 0.f) sx = sy = 1.f;
            a *= sx; d *= sy;
        }
        if (atom.rotateDegrees != 0.f) {
            const float r = atom.rotateDegrees * 3.14159265f / 180.f;
            // Screen y points down: a counter-clockwise rotation on paper.
            const float cs = std::cos(r), sn = std::sin(r);
            const float na = a * cs, nb = -sn * d, nc = a * sn, nd = d * cs;
            a = na; b = nb; c = nc; d = nd;
            (void)b; (void)c;
        }
        // Bounding box of the transformed corners (x right, y down).
        const float xs[4] = {0.f, body->width, 0.f, body->width};
        const float ys[4] = {-body->height, -body->height, body->depth, body->depth};
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        for (int i = 0; i < 4; ++i) {
            const float tx = a * xs[i] + b * ys[i];
            const float ty = c * xs[i] + d * ys[i];
            minx = std::min(minx, tx); maxx = std::max(maxx, tx);
            miny = std::min(miny, ty); maxy = std::max(maxy, ty);
        }
        box->ma = a; box->mb = b; box->mc = c; box->md = d;
        box->transformDx = -minx;
        box->transformDy = 0.f;
        box->Add(body, 0.f, 0.f);
        box->width = maxx - minx;
        box->height = -miny;
        box->depth = maxy;
        return box;
    }

    // ----- side sets -----
    MathBoxPtr LayoutSideset(const MathAtom& atom, const Env& env) {
        MathBoxPtr body = LayoutList(atom.body, env);
        Env supEnv = env; supEnv.style = env.style.Sup();
        Env subEnv = env; subEnv.style = env.style.Sub();
        auto scripts = [&](const MathList& sub, const MathList& sup, const MathList& nucleus, bool left) -> MathBoxPtr {
            if (sub.empty() && sup.empty() && nucleus.empty()) return nullptr;
            MathAtom tmp;
            tmp.kind = MathAtomKind::Scripts;
            tmp.nucleus = MathAtom::MakeRow(nucleus);
            tmp.hasSub = !sub.empty(); tmp.hasSup = !sup.empty();
            tmp.subscript = sub; tmp.superscript = sup;
            tmp.limits = MathLimits::NoLimits;
            Env e = env;
            MathBoxPtr s = LayoutScripts(tmp, e);
            if (left && nucleus.empty()) {
                // Right-align the scripts against the body.
                float maxW = 0.f;
                for (const auto& ch : s->children) maxW = std::max(maxW, ch.dx + ch.box->width);
                for (auto& ch : s->children) ch.dx += maxW - (ch.dx + ch.box->width);
                s->width = maxW;
            }
            return s;
        };
        HBuilder hb;
        if (MathBoxPtr pre = scripts(atom.preSub, atom.preSup, atom.preNucleus, true)) hb.Add(pre);
        hb.Add(body);
        if (MathBoxPtr post = scripts(atom.postSub, atom.postSup, atom.postNucleus, false)) hb.Add(post);
        return hb.Finish();
    }

    // ----- arrays -----
    MathBoxPtr LayoutArray(const MathAtom& atom, const Env& env) {
        const MathArrayData& data = *atom.array;
        const size_t ncols = std::max<size_t>(1, data.columns.size());
        Env cellEnv = env;
        if (data.displayStyleCells) cellEnv.style = MathStyle::Display();
        else if (data.scriptStyleCells) cellEnv.style = MathStyle{MathStyleKind::Script, env.style.cramped};
        else if (data.textStyleCells) cellEnv.style = MathStyle{MathStyleKind::Text, env.style.cramped};
        else cellEnv.style = MathStyle::Display();
        const bool isAlign = data.kind == MathArrayKind::Align || data.kind == MathArrayKind::Aligned ||
                             data.kind == MathArrayKind::AlignAt || data.kind == MathArrayKind::Split;
        const float colsep = (data.kind == MathArrayKind::Array || data.kind == MathArrayKind::Matrix ||
                              data.kind == MathArrayKind::Eqnarray) ? Em(cellEnv, 0.5f) : 0.f;
        const float baseEm = Size(cellEnv);
        const float strutH = data.scriptStyleCells ? 0.6f * baseEm : 0.84f * baseEm;    // \arraystretch struts
        const float strutD = data.scriptStyleCells ? 0.2f * baseEm : 0.36f * baseEm;
        const float ruleW = Em(env, 0.04f);
        const float doubleRuleSep = Em(env, 0.2f);

        // Lay out cells.
        struct CellBox { MathBoxPtr box; size_t col; int span; MathColumnAlign align; bool hasAlign; MathColor bg; bool dots; bool intertext; int linesL, linesR; };
        std::vector<std::vector<CellBox>> rows;
        std::vector<float> rowH, rowD;
        for (const auto& r : data.rows) {
            std::vector<CellBox> cells;
            size_t col = 0;
            float h = strutH, d = strutD;
            for (const auto& c : r.cells) {
                CellBox cb;
                cb.col = col; cb.span = c.colSpan; cb.bg = c.background; cb.dots = c.dotsFill; cb.intertext = c.intertext;
                cb.hasAlign = c.hasOwnSpec; cb.align = c.spec.align;
                cb.linesL = c.hasOwnSpec ? c.spec.linesBefore : 0; cb.linesR = c.hasOwnSpec ? c.spec.linesAfter : 0;
                if (c.intertext) { Env te = env; te.textRun = true; te.style = MathStyle::Text(); cb.box = LayoutText(c.content, te); }
                else if (c.dotsFill) cb.box = MathBox::MakeList();
                else {
                    Env e = cellEnv;
                    if (data.kind == MathArrayKind::Cases && col == 1 && !data.displayStyleCells) e.style = MathStyle::Text();
                    if ((isAlign && col % 2 == 1) || (data.kind == MathArrayKind::Eqnarray && col == 1)) {
                        // amsmath's template starts the second column with {} so
                        // a leading relation keeps its left-hand space.
                        MathList withEmpty;
                        withEmpty.push_back(MathAtom::MakeRow({}));
                        withEmpty.insert(withEmpty.end(), c.content.begin(), c.content.end());
                        cb.box = LayoutList(withEmpty, e);
                    } else {
                        cb.box = LayoutList(c.content, e);
                    }
                }
                h = std::max(h, cb.box->height);
                d = std::max(d, cb.box->depth);
                col += static_cast<size_t>(c.colSpan);
                cells.push_back(cb);
            }
            rows.push_back(cells);
            rowH.push_back(h);
            rowD.push_back(d);
        }
        // Column widths (single-span cells first, then spanning cells).
        std::vector<float> colW(ncols, 0.f);
        for (const auto& r : rows) for (const auto& c : r) {
            if (c.span == 1 && c.col < ncols && !c.intertext) colW[c.col] = std::max(colW[c.col], c.box->width);
        }
        // Boundary widths: between column j-1 and j (j = 0..ncols).
        std::vector<float> boundary(ncols + 1, 0.f);
        std::vector<int> boundaryLines(ncols + 1, 0);
        std::vector<MathBoxPtr> boundaryBox(ncols + 1);
        for (size_t j = 0; j <= ncols; ++j) {
            const MathColumnSpec* right = j < data.columns.size() ? &data.columns[j] : nullptr;
            const MathColumnSpec* left = j > 0 && j - 1 < data.columns.size() ? &data.columns[j - 1] : nullptr;
            float w;
            bool custom = false;
            if (right && right->customSepBefore) { boundaryBox[j] = LayoutList(right->sepBefore, cellEnv); w = boundaryBox[j]->width; custom = true; }
            else if (j == ncols && left && left->customSepAfter) { boundaryBox[j] = LayoutList(left->sepAfter, cellEnv); w = boundaryBox[j]->width; custom = true; }
            else if (j == 0) w = data.leftColSep ? colsep : 0.f;
            else if (j == ncols) w = data.rightColSep ? colsep : 0.f;
            else w = 2.f * colsep;
            int lines = 0;
            if (right) lines += right->linesBefore;
            if (j == ncols && left) lines += left->linesAfter;
            if (isAlign && j > 0 && j < ncols && j % 2 == 0 && !custom) w = Em(cellEnv, 1.f);   // between rl pairs
            if (data.kind == MathArrayKind::Cases && j == 1 && !custom) w = Em(cellEnv, 1.f);
            if (lines > 0) w += lines * ruleW + (lines - 1) * doubleRuleSep;
            boundary[j] = w;
            boundaryLines[j] = lines;
        }
        for (const auto& r : rows) for (const auto& c : r) {
            if (c.span > 1 && !c.intertext) {
                float have = 0.f;
                for (int k = 0; k < c.span && c.col + k < ncols; ++k) { have += colW[c.col + k]; if (k > 0) have += boundary[c.col + k]; }
                if (c.box->width > have && c.col < ncols) colW[std::min(ncols - 1, c.col + c.span - 1)] += c.box->width - have;
            }
        }
        std::vector<float> colX(ncols + 1, 0.f);
        {
            float x = 0.f;
            for (size_t j = 0; j < ncols; ++j) { x += boundary[j]; colX[j] = x; x += colW[j]; }
            colX[ncols] = x + boundary[ncols];
        }
        const float totalW = colX[ncols];
        // multline / gather alignment across the whole width
        // Rows
        auto box = MathBox::MakeList();
        float y = 0.f;    // baseline of the current row (positive down), first row at 0
        std::vector<float> rowBaseline;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (i > 0) {
                y += rowD[i - 1] + rowH[i];
                if (data.rows[i - 1].extraSkip > 0.f) y += Em(env, data.rows[i - 1].extraSkip);
                y += data.rows[i].hlinesBefore * ruleW + (data.kind == MathArrayKind::Lines ? Em(env, 0.15f) : 0.f);
            }
            rowBaseline.push_back(y);
        }
        const float totalH = rows.empty() ? 0.f : rowBaseline.back() + rowD.back() + rowH.front() + data.hlinesAfter * ruleW;
        const float topY = rows.empty() ? 0.f : -rowH.front();
        // Backgrounds first
        for (size_t i = 0; i < rows.size(); ++i) {
            const float rowTop = rowBaseline[i] - rowH[i], rowBottom = rowBaseline[i] + rowD[i];
            if (data.rows[i].background != kMathColorNone) {
                auto bg = std::make_shared<MathBox>();
                bg->type = MathBoxType::Background; bg->color = data.rows[i].background;
                bg->width = totalW; bg->height = rowBottom - rowTop; bg->depth = 0.f;
                box->Add(bg, 0.f, rowBottom);
            }
            for (const auto& c : rows[i]) {
                if (c.bg == kMathColorNone || c.col >= ncols) continue;
                const size_t last = std::min(ncols - 1, c.col + c.span - 1);
                const float x0 = colX[c.col] - boundary[c.col] / 2.f, x1 = colX[last] + colW[last] + boundary[last + 1] / 2.f;
                auto bg = std::make_shared<MathBox>();
                bg->type = MathBoxType::Background; bg->color = c.bg;
                bg->width = x1 - x0; bg->height = rowBottom - rowTop; bg->depth = 0.f;
                box->Add(bg, x0, rowBottom);
            }
        }
        // Cells
        for (size_t i = 0; i < rows.size(); ++i) {
            for (const auto& c : rows[i]) {
                if (c.col >= ncols && !c.intertext) continue;
                if (c.intertext) { box->Add(c.box, 0.f, rowBaseline[i]); continue; }
                const size_t last = std::min(ncols - 1, c.col + c.span - 1);
                float x0 = colX[c.col];
                float x1 = colX[last] + colW[last];
                if (c.span > 1) { x0 = colX[c.col]; x1 = colX[last] + colW[last]; }
                const float avail = x1 - x0;
                MathColumnAlign align = c.hasAlign ? c.align : data.columns[std::min(c.col, data.columns.size() - 1)].align;
                if (data.kind == MathArrayKind::Multline) align = i == 0 ? MathColumnAlign::Left : i + 1 == rows.size() ? MathColumnAlign::Right : MathColumnAlign::Center;
                if (c.dots) {
                    // \hdotsfor: dots spaced across the span
                    const uint32_t dot = font.GetGlyphIndex('.');
                    MathBoxPtr dg = dot ? GlyphBox(dot, cellEnv) : MathBox::MakeKern(0);
                    const float step = Em(cellEnv, 0.5f);
                    for (float xx = x0 + step / 2.f; xx + dg->width <= x1; xx += step) box->Add(dg, xx, rowBaseline[i]);
                    continue;
                }
                float dx = x0;
                if (align == MathColumnAlign::Center) dx = x0 + (avail - c.box->width) / 2.f;
                else if (align == MathColumnAlign::Right) dx = x1 - c.box->width;
                box->Add(c.box, dx, rowBaseline[i]);
                // Vertical rules are per cell, as in LaTeX: a spanning cell
                // replaces the rules of the columns it covers with its own.
                const float segTop = rowBaseline[i] - rowH[i] - data.rows[i].hlinesBefore * ruleW;
                const float segBottom = rowBaseline[i] + rowD[i] + ((i + 1 < rows.size() ? data.rows[i + 1].hlinesBefore : data.hlinesAfter) * ruleW);
                auto rulesAt = [&](size_t boundaryIndex, int count) {
                    if (count <= 0) return;
                    const float bx = boundaryIndex < ncols ? colX[boundaryIndex] - boundary[boundaryIndex] : colX[ncols] - boundary[ncols];
                    const float group = count * ruleW + (count - 1) * doubleRuleSep;
                    float rx = bx + (boundary[boundaryIndex] - group) / 2.f;
                    if (boundaryIndex == 0) rx = bx;
                    if (boundaryIndex == ncols) rx = bx + boundary[boundaryIndex] - group;
                    for (int k = 0; k < count; ++k)
                        box->Add(MathBox::MakeRule(ruleW, 0.f, segBottom - segTop), rx + k * (ruleW + doubleRuleSep), segTop);
                };
                const size_t rightBoundary = std::min(ncols, c.col + static_cast<size_t>(c.span));
                if (c.hasAlign) {
                    rulesAt(c.col, c.linesL);
                    rulesAt(rightBoundary, c.linesR);
                } else {
                    rulesAt(c.col, boundaryLines[c.col]);
                    if (rightBoundary == ncols) rulesAt(ncols, boundaryLines[ncols]);
                }
            }
        }
        // Column separators material (@{...})
        for (size_t j = 0; j <= ncols; ++j) {
            if (!boundaryBox[j]) continue;
            for (size_t i = 0; i < rows.size(); ++i) {
                const float bx = j < ncols ? colX[j] - boundary[j] : colX[ncols] - boundary[ncols];
                box->Add(boundaryBox[j], bx + (boundary[j] - boundaryBox[j]->width) / 2.f, rowBaseline[i]);
            }
        }
        // Horizontal lines
        for (size_t i = 0; i < rows.size(); ++i) {
            for (int k = 0; k < data.rows[i].hlinesBefore; ++k) {
                const float yy = rowBaseline[i] - rowH[i] - (data.rows[i].hlinesBefore - k) * ruleW + ruleW;
                box->Add(MathBox::MakeRule(totalW, ruleW, 0.f), 0.f, yy);
            }
        }
        for (int k = 0; k < data.hlinesAfter && !rows.empty(); ++k) {
            box->Add(MathBox::MakeRule(totalW, 0.f, ruleW), 0.f, rowBaseline.back() + rowD.back() + k * ruleW);
        }
        const float tableTop = topY;
        const float tableBottom = rows.empty() ? 0.f : rowBaseline.back() + rowD.back() + data.hlinesAfter * ruleW;
        box->width = totalW;
        // Centre the whole table on the axis (\vcenter).
        const float total = tableBottom - tableTop;
        const float axis = Axis(env);
        const float centre = (tableTop + tableBottom) / 2.f;     // in row coordinates
        const float shift = -axis - centre;                       // move so centre lands at -axis
        for (auto& ch : box->children) ch.dy += shift;
        box->height = total / 2.f + axis;
        box->depth = total / 2.f - axis;
        (void)totalH;

        if (data.leftDelim == 0 && data.rightDelim == 0) return box;
        const float target = DelimiterTarget(box, env);
        HBuilder hb;
        hb.Add(Delimiter(data.leftDelim, target, env));
        hb.Add(box);
        hb.Add(Delimiter(data.rightDelim, target, env));
        return hb.Finish();
    }

    // ----- long division -----
    MathBoxPtr LayoutLongDiv(const MathAtom& atom, const Env& env) {
        std::string dividend = atom.text, divisor = atom.divisor;
        auto digitsOnly = [](std::string& s) { std::string o; for (char c : s) if (std::isdigit(static_cast<unsigned char>(c))) o.push_back(c); s = o; };
        digitsOnly(dividend); digitsOnly(divisor);
        if (dividend.empty() || divisor.empty() || divisor == "0" || dividend.size() > 18 || divisor.size() > 18) {
            MathAtom err; err.text = "\\longdiv"; return LayoutAtom(err, env);
        }
        const unsigned long long n = std::strtoull(dividend.c_str(), nullptr, 10);
        const unsigned long long d = std::strtoull(divisor.c_str(), nullptr, 10);
        // Steps: at digit i the number being divided (cur), the product
        // subtracted and the remainder; the next step brings the next digit down.
        struct Step { std::string cur; std::string sub; size_t endDigit; };
        std::vector<Step> steps;
        std::string quotient, remainder;
        unsigned long long cur = 0;
        bool started = false;
        for (size_t i = 0; i < dividend.size(); ++i) {
            cur = cur * 10 + (dividend[i] - '0');
            const unsigned long long q = cur / d;
            if (q > 0 || started) {
                quotient.push_back(static_cast<char>('0' + q));
                started = true;
                if (q > 0) {
                    steps.push_back({std::to_string(cur), std::to_string(q * d), i});
                    cur -= q * d;
                }
            }
        }
        remainder = std::to_string(cur);
        if (quotient.empty()) quotient = "0";
        (void)n;
        auto textBox = [&](const std::string& s, const Env& e) {
            HBuilder hb;
            for (char c : s) { const uint32_t g = font.GetGlyphIndex(static_cast<unsigned char>(c)); if (g) hb.Add(GlyphBox(g, e)); }
            return hb.Finish();
        };
        MathGlyphMetrics dm; font.GetGlyphMetrics(font.GetGlyphIndex('0'), dm);
        const float digitW = Units(dm.advance, env);
        MathBoxPtr divisorBox = textBox(divisor, env);
        MathBoxPtr dividendBox = textBox(dividend, env);
        MathBoxPtr quotientBox = textBox(quotient, env);
        const uint32_t parenGid = font.GetGlyphIndex(')');
        MathBoxPtr paren = parenGid ? GlyphBox(parenGid, env) : MathBox::MakeKern(0);
        const float lineH = Em(env, 1.2f);
        const float ruleT = Em(env, 0.05f);
        auto box = MathBox::MakeList();
        const float x0 = divisorBox->width + paren->width;          // left of the dividend
        // quotient above, right-aligned to the dividend
        box->Add(quotientBox, x0 + dividendBox->width - quotientBox->width, -lineH);
        box->Add(MathBox::MakeRule(dividendBox->width, ruleT, 0.f), x0, -(dividendBox->height + Em(env, 0.15f)));
        box->Add(divisorBox, 0.f, 0.f);
        box->Add(paren, divisorBox->width, 0.f);
        box->Add(dividendBox, x0, 0.f);
        float y = 0.f;
        float maxDepth = dividendBox->depth;
        for (size_t k = 0; k < steps.size(); ++k) {
            const auto& st = steps[k];
            const float right = x0 + (st.endDigit + 1) * digitW;
            if (k > 0) {
                // the brought-down number
                y += lineH;
                MathBoxPtr cb = textBox(st.cur, env);
                box->Add(cb, right - cb->width, y);
            }
            y += lineH;
            MathBoxPtr sb = textBox(st.sub, env);
            box->Add(sb, right - sb->width, y);
            const float ruleW2 = std::max(sb->width, digitW * static_cast<float>(st.cur.size()));
            box->Add(MathBox::MakeRule(ruleW2, 0.f, ruleT), right - ruleW2, y + Em(env, 0.3f));
            maxDepth = y + sb->depth + Em(env, 0.3f) + ruleT;
        }
        {
            y += lineH;
            MathBoxPtr rb = textBox(remainder, env);
            const float right = x0 + dividendBox->width;
            box->Add(rb, right - rb->width, y);
            maxDepth = y + rb->depth;
        }
        box->width = x0 + dividendBox->width;
        box->height = lineH + quotientBox->height;
        box->depth = std::max(dividendBox->depth, maxDepth);
        return box;
    }

    // ----- top level -----
    MathBoxPtr LayoutTop(const MathAtomPtr& root) {
        Env env;
        env.style = options.style;
        MathBoxPtr box;
        if (options.maxWidth > 0.f && root && root->kind == MathAtomKind::Row) {
            box = LayoutList(root->body, env);
            if (box->width > options.maxWidth && root->body.size() > 1) box = BreakLines(root->body, env);
        } else {
            box = root ? LayoutAtom(*root, env) : MathBox::MakeList();
        }
        return box;
    }

    // Greedy line breaking at relations / binary operators for maxWidth.
    MathBoxPtr BreakLines(const MathList& list, const Env& env) {
        std::vector<MathList> lines;
        MathList current;
        for (size_t i = 0; i < list.size(); ++i) {
            const MathAtomPtr& a = list[i];
            const bool breakBefore = !current.empty() && (a->atomClass == MathAtomClass::Rel || a->atomClass == MathAtomClass::Bin);
            if (breakBefore) {
                MathList trial = current; trial.push_back(a);
                for (size_t j = i + 1; j < list.size(); ++j) {
                    if (list[j]->atomClass == MathAtomClass::Rel || list[j]->atomClass == MathAtomClass::Bin) break;
                    trial.push_back(list[j]);
                }
                if (LayoutList(trial, env)->width > options.maxWidth && !current.empty()) {
                    lines.push_back(current);
                    current.clear();
                }
            }
            current.push_back(a);
        }
        if (!current.empty()) lines.push_back(current);
        if (lines.size() <= 1) return LayoutList(list, env);
        auto box = MathBox::MakeList();
        float y = 0.f;
        float prevDepth = 0.f;
        for (size_t i = 0; i < lines.size(); ++i) {
            MathBoxPtr lb = LayoutList(lines[i], env);
            if (i > 0) y += prevDepth + lb->height + Em(env, 0.3f);
            box->Add(lb, i == 0 ? 0.f : Em(env, 1.f), y);
            box->width = std::max(box->width, (i == 0 ? 0.f : Em(env, 1.f)) + lb->width);
            if (i == 0) box->height = lb->height;
            prevDepth = lb->depth;
            box->depth = y + lb->depth;
        }
        return box;
    }
};

// =============================================================================
// Public
// =============================================================================

UltraCanvasMathLayout::UltraCanvasMathLayout(const UltraCanvasMathFont& font, const MathLayoutOptions& options,
                                             std::vector<MathDiagnostic>& diagnostics)
    : impl_(new Impl(font, options, diagnostics)) {}

UltraCanvasMathLayout::~UltraCanvasMathLayout() { delete impl_; }

MathBoxPtr UltraCanvasMathLayout::Layout(const MathAtomPtr& root) { return impl_->LayoutTop(root); }

} // namespace UltraCanvas
