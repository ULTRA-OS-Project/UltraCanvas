// UltraCanvas/include/UltraCanvasTextLayoutCache.h
// A text layout kept from draw to draw and rebuilt only when what it was
// built from changes. Shaping a text is the dear part of drawing it, and an
// element is drawn far more often than its text changes: every hover over
// a list of buttons repaints the list. The label kept its layout and reset
// it in every setter; the button compared the inputs on each draw. This is
// the one pattern for both, and for the next element: the inputs are a key
// (TextLayoutKey), and Get() hands back the layout it holds while the key is
// the one it was built with, else a new one with the key applied.
//
// What the key does not cover - the label's inline images, say - the
// element handles with Reset() where that changes. Builds() counts the
// layouts made, so a test can say "ten draws, one layout"; the render
// context counts the same on its side (IRenderContext::TextLayoutsCreated).
//
// doc-check: UltraCanvas::IRenderContext* ctx;
// doc-check: example
//   // In an element, drawing `text` at `width`:
//   UltraCanvas::TextLayoutKey key;
//   key.text = "Open a site folder";
//   key.font.fontSize = 14;
//   key.ellipsize = UltraCanvas::EllipsizeMode::EllipsizeMiddle;
//   key.width = 300;
//   UltraCanvas::UltraCanvasTextLayoutCache cache;
//   if (UltraCanvas::ITextLayout* layout = cache.Get(nullptr, key)) ctx->DrawTextLayout(*layout, UltraCanvas::Point2Dd(0, 0));
//
// Version: 1.2.0 - IsSingleCharacterText / DrawTextCentredOnInk: a one-character text centred on what
//                  it draws (the button, the label, the badge, the segmented control)
// Version: 1.1.1 - a key that names no ellipsize leaves the wrap mode's: a WrapNone label
//                  stays one line cut at the end, not wrapped at a narrow width
// Version: 1.1.0 - an explicit height in the key; the text input, the dropdown and the menu on the cache
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRenderContext.h"

#include <cmath>
#include <memory>
#include <optional>
#include <string>

namespace UltraCanvas {

    // Everything a layout is built from. Two keys that compare equal draw
    // the same text the same way.
    struct TextLayoutKey {
        std::string text;
        bool markup = false;
        FontStyle font;
        TextWrap wrap = TextWrap::WrapNone;
        TextAlignment align = TextAlignment::Left;
        VerticalAlignment valign = VerticalAlignment::Top;
        // Unset: what SetWrap left - WrapNone cuts the line at the end, which
        // is what keeps it one line at any width. Setting EllipsizeNone on a
        // WrapNone layout with a width lets Pango wrap it instead.
        std::optional<EllipsizeMode> ellipsize;
        double width = -1;        // the explicit width; -1: none, the caller sets it
        double height = -1;       // the explicit height, for a vertical alignment inside it; -1: none
        float lineHeightPx = 0;   // noted in the key; the element applies it
        bool operator==(const TextLayoutKey&) const = default;
    };

    class UltraCanvasTextLayoutCache {
    public:
        // The layout for `key`: the one held when the key is the one it was
        // built with, else a new one made with `ctx` and the key applied
        // (font, wrap, alignment, ellipsize, the width and height when given);
        // `rebuilt` says which, for what the element adds on top of a fresh
        // layout. Null without a context, or when the context gives none.
        ITextLayout* Get(IRenderContext* ctx, const TextLayoutKey& key, bool* rebuilt = nullptr) {
            if (rebuilt) *rebuilt = false;
            if (layout_ && key == key_) return layout_.get();
            layout_.reset();
            if (!ctx) return nullptr;
            layout_ = ctx->CreateTextLayout(key.text, key.markup);
            if (!layout_) return nullptr;
            layout_->SetFontStyle(key.font);
            layout_->SetWrap(key.wrap);
            layout_->SetAlignment(key.align);
            layout_->SetVerticalAlignment(key.valign);
            if (key.ellipsize) layout_->SetEllipsize(*key.ellipsize);
            if (key.width >= 0) layout_->SetExplicitWidth(key.width);
            if (key.height >= 0) layout_->SetExplicitHeight(key.height);
            key_ = key;
            ++builds_;
            if (rebuilt) *rebuilt = true;
            return layout_.get();
        }

        // The layout held, if any - for what reads it between draws.
        ITextLayout* Current() const { return layout_.get(); }
        ITextLayout* operator->() const { return layout_.get(); }
        ITextLayout& operator*() const { return *layout_; }
        explicit operator bool() const { return static_cast<bool>(layout_); }
        // Drops the layout: the next Get builds one, whatever the key.
        void Reset() { layout_.reset(); }
        // How many layouts were built, for a test.
        unsigned long Builds() const { return builds_; }

    private:
        std::unique_ptr<ITextLayout> layout_;
        TextLayoutKey key_;
        unsigned long builds_ = 0;
    };

    // ===== ONE CHARACTER, CENTRED ON ITS INK =====
    // A text is centred on its line box, which holds the font's ascender and
    // descender room: right for a word, whose capitals then sit in the
    // middle and line up with the words beside it. A lone character - "+",
    // "×", "‹", a check mark - has no capitals to line up, and sits wherever
    // the font puts it in that box: a 26 px "+" came out 2.5 px low in a
    // round 48 px button, an "×" 2 px low in a 32 px label. Elements that
    // centre their text therefore put a one-character text where its drawn
    // shape - its ink - is in the middle instead.

    // One code point of UTF-8 (the bytes that start one, not the ones that
    // continue it): a single symbol or letter.
    inline bool IsSingleCharacterText(const std::string& text) {
        int starts = 0;
        for (const char c : text)
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++starts;
        return starts == 1;
    }

    // Draws one line of `text` in `font`, in the context's current text
    // paint, with its ink box centred in `box`, on whole pixels so its
    // strokes stay sharp; the layout is kept in `cache` between draws. False,
    // having drawn nothing, when the text has no ink (a space): the caller
    // then draws it as it always did.
    inline bool DrawTextCentredOnInk(IRenderContext* ctx, UltraCanvasTextLayoutCache& cache,
                                     const std::string& text, const FontStyle& font, const Rect2Dd& box) {
        TextLayoutKey key;
        key.text = text;
        key.font = font;
        key.wrap = TextWrap::WrapNone;
        key.align = TextAlignment::Left;
        ITextLayout* layout = cache.Get(ctx, key);
        if (!layout) return false;
        const Rect2Dd ink = layout->GetLayoutExtents().ink;
        if (ink.width <= 0 || ink.height <= 0) return false;
        const double x = box.x + box.width / 2.0 - (ink.x + ink.width / 2.0);
        const double y = box.y + box.height / 2.0 - (ink.y + ink.height / 2.0);
        ctx->DrawTextLayout(*layout, Point2Dd(std::round(x), std::round(y)));
        return true;
    }

} // namespace UltraCanvas
