// include/UltraCanvasLabel.h
// Modern text display label control with styling and alignment options.
// Exemplar of the new CSSLayout intrinsic-sizing protocol: overrides
// MeasureOwnContent (constraint-aware content sizing) and ComputeIntrinsicSizes
// (constraint-free max/min-content) so the engine can place the label
// without the widget mutating finalBounds itself.
// Version: 2.0.3
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <functional>

namespace UltraCanvas {

// ===== LABEL STYLE CONFIGURATION =====
    struct LabelStyle {
        // Text appearance
        FontStyle fontStyle;
        Color textColor = Colors::Black;
        // Used instead of textColor while the label IsDisabled(), so captions
        // grey out together with the control they describe.
        Color disabledTextColor = Color(178, 178, 184, 255);

        // Text alignment. Vertical defaults to Middle so labels line up with
        // the text of neighbouring controls (buttons, checkboxes) in rows;
        // for auto-sized labels the box hugs the text and Middle == Top.
        TextAlignment horizontalAlign = TextAlignment::Left;
        VerticalAlignment verticalAlign = VerticalAlignment::Middle;
        // Word wrapping
        TextWrap wrap = TextWrap::WrapNone;

        // Text effects
        bool hasShadow = false;
        Color shadowColor = Color(0, 0, 0, 128);
        Point2Di shadowOffset = Point2Di(1, 1);

        static LabelStyle DefaultStyle() {
            return LabelStyle{};
        }

        static LabelStyle HeaderStyle();
        static LabelStyle SubHeaderStyle();
        static LabelStyle CaptionStyle();
        static LabelStyle StatusStyle();
    };

// ===== TEXT LINKS =====
    // A hyperlink inside the label's rendered text. Byte offsets refer to the
    // text as laid out (markup parsed, entities decoded) — the same indexing
    // that ITextLayout::XYToIndex reports.
    struct LabelTextLink {
        int startByte = 0;
        int endByte = 0;      // exclusive
        std::string href;
    };

    // An image flowing in the text, like a browser's inline <img>: it stands on
    // the line's baseline at the place of a U+FFFC placeholder character in
    // the rendered text, and the line grows to hold it. Wider than the line,
    // it is scaled down to fit, keeping its aspect ratio.
    // Where an inline image sits against the text of its line (CSS
    // vertical-align): standing on the baseline, centred on the x-height, its
    // top at the text's top, or its bottom at the text's bottom.
    enum class LabelInlineImageAlign { Baseline, Middle, Top, Bottom };

    struct LabelInlineImage {
        int   byteOffset = 0;       // of the U+FFFC placeholder in the rendered text
        float width = 0.f;          // display size in px (before any fit-to-line)
        float height = 0.f;
        std::shared_ptr<UCImage> image;
        LabelInlineImageAlign align = LabelInlineImageAlign::Baseline;
    };

// ===== LABEL COMPONENT =====
    class UltraCanvasLabel : public UltraCanvasUIElement {
    private:
        // ===== LABEL PROPERTIES =====
        std::string text;
        LabelStyle style;
        std::vector<LabelTextLink> textLinks;
        int hoveredLink = -1;
        std::vector<LabelInlineImage> inlineImages;
        float inlineFitWidth = -1.f;   // line width the image shapes were sized for; -1 = none
        std::vector<float> inlineAscents;   // per image: its top above the baseline, as laid out

        // ===== COMPUTED LAYOUT =====
        Rect2Di textArea;
        std::unique_ptr<ITextLayout> textLayout = nullptr;
        bool isMarkup = false;

        bool internalLayoutValid = false;
    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasLabel(const std::string &identifier, float x, float y, float w, float h,
                         const std::string &labelText = "");

        UltraCanvasLabel(const std::string &identifier, float w, float h, const std::string &labelText = "")
                : UltraCanvasLabel(identifier, -1, -1, w, h, labelText) {}

        UltraCanvasLabel(const std::string &identifier, const std::string &labelText)
                : UltraCanvasLabel(identifier, -1, -1, -1, -1, labelText) {}

        explicit UltraCanvasLabel(const std::string &labelText = "")
                : UltraCanvasLabel("", -1, -1, -1, -1, labelText) {}

        virtual ~UltraCanvasLabel() = default;

        // ===== TEXT MANAGEMENT =====
        void SetText(const std::string &newText);

        const std::string &GetText() const {
            return text;
        }

        // ===== TEXT LINKS =====
        // Clickable byte ranges inside the rendered text; activation is
        // reported through onLinkActivated. Hit testing needs the text
        // layout, so it only works once the label has been rendered.
        void SetTextLinks(std::vector<LabelTextLink> links) {
            textLinks = std::move(links);
            hoveredLink = -1;
        }
        const std::vector<LabelTextLink> &GetTextLinks() const { return textLinks; }
        // Index into GetTextLinks() of the link at a label-local point, or -1.
        int LinkIndexAtPoint(const Point2Di& localPoint);

        // ===== INLINE IMAGES =====
        // Images drawn at U+FFFC placeholders of the text (see LabelInlineImage).
        void SetInlineImages(std::vector<LabelInlineImage> images);
        const std::vector<LabelInlineImage>& GetInlineImages() const { return inlineImages; }
        // Where an inline image is drawn, label-local; empty before the first
        // layout or for an index out of range.
        Rect2Df InlineImageRect(size_t index);

        // ===== STYLE MANAGEMENT =====
        void SetStyle(const LabelStyle &newStyle);
        const LabelStyle &GetStyle() const {
            return style;
        }

        // ===== CONVENIENCE STYLE SETTERS =====
        void SetFont(const std::string &fontFamily, float fontSize = 12.0f, FontWeight weight = FontWeight::Normal);
        void SetFontSize(float fontSize);
        void SetFontWeight(const FontWeight w);
        void SetTextColor(const Color &color);
        void SetAlignment(TextAlignment horizontal, VerticalAlignment vertical = VerticalAlignment::Middle);
        void SetWrap(TextWrap wrap);
        void SetTextIsMarkup(bool markup);

        // ===== ENGINE-DRIVEN LAYOUT =====
        // Content-box size of the text. nullopt width → max-content width;
        // a definite width → height at that width (reflects wrapping). The
        // block layout adds padding/border and applies size.*/constraints.
        Size2Df MeasureOwnContent(std::optional<float> definiteContentWidth,
                                  const CSSLayout::LayoutContext& ctx) override;

        // Constraint-free: publishes intrinsic min/max-content via the
        // inherited `intrinsic` cache, in BORDER-BOX units (i.e. including
        // padding + border) to match what measured.* would return.
        void ComputeIntrinsicSizes(const CSSLayout::LayoutContext& ctx) override;

        void InvalidateLayout() override;

        // Re-sync the cached text layout when the engine re-arranges us to a
        // new size (e.g. a window resize growing a grid/flex cell); otherwise
        // wrapped text keeps its previous wrap width.
        void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override;

        // ===== RENDERING =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void UpdateInternalLayout(IRenderContext *ctx);

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;

        // A label with a click handler behaves like a hyperlink, so it reports
        // the hand/pointer cursor automatically (for text links only while
        // the pointer is over a link range). An explicit SetMouseCursor()
        // (i.e. a non-Default cursor) always takes precedence.
        UCMouseCursor GetMouseCursor() const override {
            if (mouseCursor == UCMouseCursor::Default &&
                (onClick || hoveredLink >= 0)) {
                return UCMouseCursor::Hand;
            }
            return mouseCursor;
        }

        // ===== EVENT CALLBACKS =====
        std::function<void()> onClick;
        // Fired with LabelTextLink::href when a text link is clicked.
        std::function<void(const std::string&)> onLinkActivated;
        std::function<void()> onHoverEnter;
        std::function<void()> onHoverLeave;
        std::function<void(const std::string&)> onTextChanged;

    protected:
        // Build the cached ITextLayout if missing and configure it with the
        // current font/wrap/alignment. Does NOT set explicit width — callers
        // (MeasureOwnContent / ComputeIntrinsicSizes) own that.
        // Returns true if the layout is now valid, false if no render context
        // is available (in which case callers should bail gracefully).
        //
        // `ctx` is the context the caller is about to draw into, and is used
        // in preference to the one reachable through the element's window.
        // They are the same object for a label in a window; they differ for a
        // label rendered into an offscreen surface, where the element has no
        // window to ask and only the caller knows the context.
        bool EnsureTextLayout(IRenderContext* ctx = nullptr);

    private:
        // The display size of an inline image on a line `inlineFitWidth` wide.
        Size2Df InlineImageSize(const LabelInlineImage& image) const;
        // Size the images for a line `width` wide (-1: unbounded); drops the
        // text layout when that changes an image's box, so it is rebuilt.
        void FitInlineImages(float width);
    };


// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasLabel>
    CreateLabel(const std::string &identifier, float x, float y, float w, float h,
                const std::string &text = "") {
        return std::make_shared<UltraCanvasLabel>(identifier, x, y, w, h, text);
    }

    inline std::shared_ptr<UltraCanvasLabel>
    CreateLabel(const std::string &identifier, float w, float h, const std::string &text = "") {
        return std::make_shared<UltraCanvasLabel>(identifier, 0, 0, w, h, text);
    }

    inline std::shared_ptr<UltraCanvasLabel> CreateLabel(const std::string &text) {
        return std::make_shared<UltraCanvasLabel>("", text);
    }

    inline std::shared_ptr<UltraCanvasLabel> CreateLabel(const std::string &identifier,
                                                  const std::string &text) {
        return std::make_shared<UltraCanvasLabel>(identifier, text);
    }

// ===== BUILDER PATTERN =====
    class LabelBuilder {
    private:
        std::shared_ptr<UltraCanvasLabel> label;

    public:
        LabelBuilder(const std::string& identifier);

        LabelBuilder& SetText(const std::string& text);
        LabelBuilder& SetFont(const std::string& fontFamily, float fontSize = 12.0f);
        LabelBuilder& SetTextColor(const Color& color);
        LabelBuilder& SetBackgroundColor(const Color& color);
        LabelBuilder& SetAlignment(TextAlignment align);
        LabelBuilder& SetPadding(float padding);
        LabelBuilder& SetStyle(const LabelStyle& style);
        LabelBuilder& OnClick(std::function<void()> callback);

        std::shared_ptr<UltraCanvasLabel> Build() {
            return label;
        }
    };
} // namespace UltraCanvas
