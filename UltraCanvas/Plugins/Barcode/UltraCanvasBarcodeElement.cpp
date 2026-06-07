// Plugins/Barcode/UltraCanvasBarcodeElement.cpp
// Implementation of the UltraCanvas barcode widget. Renders the encoded
// module stream via IRenderContext primitives — bars become filled rects,
// HRI text uses the standard text layout path. Rotation is performed by
// transforming the context around the widget center.
//
// Version: 1.1.0
// Last Modified: 2026-06-07
// Author: UltraCanvas Framework
// V1.1.0: CSS Measure/Arrange migration — GetPreferredWidth/Height replaced by
//   MeasureOwnContent/ComputeIntrinsicSizes; Render/RenderError now take Rect2Df.

#include "Plugins/Barcode/UltraCanvasBarcodeElement.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

    // ===== STYLE PRESETS =====
    BarcodeStyle BarcodeStyle::CompactStyle() {
        BarcodeStyle s;
        s.moduleWidth = 1.5f;
        s.barHeight = 36.0f;
        s.quietZoneModules = 6.0f;
        s.textFontSize = 10.0f;
        return s;
    }

    BarcodeStyle BarcodeStyle::PrintStyle() {
        BarcodeStyle s;
        s.moduleWidth = 4.0f;      // ~0.33mm @ 300dpi
        s.barHeight = 100.0f;
        s.quietZoneModules = 10.0f;
        s.textFontSize = 14.0f;
        return s;
    }

    BarcodeStyle BarcodeStyle::MonochromeStyle() {
        BarcodeStyle s;
        s.foreground = Colors::Black;
        s.background = Colors::White;
        s.textColor = Colors::Black;
        s.textFontWeight = FontWeight::Bold;
        return s;
    }

    // ===== CONSTRUCTORS =====
    UltraCanvasBarcodeElement::UltraCanvasBarcodeElement(const std::string& id,
                                                          long x, long y, long w, long h)
        : UltraCanvasUIElement(id, x, y, w, h) {
        style = BarcodeStyle::DefaultStyle();
    }

    UltraCanvasBarcodeElement::UltraCanvasBarcodeElement(const std::string& id,
                                                          long w, long h)
        : UltraCanvasUIElement(id, 0, 0, w, h) {
        style = BarcodeStyle::DefaultStyle();
    }

    // ===== DATA & SYMBOLOGY =====
    void UltraCanvasBarcodeElement::SetSymbology(BarcodeSymbology s) {
        if (symbology != s) {
            symbology = s;
            encodeDirty = true;
            RequestRedraw();
        }
    }

    void UltraCanvasBarcodeElement::SetData(const std::string& d) {
        if (data != d) {
            data = d;
            encodeDirty = true;
            RequestRedraw();
        }
    }

    void UltraCanvasBarcodeElement::SetEncoderOptions(const BarcodeEncoderOptions& opt) {
        options = opt;
        encodeDirty = true;
        RequestRedraw();
    }

    void UltraCanvasBarcodeElement::Set(BarcodeSymbology s, const std::string& d) {
        symbology = s;
        data = d;
        encodeDirty = true;
        RequestRedraw();
    }

    // ===== STYLE =====
    void UltraCanvasBarcodeElement::SetStyle(const BarcodeStyle& s) {
        style = s;
        RequestRedraw();
    }
    void UltraCanvasBarcodeElement::SetModuleWidth(float w) {
        style.moduleWidth = w; RequestRedraw();
    }
    void UltraCanvasBarcodeElement::SetBarHeight(float h) {
        style.barHeight = h; RequestRedraw();
    }
    void UltraCanvasBarcodeElement::SetQuietZoneModules(float m) {
        style.quietZoneModules = m; RequestRedraw();
    }
    void UltraCanvasBarcodeElement::SetForegroundColor(const Color& c) {
        style.foreground = c; RequestRedraw();
    }
    void UltraCanvasBarcodeElement::SetBackgroundColor(const Color& c) {
        style.background = c; RequestRedraw();
    }
    void UltraCanvasBarcodeElement::SetTextPosition(BarcodeTextPosition p) {
        style.textPosition = p; RequestRedraw();
    }
    void UltraCanvasBarcodeElement::SetTextFontSize(float s) {
        style.textFontSize = s; RequestRedraw();
    }
    void UltraCanvasBarcodeElement::SetTextFontFamily(const std::string& f) {
        style.textFontFamily = f; RequestRedraw();
    }
    void UltraCanvasBarcodeElement::SetTextColor(const Color& c) {
        style.textColor = c; RequestRedraw();
    }
    void UltraCanvasBarcodeElement::SetRotation(BarcodeRotation r) {
        style.rotation = r; RequestRedraw();
    }
    void UltraCanvasBarcodeElement::SetAutoFitToBounds(bool b) {
        style.autoFitToBounds = b; RequestRedraw();
    }

    // ===== ENCODING =====
    void UltraCanvasBarcodeElement::Reencode() {
        pattern = EncodeBarcode(symbology, data, options);
        encodeDirty = false;
        if (!pattern.valid) {
            if (onError) onError(pattern.errorMessage);
        } else if (onEncoded) {
            onEncoded();
        }
    }

    void UltraCanvasBarcodeElement::EnsureEncoded() {
        if (encodeDirty) Reencode();
    }

    // ===== SIZE =====
    Size2Di UltraCanvasBarcodeElement::GetNaturalSize() const {
        if (!pattern.valid || pattern.modules.empty()) return {120, 60};
        float totalModules = pattern.ModuleCount() + 2.0f * style.quietZoneModules;
        float w = totalModules * style.moduleWidth;
        float h = style.barHeight;
        if (style.textPosition == BarcodeTextPosition::TextBelow ||
            style.textPosition == BarcodeTextPosition::TextAbove ||
            style.textPosition == BarcodeTextPosition::TextBothSides) {
            h += style.textFontSize + style.textPadding;
            if (style.textPosition == BarcodeTextPosition::TextBothSides) {
                h += style.textFontSize + style.textPadding;
            }
        }
        // Guard bars extend below; account for them in total height.
        bool hasGuard = false;
        for (auto m : pattern.modules) {
            if (m == BarcodeModuleKind::GuardBar) { hasGuard = true; break; }
        }
        if (hasGuard) h += style.guardBarExtraHeight;
        if (pattern.drawBearerBars) h += 2 * style.bearerBarThickness;
        if (style.rotation == BarcodeRotation::Rotate90 ||
            style.rotation == BarcodeRotation::Rotate270) {
            return {(int)h, (int)w};
        }
        return {(int)w, (int)h};
    }

    // ===== LAYOUT (CSS Measure/Arrange) =====
    Size2Df UltraCanvasBarcodeElement::MeasureOwnContent(
            std::optional<float> /*definiteContentWidth*/,
            const CSSLayout::LayoutContext& /*ctx*/) {
        // Content box = the encoded symbol's natural pixel size (no padding/border).
        // GetNaturalSize() reads `pattern`, so the stream must be encoded first.
        EnsureEncoded();
        Size2Di n = GetNaturalSize();
        return Size2Df((float)n.width, (float)n.height);
    }

    void UltraCanvasBarcodeElement::ComputeIntrinsicSizes(
            const CSSLayout::LayoutContext& /*ctx*/) {
        EnsureEncoded();
        const float padH = GetTotalPaddingHorizontal() + GetTotalBorderHorizontal();
        const float padV = GetTotalPaddingVertical()   + GetTotalBorderVertical();
        Size2Di n = GetNaturalSize();
        fprintf(stderr, "[BCDBG-NAT] %-16s natural=%dx%d\n", GetIdentifier().c_str(), n.width, n.height); // BCDBG temp
        // Max-content (preferred) = the natural symbol size, in BORDER-box units.
        intrinsic.valid = true;
        intrinsic.maxContentWidth  = (float)n.width  + padH;
        intrinsic.maxContentHeight = (float)n.height + padV;
        if (style.autoFitToBounds) {
            // The symbol scales to its bounds, so it can shrink well below natural
            // size. Report a small min-content floor (NOT the full width) so flex/grid
            // can size us down — otherwise a container narrower than the natural width
            // would be forced to overflow (horizontal scroll).
            intrinsic.minContentWidth  = padH + 32.f;
            intrinsic.minContentHeight = padV + 16.f;
        } else {
            // Fixed-size symbol: min-content == max-content (non-shrinkable).
            intrinsic.minContentWidth  = (float)n.width  + padH;
            intrinsic.minContentHeight = (float)n.height + padV;
        }
    }

    void UltraCanvasBarcodeElement::Arrange(const Rect2Df& finalRect,
                                            const CSSLayout::LayoutContext& ctx) {
        // No sub-rects: the symbol is drawn from GetLocalContentRect() + autoFit at
        // render time. The base sets finalBounds + damage tracking.
        UltraCanvasUIElement::Arrange(finalRect, ctx);
    }

    // ===== LAYOUT =====
    UltraCanvasBarcodeElement::LayoutMetrics
    UltraCanvasBarcodeElement::ComputeMetrics(IRenderContext* /*ctx*/, float availW, float availH) const {
        LayoutMetrics m;
        m.moduleWidth = style.moduleWidth;
        m.barHeight = style.barHeight;
        m.guardExtra = 0.0f;
        m.bearerThickness = pattern.drawBearerBars ? style.bearerBarThickness : 0.0f;
        m.quietPx = style.quietZoneModules * style.moduleWidth;
        m.textHeightAbove = 0.0f;
        m.textHeightBelow = 0.0f;

        // Detect guard bars.
        bool hasGuard = false;
        for (auto kind : pattern.modules) {
            if (kind == BarcodeModuleKind::GuardBar) { hasGuard = true; break; }
        }
        if (hasGuard) m.guardExtra = style.guardBarExtraHeight;

        // Text heights.
        const float lineH = style.textFontSize + style.textPadding;
        if (style.textPosition == BarcodeTextPosition::TextAbove ||
            style.textPosition == BarcodeTextPosition::TextBothSides) {
            m.textHeightAbove = lineH;
        }
        if (style.textPosition == BarcodeTextPosition::TextBelow ||
            style.textPosition == BarcodeTextPosition::TextBothSides) {
            m.textHeightBelow = lineH;
        }

        const int nModules = pattern.ModuleCount();
        float naturalWidth = (nModules + 2.0f * style.quietZoneModules) * style.moduleWidth;
        float naturalHeight = m.textHeightAbove + m.barHeight + m.guardExtra +
                              m.textHeightBelow + 2.0f * m.bearerThickness;

        if (style.autoFitToBounds && nModules > 0) {
            // Solve for scale that fits both axes.
            float scaleX = availW / naturalWidth;
            float scaleY = availH / naturalHeight;
            float scale = std::min(scaleX, scaleY);
            if (scale > 0 && scale != 1.0f) {
                // Scale module width and bar/guard/text dims proportionally.
                m.moduleWidth = style.moduleWidth * scale;
                m.barHeight = style.barHeight * scale;
                m.guardExtra = m.guardExtra * scale;
                m.bearerThickness = m.bearerThickness * scale;
                m.quietPx = m.quietPx * scale;
                m.textHeightAbove *= scale;
                m.textHeightBelow *= scale;
            }
            naturalWidth = (nModules + 2.0f * style.quietZoneModules) * m.moduleWidth;
            naturalHeight = m.textHeightAbove + m.barHeight + m.guardExtra +
                            m.textHeightBelow + 2.0f * m.bearerThickness;
        }

        m.totalWidth = naturalWidth;
        m.totalHeight = naturalHeight;

        // Center within available area.
        m.originX = (availW - naturalWidth) * 0.5f;
        m.originY = (availH - naturalHeight) * 0.5f;
        m.barsTopY = m.originY + m.bearerThickness + m.textHeightAbove;

        return m;
    }

    // ===== RENDER =====
    void UltraCanvasBarcodeElement::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        EnsureEncoded();
        Rect2Df local = GetLocalContentRect();

        // Element background + border (handled by base if we delegate).
        UltraCanvasUIElement::Render(ctx, local);

        if (!pattern.valid) {
            RenderError(ctx, local);
            return;
        }
        if (pattern.modules.empty()) {
            return;
        }

        ctx->PushState();
        // Translate to content origin.
        ctx->Translate(local.x, local.y);

        const bool sideways = (style.rotation == BarcodeRotation::Rotate90 ||
                               style.rotation == BarcodeRotation::Rotate270);

        float availW = static_cast<float>(local.width);
        float availH = static_cast<float>(local.height);
        if (sideways) std::swap(availW, availH);

        LayoutMetrics m = ComputeMetrics(ctx, availW, availH);

        // Apply rotation around the local content center.
        if (style.rotation != BarcodeRotation::Rotate0) {
            float cx = local.width * 0.5f;
            float cy = local.height * 0.5f;
            ctx->Translate(cx, cy);
            double angle = 0.0;
            switch (style.rotation) {
                case BarcodeRotation::Rotate90:  angle =  M_PI * 0.5; break;
                case BarcodeRotation::Rotate180: angle =  M_PI;       break;
                case BarcodeRotation::Rotate270: angle = -M_PI * 0.5; break;
                default: break;
            }
            ctx->Rotate(angle);
            ctx->Translate(-availW * 0.5f, -availH * 0.5f);
        }

        // Background fill of the symbol area only (not the whole element).
        ctx->SetFillPaint(style.background);
        ctx->FillRectangle(Rect2Df(m.originX, m.originY, m.totalWidth, m.totalHeight));

        RenderUnrotated(ctx, m);

        ctx->PopState();
    }

    void UltraCanvasBarcodeElement::RenderUnrotated(IRenderContext* ctx, const LayoutMetrics& m) {
        const float startX = m.originX + m.quietPx;
        const float barsTop = m.barsTopY;
        const float barsBottom = barsTop + m.barHeight;
        const float guardBottom = barsBottom + m.guardExtra;

        ctx->SetFillPaint(style.foreground);

        // Walk the module stream, coalescing consecutive bars of the same
        // kind into one filled rect to minimise draw calls.
        int n = pattern.ModuleCount();
        int i = 0;
        while (i < n) {
            BarcodeModuleKind kind = pattern.modules[i];
            if (kind == BarcodeModuleKind::Space) { ++i; continue; }
            int j = i;
            while (j < n && pattern.modules[j] == kind) ++j;
            // Draw the bar block from i..j-1 (one rect).
            float x = startX + i * m.moduleWidth;
            float w = (j - i) * m.moduleWidth;
            float bottom = (kind == BarcodeModuleKind::GuardBar) ? guardBottom : barsBottom;
            ctx->FillRectangle(Rect2Df(x, barsTop, w, bottom - barsTop));
            i = j;
        }

        // Bearer bars (ITF-14).
        if (pattern.drawBearerBars) {
            float bearerH = m.bearerThickness;
            // Bearer top + bottom span the entire symbol width (including quiet zones).
            ctx->FillRectangle(Rect2Df(m.originX, barsTop - bearerH,
                                       m.totalWidth, bearerH));
            ctx->FillRectangle(Rect2Df(m.originX, guardBottom,
                                       m.totalWidth, bearerH));
        }

        // Human-readable text.
        if (style.textPosition != BarcodeTextPosition::HiddenText) {
            ctx->PushState();
            if (!style.textFontFamily.empty()) {
                ctx->SetFontFamily(style.textFontFamily);
            }
            ctx->SetFontSize(style.textFontSize);
            ctx->SetFontWeight(style.textFontWeight);
            ctx->SetCurrentPaint(style.textColor);
            ctx->SetTextPaint(style.textColor);

            float belowY = guardBottom + style.textPadding +
                           (pattern.drawBearerBars ? m.bearerThickness : 0.0f);
            float aboveY = m.originY + (pattern.drawBearerBars ? m.bearerThickness : 0.0f);
            float lineH = style.textFontSize;

            auto drawCentered = [&](const std::string& text, float cx, float topY) {
                if (text.empty()) return;
                Size2Di sz = ctx->GetTextLineDimensions(text);
                ctx->DrawText(text, Point2Df(cx - sz.width * 0.5f, topY));
            };

            if (!pattern.hriSegments.empty()) {
                // EAN/UPC etc — draw each segment under its module range.
                for (const auto& seg : pattern.hriSegments) {
                    float topY = seg.aboveBars ? aboveY : belowY;
                    if (seg.aboveBars && style.textPosition != BarcodeTextPosition::TextAbove &&
                        style.textPosition != BarcodeTextPosition::TextBothSides) {
                        continue;
                    }
                    if (!seg.aboveBars && style.textPosition != BarcodeTextPosition::TextBelow &&
                        style.textPosition != BarcodeTextPosition::TextBothSides) {
                        continue;
                    }
                    float centerX;
                    if (seg.moduleStart < 0) {
                        // Text appears in the left quiet zone (e.g. EAN-13 first digit).
                        centerX = m.originX + m.quietPx * 0.5f;
                    } else if (seg.moduleStart >= pattern.ModuleCount()) {
                        // Text appears in the right quiet zone.
                        centerX = m.originX + m.totalWidth - m.quietPx * 0.5f;
                    } else {
                        float x = startX + seg.moduleStart * m.moduleWidth;
                        centerX = x + seg.moduleWidth * m.moduleWidth * 0.5f;
                    }
                    drawCentered(seg.text, centerX, topY);
                }
            } else if (!pattern.humanReadable.empty()) {
                float centerX = m.originX + m.totalWidth * 0.5f;
                if (style.textPosition == BarcodeTextPosition::TextBelow ||
                    style.textPosition == BarcodeTextPosition::TextBothSides) {
                    drawCentered(pattern.humanReadable, centerX, belowY);
                }
                if (style.textPosition == BarcodeTextPosition::TextAbove ||
                    style.textPosition == BarcodeTextPosition::TextBothSides) {
                    drawCentered(pattern.humanReadable, centerX, aboveY);
                }
            }
            ctx->PopState();
        }
    }

    void UltraCanvasBarcodeElement::RenderError(IRenderContext* ctx, const Rect2Df& local) {
        // Diagonal cross + message in the error color, on the configured
        // background. Lets users immediately see something is wrong.
        ctx->PushState();
        ctx->SetFillPaint(style.background);
        ctx->FillRectangle(Rect2Df(local.x, local.y, local.width, local.height));
        ctx->SetStrokePaint(style.errorColor);
        ctx->SetStrokeWidth(2.0);
        ctx->DrawLine(Point2Df(local.x, local.y),
                      Point2Df(local.x + local.width, local.y + local.height));
        ctx->DrawLine(Point2Df(local.x + local.width, local.y),
                      Point2Df(local.x, local.y + local.height));

        if (!pattern.errorMessage.empty()) {
            ctx->SetFontSize(std::max(10.0f, style.textFontSize));
            ctx->SetCurrentPaint(style.errorColor);
            ctx->SetTextPaint(style.errorColor);
            Size2Di sz = ctx->GetTextLineDimensions(pattern.errorMessage);
            ctx->DrawText(pattern.errorMessage,
                          Point2Df(local.x + (local.width - sz.width) * 0.5f,
                                   local.y + (local.height - sz.height) * 0.5f));
        }
        ctx->PopState();
    }

    // ===== EVENTS =====
    bool UltraCanvasBarcodeElement::OnEvent(const UCEvent& event) {
        if (UltraCanvasUIElement::OnEvent(event)) return true;
        if (event.type == UCEventType::MouseDown && Contains(event.pointer)) {
            if (onClick) onClick();
            return true;
        }
        return false;
    }

    // ===== FACTORIES =====
    std::shared_ptr<UltraCanvasBarcodeElement> CreateBarcode(
        const std::string& id, long x, long y, long w, long h,
        BarcodeSymbology sym, const std::string& d) {
        auto bc = std::make_shared<UltraCanvasBarcodeElement>(id, x, y, w, h);
        bc->Set(sym, d);
        return bc;
    }

    std::shared_ptr<UltraCanvasBarcodeElement> CreateBarcode(
        const std::string& id, long w, long h,
        BarcodeSymbology sym, const std::string& d) {
        return CreateBarcode(id, 0, 0, w, h, sym, d);
    }

    // ===== BUILDER =====
    BarcodeBuilder::BarcodeBuilder(const std::string& id, long x, long y, long w, long h) {
        widget = std::make_shared<UltraCanvasBarcodeElement>(id, x, y, w, h);
    }
    BarcodeBuilder& BarcodeBuilder::SetSymbology(BarcodeSymbology s) {
        widget->SetSymbology(s); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::SetData(const std::string& d) {
        widget->SetData(d); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::SetStyle(const BarcodeStyle& s) {
        widget->SetStyle(s); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::SetModuleWidth(float w) {
        widget->SetModuleWidth(w); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::SetBarHeight(float h) {
        widget->SetBarHeight(h); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::SetQuietZoneModules(float m) {
        widget->SetQuietZoneModules(m); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::SetForegroundColor(const Color& c) {
        widget->SetForegroundColor(c); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::SetBackgroundColor(const Color& c) {
        widget->SetBackgroundColor(c); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::SetTextPosition(BarcodeTextPosition p) {
        widget->SetTextPosition(p); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::SetTextFontSize(float s) {
        widget->SetTextFontSize(s); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::SetTextFontFamily(const std::string& f) {
        widget->SetTextFontFamily(f); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::SetRotation(BarcodeRotation r) {
        widget->SetRotation(r); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::SetAutoFitToBounds(bool b) {
        widget->SetAutoFitToBounds(b); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::SetIncludeChecksum(bool b) {
        BarcodeEncoderOptions o = widget->GetEncoderOptions();
        o.includeChecksum = b;
        widget->SetEncoderOptions(o);
        return *this;
    }
    BarcodeBuilder& BarcodeBuilder::SetMSICheckDigit(MSICheckDigit m) {
        BarcodeEncoderOptions o = widget->GetEncoderOptions();
        o.msiCheckDigit = m;
        widget->SetEncoderOptions(o);
        return *this;
    }
    BarcodeBuilder& BarcodeBuilder::OnError(std::function<void(const std::string&)> cb) {
        widget->onError = std::move(cb); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::OnEncoded(std::function<void()> cb) {
        widget->onEncoded = std::move(cb); return *this;
    }
    BarcodeBuilder& BarcodeBuilder::OnClick(std::function<void()> cb) {
        widget->onClick = std::move(cb); return *this;
    }

} // namespace UltraCanvas
