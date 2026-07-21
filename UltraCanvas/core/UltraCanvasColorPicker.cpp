// core/UltraCanvasColorPicker.cpp
// Implementation of the comprehensive colour picker widget.
// Version: 1.2.5
// Last Modified: 2026-07-21
// Author: UltraCanvas Framework

#include "UltraCanvasColorPicker.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasTooltipManager.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace UltraCanvas {

    // ===================================================================
    // Construction
    // ===================================================================
    UltraCanvasColorPicker::UltraCanvasColorPicker(const std::string& identifier,
                                                   float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        mouseCursor = UCMouseCursor::Default;
        previousColor = GetColor();
    }

    UltraCanvasColorPicker::~UltraCanvasColorPicker() {
        // The screen-pick event filter captures `this`, so it must not outlive
        // the widget.
        screenPickActive = false;
        if (screenPickFilterInstalled) {
            if (auto* win = GetWindow()) {
                win->UnInstallWindowEventFilter(ScreenPickFilterId());
            }
            screenPickFilterInstalled = false;
        }
    }

    // ===================================================================
    // Colour access
    // ===================================================================
    Color UltraCanvasColorPicker::GetColor() const {
        return HSV(hue, sat, val, alpha);
    }

    void UltraCanvasColorPicker::SetColor(const Color& color, bool notify) {
        float h, s, v;
        RGBToHSV(color, h, s, v);
        // Preserve the existing hue when the incoming colour is achromatic
        // (grey/black/white) so the wheel marker does not jump to red.
        if (s > 1e-4f && v > 1e-4f) {
            hue = h;
        }
        sat = s;
        val = v;
        alpha = color.a;
        if (notify) Changed(true);
        else RequestRedraw();
    }

    void UltraCanvasColorPicker::SetAlpha(uint8_t a, bool notify) {
        alpha = a;
        if (notify) Changed(true);
        else RequestRedraw();
    }

    void UltraCanvasColorPicker::SetHSV(float h, float s, float v, bool notify) {
        hue = std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f);
        sat = std::clamp(s, 0.0f, 1.0f);
        val = std::clamp(v, 0.0f, 1.0f);
        if (notify) Changed(true);
        else RequestRedraw();
    }

    void UltraCanvasColorPicker::SetModel(ColorPickerModel m) {
        if (model == m) return;
        model = m;
        if (editField != EditField::NoEdit && editField != EditField::Hex) CancelEdit();
        RequestRedraw();
    }

    void UltraCanvasColorPicker::SetModeSelector(ColorPickerModeSelector s) {
        if (modeSelector == s) return;
        modeSelector = s;
        dropdownOpen = false;
        layoutValid = false;
        RequestRedraw();
    }

    void UltraCanvasColorPicker::SetSliderStyle(ColorPickerSliderStyle s) {
        if (sliderStyle == s) return;
        sliderStyle = s;
        RequestRedraw();
    }

    void UltraCanvasColorPicker::SetWheelStyle(ColorPickerWheelStyle s) {
        if (wheelStyle == s) return;
        wheelStyle = s;
        layoutValid = false;
        RequestRedraw();
    }

    void UltraCanvasColorPicker::SetShowValueSpinners(bool show) {
        if (showValueSpinners == show) return;
        showValueSpinners = show;
        RequestRedraw();
    }

    void UltraCanvasColorPicker::SetSlidersCollapsible(bool collapsible, bool expanded) {
        if (slidersCollapsible == collapsible && slidersExpanded == expanded) return;
        slidersCollapsible = collapsible;
        slidersExpanded = expanded;
        layoutValid = false;
        RequestRedraw();
    }

    void UltraCanvasColorPicker::SetSlidersExpanded(bool expanded) {
        if (slidersExpanded == expanded) return;
        slidersExpanded = expanded;
        layoutValid = false;
        RequestRedraw();
    }

    void UltraCanvasColorPicker::SetShowColorWheel(bool show) {
        if (showColorWheel == show) return;
        showColorWheel = show;
        layoutValid = false;
        RequestRedraw();
    }

    void UltraCanvasColorPicker::SetShowAlpha(bool show) {
        if (showAlpha == show) return;
        showAlpha = show;
        layoutValid = false;
        RequestRedraw();
    }

    void UltraCanvasColorPicker::SetUIScale(float scale) {
        scale = std::max(0.1f, scale);
        if (style.uiScale == scale) return;
        style.uiScale = scale;
        layoutValid = false;
        RequestRedraw();
    }

    void UltraCanvasColorPicker::Changed(bool finished) {
        RequestRedraw();
        Color c = GetColor();
        if (editingBackground) {
            // The working state currently holds the background colour being
            // edited: mirror it into the background swatch live and notify via
            // the background callbacks instead of the foreground ones.
            previousColor = c;
            if (onBackgroundChanging) onBackgroundChanging(c);
            if (finished && onBackgroundChanged) onBackgroundChanged(c);
            return;
        }
        if (onColorChanging) onColorChanging(c);
        if (finished && onColorChanged) onColorChanged(c);
    }

    void UltraCanvasColorPicker::SetFont(IRenderContext* ctx, bool muted) const {
        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(Scaled(style.fontSize));
        ctx->SetTextPaint(muted ? style.mutedTextColor : style.textColor);
    }

    // ===================================================================
    // Layout
    // ===================================================================
    void UltraCanvasColorPicker::RecalculateLayout() {
        const float W = GetWidth();
        const float H = GetHeight();
        if (layoutValid && cachedW == W && cachedH == H) return;
        cachedW = W;
        cachedH = H;

        const float pad = Scaled(style.padding);
        const float gap = Scaled(style.rowGap);
        const float x0 = pad;
        const float innerW = std::max(40.0f, W - 2.0f * pad);
        const float rightEdge = x0 + innerW;
        const float rh = Scaled(style.rowHeight);
        const float tabH = Scaled(style.tabHeight);

        const int nRows = 3 + (showAlpha ? 1 : 0);
        const float rowsH = SlidersVisible() ? nRows * (rh + gap) : 0.0f;
        const float headerH = slidersCollapsible ? (rh + gap) : 0.0f;
        const float previewRowH = 2.0f * rh + gap;       // swatches / hex block
        const float tabsH = (modeSelector == ColorPickerModeSelector::TabBar)
                            ? (tabH + gap) : 0.0f;
        const float bottomH = previewRowH + gap + tabsH + headerH + rowsH;

        // --- Optional colour wheel occupies the space above the controls ---
        float cursorY = pad;
        if (showColorWheel) {
            float wheelAreaH = H - pad - bottomH - gap - pad;
            if (wheelStyle == ColorPickerWheelStyle::Ring) {
                float wheelSize = std::min(innerW, std::max(60.0f, wheelAreaH));
                wheelRect = Rect2Df(x0 + (innerW - wheelSize) * 0.5f, pad, wheelSize, wheelSize);
                wheelCenter = Point2Df(wheelRect.x + wheelSize * 0.5f, wheelRect.y + wheelSize * 0.5f);
                ringOuter = wheelSize * 0.5f;
                ringInner = std::max(10.0f, ringOuter - Scaled(style.ringThickness));
                float sq = ringInner * 1.41421356f * 0.86f;   // inscribed square (with margin)
                svRect = Rect2Df(wheelCenter.x - sq * 0.5f, wheelCenter.y - sq * 0.5f, sq, sq);
                hueBarRect = Rect2Df();
                cursorY = wheelRect.y + wheelSize + pad;
            } else {
                // Bar style: the SV rectangle grows to the full inner width
                // (with the padding as margin) and the hue range sits under it
                // as a horizontal bar.
                float hueH = Scaled(style.hueBarHeight);
                float svH = std::max(40.0f, wheelAreaH - hueH - gap);
                svRect = Rect2Df(x0, pad, innerW, svH);
                hueBarRect = Rect2Df(x0, svRect.y + svRect.height + gap, innerW, hueH);
                wheelRect = Rect2Df();
                wheelCenter = Point2Df();
                ringOuter = ringInner = 0;
                cursorY = hueBarRect.y + hueBarRect.height + pad;
            }
        } else {
            wheelRect = Rect2Df();
            svRect = Rect2Df();
            hueBarRect = Rect2Df();
            cursorY = pad;
        }

        // --- Preview row: swatches on the left, mode + hex on the right ---
        const float swatch = std::min(Scaled(style.swatchSize), previewRowH);
        currentSwatchRect  = Rect2Df(x0, cursorY, swatch, swatch);
        previousSwatchRect = Rect2Df(x0 + swatch * 0.55f, cursorY + swatch * 0.45f, swatch, swatch);

        // Swap symbol tucked into the free top-right corner of the swatch pair
        // (right of the foreground swatch, above the background swatch).
        swapArrowRect = Rect2Df(x0 + swatch + Scaled(3.0f), cursorY,
                                swatch * 0.5f, swatch * 0.42f);

        // Eyedropper (screen colour picker) sits just right of the background
        // swatch, bottom-aligned with it.
        const float iconSize = Scaled(20.0f);
        screenPickRect = Rect2Df(previousSwatchRect.x + previousSwatchRect.width + Scaled(6.0f),
                                 previousSwatchRect.y + previousSwatchRect.height - iconSize,
                                 iconSize, iconSize);

        const float hexFieldW = std::min(Scaled(120.0f), innerW * 0.45f);
        if (modeSelector == ColorPickerModeSelector::Dropdown) {
            const float modeW = Scaled(64.0f);
            modeButtonRect = Rect2Df(rightEdge - modeW, cursorY, modeW, rh);
            hexFieldRect = Rect2Df(rightEdge - hexFieldW, cursorY + rh + gap, hexFieldW, rh);
        } else {
            // Tab bar mode: no dropdown button, the hex field takes the top line.
            modeButtonRect = Rect2Df();
            hexFieldRect = Rect2Df(rightEdge - hexFieldW, cursorY, hexFieldW, rh);
        }
        hexLabelRect = Rect2Df(hexFieldRect.x - Scaled(40.0f), hexFieldRect.y,
                               Scaled(34.0f), rh);

        cursorY += previewRowH + gap;

        // --- Mode tabs (HSV / HSL / RGB) — TabBar selector only ---
        if (modeSelector == ColorPickerModeSelector::TabBar) {
            tabRect = Rect2Df(x0, cursorY, innerW, tabH);
            const float tabW = innerW / 3.0f;
            for (int i = 0; i < 3; ++i) {
                tabRects[i] = Rect2Df(x0 + tabW * i, cursorY, tabW, tabH);
            }
            cursorY += tabH + gap;
        } else {
            tabRect = Rect2Df();
            for (auto& r : tabRects) r = Rect2Df();
        }

        // --- Disclosure header for collapsible sliders ---
        if (slidersCollapsible) {
            slidersHeaderRect = Rect2Df(x0, cursorY, innerW, rh);
            cursorY += rh + gap;
        } else {
            slidersHeaderRect = Rect2Df();
        }

        // --- Channel rows ---
        const float labelW = Scaled(18.0f);
        const float valueW = Scaled(showValueSpinners ? 68.0f : 56.0f);
        for (int i = 0; i < nRows; ++i) {
            if (!SlidersVisible()) {
                rowLabelRects[i] = Rect2Df();
                rowValueRects[i] = Rect2Df();
                rowSliderRects[i] = Rect2Df();
                continue;
            }
            float y = cursorY + i * (rh + gap);
            rowLabelRects[i] = Rect2Df(x0, y, labelW, rh);
            rowValueRects[i] = Rect2Df(rightEdge - valueW, y, valueW, rh);
            float sliderX = x0 + labelW + Scaled(6.0f);
            float sliderW = std::max(20.0f, (rowValueRects[i].x - Scaled(8.0f)) - sliderX);
            rowSliderRects[i] = Rect2Df(sliderX, y, sliderW, rh);
        }
        for (int i = nRows; i < 4; ++i) {
            rowLabelRects[i] = Rect2Df();
            rowValueRects[i] = Rect2Df();
            rowSliderRects[i] = Rect2Df();
        }

        layoutValid = true;
    }

    Rect2Df UltraCanvasColorPicker::SpinnerDownRect(int row) const {
        const Rect2Df& vb = rowValueRects[row];
        float aw = Scaled(14.0f);
        return Rect2Df(vb.x, vb.y, aw, vb.height);
    }

    Rect2Df UltraCanvasColorPicker::SpinnerUpRect(int row) const {
        const Rect2Df& vb = rowValueRects[row];
        float aw = Scaled(14.0f);
        return Rect2Df(vb.x + vb.width - aw, vb.y, aw, vb.height);
    }

    void UltraCanvasColorPicker::StepChannel(int row, float direction) {
        if (row == 3) {
            int a = (int)alpha + (int)direction;
            alpha = (uint8_t)std::clamp(a, 0, 255);
        } else {
            std::string label; float value, mn, mx;
            GetChannelInfo(row, label, value, mn, mx);
            SetChannelValue(row, std::clamp(value + direction, mn, mx));
        }
        Changed(true);
    }

    // ===================================================================
    // Channel model glue
    // ===================================================================
    void UltraCanvasColorPicker::GetChannelInfo(int row, std::string& label, float& value,
                                                float& minV, float& maxV) const {
        switch (model) {
            case ColorPickerModel::HSV: {
                const char* labels[3] = {"H", "S", "V"};
                float vals[3] = {hue, sat * 100.0f, val * 100.0f};
                float maxes[3] = {360.0f, 100.0f, 100.0f};
                label = labels[row]; value = vals[row]; minV = 0.0f; maxV = maxes[row];
                break;
            }
            case ColorPickerModel::HSL: {
                float h, s, l;
                RGBToHSL(GetColor(), h, s, l);
                if (sat > 1e-4f && val > 1e-4f) h = hue; // keep canonical hue
                const char* labels[3] = {"H", "S", "L"};
                float vals[3] = {h, s * 100.0f, l * 100.0f};
                float maxes[3] = {360.0f, 100.0f, 100.0f};
                label = labels[row]; value = vals[row]; minV = 0.0f; maxV = maxes[row];
                break;
            }
            case ColorPickerModel::RGB: {
                Color c = GetColor();
                const char* labels[3] = {"R", "G", "B"};
                float vals[3] = {(float)c.r, (float)c.g, (float)c.b};
                label = labels[row]; value = vals[row]; minV = 0.0f; maxV = 255.0f;
                break;
            }
        }
    }

    void UltraCanvasColorPicker::SetChannelValue(int row, float value) {
        switch (model) {
            case ColorPickerModel::HSV: {
                if (row == 0) hue = std::clamp(value, 0.0f, 360.0f);
                else if (row == 1) sat = std::clamp(value / 100.0f, 0.0f, 1.0f);
                else val = std::clamp(value / 100.0f, 0.0f, 1.0f);
                break;
            }
            case ColorPickerModel::HSL: {
                float h, s, l;
                RGBToHSL(GetColor(), h, s, l);
                if (sat > 1e-4f && val > 1e-4f) h = hue;
                if (row == 0) h = std::clamp(value, 0.0f, 360.0f);
                else if (row == 1) s = std::clamp(value / 100.0f, 0.0f, 1.0f);
                else l = std::clamp(value / 100.0f, 0.0f, 1.0f);
                Color c = HSLToRGB(h, s, l, alpha);
                float nh, ns, nv;
                RGBToHSV(c, nh, ns, nv);
                hue = (ns > 1e-4f && nv > 1e-4f) ? nh : h; // h and HSV-hue are identical
                sat = ns; val = nv;
                break;
            }
            case ColorPickerModel::RGB: {
                Color c = GetColor();
                int iv = (int)std::lround(std::clamp(value, 0.0f, 255.0f));
                if (row == 0) c.r = (uint8_t)iv;
                else if (row == 1) c.g = (uint8_t)iv;
                else c.b = (uint8_t)iv;
                float nh, ns, nv;
                RGBToHSV(c, nh, ns, nv);
                if (ns > 1e-4f && nv > 1e-4f) hue = nh;
                sat = ns; val = nv;
                break;
            }
        }
    }

    std::string UltraCanvasColorPicker::FormatChannel(int row) const {
        std::string label; float value, mn, mx;
        GetChannelInfo(row, label, value, mn, mx);
        char buf[32];
        if (model == ColorPickerModel::RGB) {
            std::snprintf(buf, sizeof(buf), "%d", (int)std::lround(value));
        } else {
            std::snprintf(buf, sizeof(buf), "%.1f", value);
        }
        return std::string(buf);
    }

    std::shared_ptr<IPaintPattern> UltraCanvasColorPicker::MakeChannelGradient(
            IRenderContext* ctx, int row) const {
        const Rect2Df& tr = rowSliderRects[row];
        double x1 = tr.x, x2 = tr.x + tr.width, y = tr.y + tr.height * 0.5;
        std::vector<GradientStop> stops;

        switch (model) {
            case ColorPickerModel::HSV: {
                if (row == 0) {
                    for (int i = 0; i <= 6; ++i)
                        stops.emplace_back(i / 6.0, HSV(i * 60.0f, 1.0f, 1.0f));
                } else if (row == 1) {
                    stops.emplace_back(0.0, HSV(hue, 0.0f, val));
                    stops.emplace_back(1.0, HSV(hue, 1.0f, val));
                } else {
                    stops.emplace_back(0.0, HSV(hue, sat, 0.0f));
                    stops.emplace_back(1.0, HSV(hue, sat, 1.0f));
                }
                break;
            }
            case ColorPickerModel::HSL: {
                float h, s, l;
                RGBToHSL(GetColor(), h, s, l);
                if (sat > 1e-4f && val > 1e-4f) h = hue;
                if (row == 0) {
                    for (int i = 0; i <= 6; ++i)
                        stops.emplace_back(i / 6.0, HSLToRGB(i * 60.0f, 1.0f, 0.5f));
                } else if (row == 1) {
                    stops.emplace_back(0.0, HSLToRGB(h, 0.0f, l));
                    stops.emplace_back(1.0, HSLToRGB(h, 1.0f, l));
                } else {
                    stops.emplace_back(0.0, HSLToRGB(h, s, 0.0f));
                    stops.emplace_back(0.5, HSLToRGB(h, s, 0.5f));
                    stops.emplace_back(1.0, HSLToRGB(h, s, 1.0f));
                }
                break;
            }
            case ColorPickerModel::RGB: {
                Color c = GetColor();
                if (row == 0) {
                    stops.emplace_back(0.0, Color(0, c.g, c.b));
                    stops.emplace_back(1.0, Color(255, c.g, c.b));
                } else if (row == 1) {
                    stops.emplace_back(0.0, Color(c.r, 0, c.b));
                    stops.emplace_back(1.0, Color(c.r, 255, c.b));
                } else {
                    stops.emplace_back(0.0, Color(c.r, c.g, 0));
                    stops.emplace_back(1.0, Color(c.r, c.g, 255));
                }
                break;
            }
        }
        return ctx->CreateLinearGradientPattern(x1, y, x2, y, stops);
    }

    // ===================================================================
    // Rendering
    // ===================================================================
    void UltraCanvasColorPicker::DrawCheckerboard(IRenderContext* ctx, const Rect2Df& rect, float cell) {
        ctx->SetFillPaint(Color(255, 255, 255, 255));
        ctx->FillRectangle(Rect2Dd(rect.x, rect.y, rect.width, rect.height));
        ctx->SetFillPaint(Color(190, 190, 190, 255));
        int cols = (int)std::ceil(rect.width / cell);
        int rows = (int)std::ceil(rect.height / cell);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (((r + c) & 1) == 0) continue;
                float cx = rect.x + c * cell;
                float cy = rect.y + r * cell;
                float cw = std::min(cell, rect.x + rect.width - cx);
                float ch = std::min(cell, rect.y + rect.height - cy);
                ctx->FillRectangle(Rect2Dd(cx, cy, cw, ch));
            }
        }
    }

    void UltraCanvasColorPicker::RenderHueRing(IRenderContext* ctx) {
        // Hue 0 (red) sits at the left of the ring; hue increases clockwise.
        // screenAngle(deg) = hue + 180, measured in screen space (y points down).
        const int step = 2;
        const float r0 = ringInner, r1 = ringOuter;
        // Each wedge is filled separately, so anti-aliasing along the shared
        // edge between neighbouring wedges leaves a partially-covered seam that
        // lets the background bleed through. Extend every wedge slightly past
        // its end angle so consecutive (opaque) wedges overlap and the seam is
        // covered. The overlap is sub-pixel in colour and never visible.
        const float overlap = 0.5f * (float)M_PI / 180.0f;
        for (int i = 0; i < 360; i += step) {
            float a0 = (i + 180.0f) * (float)M_PI / 180.0f;
            float a1 = (i + step + 180.0f) * (float)M_PI / 180.0f + overlap;
            std::vector<Point2Dd> quad = {
                Point2Dd(wheelCenter.x + r0 * std::cos(a0), wheelCenter.y + r0 * std::sin(a0)),
                Point2Dd(wheelCenter.x + r1 * std::cos(a0), wheelCenter.y + r1 * std::sin(a0)),
                Point2Dd(wheelCenter.x + r1 * std::cos(a1), wheelCenter.y + r1 * std::sin(a1)),
                Point2Dd(wheelCenter.x + r0 * std::cos(a1), wheelCenter.y + r0 * std::sin(a1))
            };
            ctx->SetFillPaint(HSV((float)i, 1.0f, 1.0f));
            ctx->FillLinePath(quad);
        }

        // Hue marker
        float am = (hue + 180.0f) * (float)M_PI / 180.0f;
        float rm = (ringInner + ringOuter) * 0.5f;
        Point2Dd mp(wheelCenter.x + rm * std::cos(am), wheelCenter.y + rm * std::sin(am));
        float mr = Scaled(style.ringThickness) * 0.42f;
        ctx->SetFillPaint(HSV(hue, 1.0f, 1.0f));
        ctx->FillCircle(mp, mr);
        ctx->SetStrokePaint(style.markerColor);
        ctx->SetStrokeWidth(2.0);
        ctx->DrawCircle(mp, mr);
        ctx->SetStrokePaint(style.markerOutline);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawCircle(mp, mr + 1.5f);
    }

    void UltraCanvasColorPicker::RenderHueBar(IRenderContext* ctx) {
        // Full hue range painted inside a thick bar; the control point travels
        // inside the bar (Slider-demo palette style).
        Rect2Dd bar(hueBarRect.x, hueBarRect.y, hueBarRect.width, hueBarRect.height);
        double radius = bar.height * 0.5;
        std::vector<GradientStop> stops;
        for (int i = 0; i <= 6; ++i)
            stops.emplace_back(i / 6.0, HSV(i * 60.0f, 1.0f, 1.0f));
        auto grad = ctx->CreateLinearGradientPattern(bar.x, bar.y, bar.x + bar.width, bar.y, stops);
        ctx->SetFillPaint(grad);
        ctx->FillRoundedRectangle(bar, radius);
        ctx->SetStrokePaint(style.fieldBorderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRoundedRectangle(bar, radius);

        // Handle inside the bar
        float hr = std::max(3.0f, (float)radius - Scaled(2.0f));
        float t = std::clamp(hue / 360.0f, 0.0f, 1.0f);
        float cx = hueBarRect.x + radius + t * (hueBarRect.width - 2.0f * radius);
        float cy = hueBarRect.y + hueBarRect.height * 0.5f;
        ctx->SetFillPaint(HSV(hue, 1.0f, 1.0f));
        ctx->FillCircle(Point2Dd(cx, cy), hr);
        ctx->SetStrokePaint(style.markerColor);
        ctx->SetStrokeWidth(2.0);
        ctx->DrawCircle(Point2Dd(cx, cy), hr);
    }

    void UltraCanvasColorPicker::RenderSVSquare(IRenderContext* ctx) {
        Rect2Dd sq(svRect.x, svRect.y, svRect.width, svRect.height);

        // Base: white -> full-saturation hue (horizontal)
        auto hueGrad = ctx->CreateLinearGradientPattern(
                sq.x, sq.y, sq.x + sq.width, sq.y,
                {GradientStop(0.0, Colors::White), GradientStop(1.0, HSV(hue, 1.0f, 1.0f))});
        ctx->SetFillPaint(hueGrad);
        ctx->FillRectangle(sq);

        // Overlay: transparent -> black (vertical) for the value axis
        auto valGrad = ctx->CreateLinearGradientPattern(
                sq.x, sq.y, sq.x, sq.y + sq.height,
                {GradientStop(0.0, Color(0, 0, 0, 0)), GradientStop(1.0, Color(0, 0, 0, 255))});
        ctx->SetFillPaint(valGrad);
        ctx->FillRectangle(sq);

        ctx->SetStrokePaint(style.borderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRectangle(sq);

        // Selection marker
        float mx = svRect.x + sat * svRect.width;
        float my = svRect.y + (1.0f - val) * svRect.height;
        ctx->SetStrokePaint(style.markerColor);
        ctx->SetStrokeWidth(2.0);
        ctx->DrawCircle(Point2Dd(mx, my), Scaled(6.0f));
        ctx->SetStrokePaint(style.markerOutline);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawCircle(Point2Dd(mx, my), Scaled(7.5f));
    }

    void UltraCanvasColorPicker::RenderSwatches(IRenderContext* ctx) {
        // While the eyedropper is armed, the target swatch (chosen by the button
        // used on the icon) live-previews the pixel under the pointer and is
        // outlined in the accent colour. The other swatch renders normally.
        bool previewBg = screenPickActive && screenPickPreviewValid && !screenPickForeground;
        bool previewFg = screenPickActive && screenPickPreviewValid &&  screenPickForeground;
        bool targetBg  = screenPickActive && !screenPickForeground;
        bool targetFg  = screenPickActive &&  screenPickForeground;

        // During an Adjust (right-button) edit the working HSV state holds the
        // background colour, so the foreground swatch must show the saved
        // foreground rather than GetColor(); the background swatch tracks the
        // live edit (mirrored into previousColor). Outline the background swatch
        // to signal it is the one being edited.
        Color foreground = editingBackground
                           ? HSV(fgSaveHue, fgSaveSat, fgSaveVal, fgSaveAlpha)
                           : GetColor();

        // Previous (behind), then current (in front)
        DrawCheckerboard(ctx, previousSwatchRect, Scaled(8.0f));
        ctx->SetFillPaint(previewBg ? screenPickPreview : previousColor);
        ctx->FillRectangle(Rect2Dd(previousSwatchRect.x, previousSwatchRect.y,
                                   previousSwatchRect.width, previousSwatchRect.height));
        ctx->SetStrokePaint((targetBg || editingBackground) ? style.accentColor : style.borderColor);
        ctx->SetStrokeWidth((targetBg || editingBackground) ? 1.5 : 1.0);
        ctx->DrawRectangle(Rect2Dd(previousSwatchRect.x, previousSwatchRect.y,
                                   previousSwatchRect.width, previousSwatchRect.height));

        DrawCheckerboard(ctx, currentSwatchRect, Scaled(8.0f));
        ctx->SetFillPaint(previewFg ? screenPickPreview : foreground);
        ctx->FillRectangle(Rect2Dd(currentSwatchRect.x, currentSwatchRect.y,
                                   currentSwatchRect.width, currentSwatchRect.height));
        ctx->SetStrokePaint(targetFg ? style.accentColor : style.borderColor);
        ctx->SetStrokeWidth(targetFg ? 1.5 : 1.0);
        ctx->DrawRectangle(Rect2Dd(currentSwatchRect.x, currentSwatchRect.y,
                                   currentSwatchRect.width, currentSwatchRect.height));
        ctx->SetStrokeWidth(1.0);

        // Swap symbol: bent double-headed arrow in the corner between the two
        // swatches — one head points at the foreground swatch (left), the other
        // down at the background swatch.
        const float L = swapArrowRect.x, T = swapArrowRect.y;
        const float Wd = swapArrowRect.width, Ht = swapArrowRect.height;
        const float ah = std::max(2.0f, Scaled(3.5f));    // arrowhead size
        Point2Dd leftEnd(L + Wd * 0.05f, T + Ht * 0.30f);
        Point2Dd corner (L + Wd * 0.78f, T + Ht * 0.30f);
        Point2Dd downEnd(L + Wd * 0.78f, T + Ht * 0.95f);
        ctx->SetStrokePaint(style.mutedTextColor);
        ctx->SetStrokeWidth(1.5);
        ctx->DrawLine(leftEnd, corner);
        ctx->DrawLine(corner, downEnd);
        ctx->DrawLine(leftEnd, Point2Dd(leftEnd.x + ah, leftEnd.y - ah));
        ctx->DrawLine(leftEnd, Point2Dd(leftEnd.x + ah, leftEnd.y + ah));
        ctx->DrawLine(downEnd, Point2Dd(downEnd.x - ah, downEnd.y - ah));
        ctx->DrawLine(downEnd, Point2Dd(downEnd.x + ah, downEnd.y - ah));
    }

    void UltraCanvasColorPicker::RenderModeButton(IRenderContext* ctx) {
        ctx->SetFillPaint(style.fieldColor);
        ctx->FillRoundedRectangle(Rect2Dd(modeButtonRect.x, modeButtonRect.y,
                                          modeButtonRect.width, modeButtonRect.height),
                                  Scaled(style.cornerRadius));
        ctx->SetStrokePaint(style.fieldBorderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRoundedRectangle(Rect2Dd(modeButtonRect.x, modeButtonRect.y,
                                          modeButtonRect.width, modeButtonRect.height),
                                  Scaled(style.cornerRadius));

        const char* names[3] = {"HSV", "HSL", "RGB"};
        SetFont(ctx);
        ctx->SetTextAlignment(TextAlignment::Left);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect(names[(int)model],
                            Rect2Dd(modeButtonRect.x + Scaled(8.0f), modeButtonRect.y,
                                    modeButtonRect.width - Scaled(20.0f), modeButtonRect.height));

        // Down triangle
        float tx = modeButtonRect.x + modeButtonRect.width - Scaled(14.0f);
        float ty = modeButtonRect.y + modeButtonRect.height * 0.5f - Scaled(2.0f);
        ctx->SetFillPaint(style.mutedTextColor);
        ctx->FillLinePath({Point2Dd(tx, ty), Point2Dd(tx + Scaled(8.0f), ty),
                           Point2Dd(tx + Scaled(4.0f), ty + Scaled(5.0f))});
    }

    void UltraCanvasColorPicker::RenderScreenPickButton(IRenderContext* ctx) {
        Rect2Dd r(screenPickRect.x, screenPickRect.y,
                  screenPickRect.width, screenPickRect.height);
        ctx->SetFillPaint(screenPickActive ? style.panelColor : style.fieldColor);
        ctx->FillRoundedRectangle(r, Scaled(style.cornerRadius));
        ctx->SetStrokePaint(screenPickActive ? style.accentColor : style.fieldBorderColor);
        ctx->SetStrokeWidth(screenPickActive ? 1.5 : 1.0);
        ctx->DrawRoundedRectangle(r, Scaled(style.cornerRadius));

        // Eyedropper glyph: a diagonal barrel with a squeeze-bulb at the top-right
        // and a pointed tip at the bottom-left.
        const float x = screenPickRect.x, y = screenPickRect.y;
        const float w = screenPickRect.width, h = screenPickRect.height;
        Point2Dd tip(x + w * 0.26f, y + h * 0.74f);   // dropper tip (bottom-left)
        Point2Dd neck(x + w * 0.60f, y + h * 0.40f);  // where barrel meets bulb
        ctx->SetStrokePaint(style.textColor);
        ctx->SetStrokeWidth(2.0);
        ctx->DrawLine(tip, neck);
        // Squeeze bulb
        ctx->SetFillPaint(style.accentColor);
        ctx->FillCircle(Point2Dd(x + w * 0.71f, y + h * 0.29f), w * 0.15f);
        // Drop at the tip
        ctx->SetFillPaint(style.textColor);
        ctx->FillCircle(tip, w * 0.07f);
    }

    void UltraCanvasColorPicker::RenderHexField(IRenderContext* ctx) {
        SetFont(ctx, true);
        ctx->SetTextAlignment(TextAlignment::Right);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect("Hex", Rect2Dd(hexLabelRect.x, hexLabelRect.y,
                                           hexLabelRect.width, hexLabelRect.height));

        bool editing = (editField == EditField::Hex);
        ctx->SetFillPaint(style.fieldColor);
        ctx->FillRoundedRectangle(Rect2Dd(hexFieldRect.x, hexFieldRect.y,
                                          hexFieldRect.width, hexFieldRect.height),
                                  Scaled(style.cornerRadius));
        ctx->SetStrokePaint(editing ? style.accentColor : style.fieldBorderColor);
        ctx->SetStrokeWidth(editing ? 1.5 : 1.0);
        ctx->DrawRoundedRectangle(Rect2Dd(hexFieldRect.x, hexFieldRect.y,
                                          hexFieldRect.width, hexFieldRect.height),
                                  Scaled(style.cornerRadius));

        if (editing) {
            RenderEditableText(ctx);
        } else {
            SetFont(ctx);
            ctx->SetTextAlignment(TextAlignment::Left);
            ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
            ctx->DrawTextInRect(GetColor().ToHexStringWithAlpha(),
                                Rect2Dd(hexFieldRect.x + Scaled(8.0f), hexFieldRect.y,
                                        hexFieldRect.width - Scaled(12.0f), hexFieldRect.height));
        }
    }

    void UltraCanvasColorPicker::RenderTabs(IRenderContext* ctx) {
        const char* names[3] = {"HSV", "HSL", "RGB"};
        ctx->SetFillPaint(style.fieldColor);
        ctx->FillRoundedRectangle(Rect2Dd(tabRect.x, tabRect.y, tabRect.width, tabRect.height),
                                  Scaled(style.cornerRadius));
        for (int i = 0; i < 3; ++i) {
            bool active = ((int)model == i);
            const Rect2Df& tb = tabRects[i];
            if (active) {
                ctx->SetFillPaint(style.panelColor);
                ctx->FillRoundedRectangle(Rect2Dd(tb.x + 1, tb.y + 1, tb.width - 2, tb.height - 2),
                                          Scaled(style.cornerRadius));
            }
            SetFont(ctx, !active);
            ctx->SetTextAlignment(TextAlignment::Center);
            ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
            ctx->SetTextPaint(active ? style.textColor : style.mutedTextColor);
            ctx->DrawTextInRect(names[i], Rect2Dd(tb.x, tb.y, tb.width, tb.height));
        }
        ctx->SetStrokePaint(style.fieldBorderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRoundedRectangle(Rect2Dd(tabRect.x, tabRect.y, tabRect.width, tabRect.height),
                                  Scaled(style.cornerRadius));
    }

    void UltraCanvasColorPicker::RenderSlidersHeader(IRenderContext* ctx) {
        const Rect2Df& hr = slidersHeaderRect;
        // Disclosure triangle: right-pointing when collapsed, down when expanded.
        float cx = hr.x + Scaled(4.0f);
        float cy = hr.y + hr.height * 0.5f;
        float s = Scaled(5.0f);
        ctx->SetFillPaint(style.mutedTextColor);
        if (slidersExpanded) {
            ctx->FillLinePath({Point2Dd(cx, cy - s * 0.6f), Point2Dd(cx + 2 * s, cy - s * 0.6f),
                               Point2Dd(cx + s, cy + s)});
        } else {
            ctx->FillLinePath({Point2Dd(cx, cy - s), Point2Dd(cx + s * 1.6f, cy),
                               Point2Dd(cx, cy + s)});
        }
        SetFont(ctx, true);
        ctx->SetTextAlignment(TextAlignment::Left);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect(slidersExpanded ? "Channels" : ("Channels  (" +
                            GetColor().ToHexStringWithAlpha() + ")"),
                            Rect2Dd(hr.x + Scaled(18.0f), hr.y,
                                    hr.width - Scaled(20.0f), hr.height));
    }

    void UltraCanvasColorPicker::RenderSliderTrackAndHandle(
            IRenderContext* ctx, int row, float t,
            const std::shared_ptr<IPaintPattern>& gradient, bool checkerUnder) {
        const Rect2Df& tr = rowSliderRects[row];
        t = std::clamp(t, 0.0f, 1.0f);

        if (sliderStyle == ColorPickerSliderStyle::Thick) {
            // Thick bar that encloses the control point: full row height with
            // rounded end caps, handle circle travelling inside the bar.
            float trackH = tr.height - Scaled(2.0f);
            Rect2Df trackF(tr.x, tr.y + (tr.height - trackH) * 0.5f, tr.width, trackH);
            Rect2Dd track(trackF.x, trackF.y, trackF.width, trackF.height);
            double radius = trackH * 0.5;
            if (checkerUnder) {
                // Clip the checkerboard to the rounded track so its square
                // corners don't poke out past the rounded gradient — the alpha
                // slider then reads as a rounded bar, matching the other
                // channels. Wrapped in Push/PopState so the clip is scoped to
                // the checkerboard and the parent dirty-rect clip is restored.
                ctx->PushState();
                ctx->ClipRoundedRectangle(track, radius, radius, radius, radius);
                DrawCheckerboard(ctx, trackF, Scaled(6.0f));
                ctx->PopState();
            }
            ctx->SetFillPaint(gradient);
            ctx->FillRoundedRectangle(track, radius);
            ctx->SetStrokePaint(style.fieldBorderColor);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawRoundedRectangle(track, radius);

            float hr = std::max(3.0f, (float)radius - Scaled(2.0f));
            float hx = trackF.x + (float)radius + t * (trackF.width - 2.0f * (float)radius);
            float hy = trackF.y + trackH * 0.5f;
            ctx->SetFillPaint(Colors::White);
            ctx->FillCircle(Point2Dd(hx, hy), hr);
            ctx->SetStrokePaint(style.accentColor);
            ctx->SetStrokeWidth(1.5);
            ctx->DrawCircle(Point2Dd(hx, hy), hr);
        } else {
            // Thin track with the handle overhanging it.
            float trackH = Scaled(8.0f);
            Rect2Df trackF(tr.x, tr.y + tr.height * 0.5f - trackH * 0.5f, tr.width, trackH);
            Rect2Dd track(trackF.x, trackF.y, trackF.width, trackF.height);
            double radius = trackH * 0.5;
            if (checkerUnder) {
                // Round the checkerboard's ends to the track shape (see the
                // Thick branch above) so the alpha slider has round ends.
                ctx->PushState();
                ctx->ClipRoundedRectangle(track, radius, radius, radius, radius);
                DrawCheckerboard(ctx, trackF, Scaled(6.0f));
                ctx->PopState();
            }
            ctx->SetFillPaint(gradient);
            ctx->FillRoundedRectangle(track, trackH * 0.5);
            ctx->SetStrokePaint(style.fieldBorderColor);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawRoundedRectangle(track, trackH * 0.5);

            float hx = tr.x + t * tr.width;
            float hy = tr.y + tr.height * 0.5f;
            ctx->SetFillPaint(Colors::White);
            ctx->FillCircle(Point2Dd(hx, hy), Scaled(6.0f));
            ctx->SetStrokePaint(style.accentColor);
            ctx->SetStrokeWidth(1.5);
            ctx->DrawCircle(Point2Dd(hx, hy), Scaled(6.0f));
        }
    }

    void UltraCanvasColorPicker::RenderValueBox(IRenderContext* ctx, int row,
                                                bool editing, const std::string& text) {
        const Rect2Df& vb = rowValueRects[row];
        ctx->SetFillPaint(style.fieldColor);
        ctx->FillRoundedRectangle(Rect2Dd(vb.x, vb.y, vb.width, vb.height),
                                  Scaled(style.cornerRadius));
        ctx->SetStrokePaint(editing ? style.accentColor : style.fieldBorderColor);
        ctx->SetStrokeWidth(editing ? 1.5 : 1.0);
        ctx->DrawRoundedRectangle(Rect2Dd(vb.x, vb.y, vb.width, vb.height),
                                  Scaled(style.cornerRadius));

        float aw = showValueSpinners ? Scaled(14.0f) : 0.0f;
        if (showValueSpinners) {
            // < and > step arrows inside the box edges
            float ah = Scaled(4.0f);
            float cy = vb.y + vb.height * 0.5f;
            ctx->SetStrokePaint(style.mutedTextColor);
            ctx->SetStrokeWidth(1.5);
            float lx = vb.x + aw * 0.62f;
            ctx->DrawLine(Point2Dd(lx, cy - ah), Point2Dd(lx - ah, cy));
            ctx->DrawLine(Point2Dd(lx - ah, cy), Point2Dd(lx, cy + ah));
            float rx = vb.x + vb.width - aw * 0.62f;
            ctx->DrawLine(Point2Dd(rx, cy - ah), Point2Dd(rx + ah, cy));
            ctx->DrawLine(Point2Dd(rx + ah, cy), Point2Dd(rx, cy + ah));
        }

        if (editing) {
            RenderEditableText(ctx);
        } else {
            SetFont(ctx);
            ctx->SetTextAlignment(TextAlignment::Center);
            ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
            ctx->DrawTextInRect(text, Rect2Dd(vb.x + aw, vb.y, vb.width - 2.0f * aw, vb.height));
        }
    }

    void UltraCanvasColorPicker::RenderChannelRow(IRenderContext* ctx, int row) {
        std::string label; float value, mn, mx;
        GetChannelInfo(row, label, value, mn, mx);

        // Label
        SetFont(ctx, true);
        ctx->SetTextAlignment(TextAlignment::Left);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect(label, Rect2Dd(rowLabelRects[row].x, rowLabelRects[row].y,
                                           rowLabelRects[row].width, rowLabelRects[row].height));

        // Slider track with gradient + handle
        float t = (mx > mn) ? (value - mn) / (mx - mn) : 0.0f;
        RenderSliderTrackAndHandle(ctx, row, t, MakeChannelGradient(ctx, row), false);

        // Value box
        bool editing = ((row == 0 && editField == EditField::Channel0) ||
                        (row == 1 && editField == EditField::Channel1) ||
                        (row == 2 && editField == EditField::Channel2));
        RenderValueBox(ctx, row, editing, FormatChannel(row));
    }

    void UltraCanvasColorPicker::RenderAlphaRow(IRenderContext* ctx) {
        const int row = 3;
        // Label
        SetFont(ctx, true);
        ctx->SetTextAlignment(TextAlignment::Left);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect("A", Rect2Dd(rowLabelRects[row].x, rowLabelRects[row].y,
                                         rowLabelRects[row].width, rowLabelRects[row].height));

        const Rect2Df& tr = rowSliderRects[row];
        Color opaque = GetColor(); opaque.a = 255;
        Color clear = opaque; clear.a = 0;
        auto grad = ctx->CreateLinearGradientPattern(
                tr.x, tr.y, tr.x + tr.width, tr.y,
                {GradientStop(0.0, clear), GradientStop(1.0, opaque)});
        RenderSliderTrackAndHandle(ctx, row, alpha / 255.0f, grad, true);

        bool editing = (editField == EditField::Alpha);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", (int)alpha);
        RenderValueBox(ctx, row, editing, std::string(buf));
    }

    void UltraCanvasColorPicker::RenderDropdownPopup(IRenderContext* ctx) {
        const char* names[3] = {"HSV", "HSL", "RGB"};
        float itemH = Scaled(style.rowHeight);
        float w = modeButtonRect.width;
        float x = modeButtonRect.x;
        float y = modeButtonRect.y + modeButtonRect.height + 2.0f;
        Rect2Dd box(x, y, w, itemH * 3.0f);

        ctx->SetFillPaint(style.fieldColor);
        ctx->FillRoundedRectangle(box, Scaled(style.cornerRadius));
        ctx->SetStrokePaint(style.fieldBorderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRoundedRectangle(box, Scaled(style.cornerRadius));

        for (int i = 0; i < 3; ++i) {
            Rect2Dd item(x, y + i * itemH, w, itemH);
            if ((int)model == i) {
                ctx->SetFillPaint(style.accentColor.WithAlpha(120));
                ctx->FillRectangle(item);
            }
            SetFont(ctx);
            ctx->SetTextAlignment(TextAlignment::Left);
            ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
            ctx->DrawTextInRect(names[i], Rect2Dd(item.x + Scaled(8.0f), item.y,
                                                  item.width - Scaled(12.0f), item.height));
        }
    }

    void UltraCanvasColorPicker::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        RecalculateLayout();

        // Background panel. When a swatch is hovered the whole surface is flooded
        // with that colour (forced opaque) so the picked colour can be judged on a
        // large area; otherwise the normal UI background is used.
        Color surface = style.backgroundColor;
        if (hoverSwatch == HoverSwatch::Foreground)      { surface = GetColor();     surface.a = 255; }
        else if (hoverSwatch == HoverSwatch::Background) { surface = previousColor;  surface.a = 255; }
        ctx->SetFillPaint(surface);
        ctx->FillRectangle(Rect2Dd(0, 0, GetWidth(), GetHeight()));

        if (showColorWheel) {
            if (wheelStyle == ColorPickerWheelStyle::Ring) {
                RenderHueRing(ctx);
            } else {
                RenderHueBar(ctx);
            }
            RenderSVSquare(ctx);
            RenderSwatches(ctx);
            RenderScreenPickButton(ctx);
        }
        if (modeSelector == ColorPickerModeSelector::Dropdown) {
            RenderModeButton(ctx);
        } else {
            RenderTabs(ctx);
        }
        RenderHexField(ctx);
        if (slidersCollapsible) RenderSlidersHeader(ctx);

        if (SlidersVisible()) {
            for (int i = 0; i < 3; ++i) RenderChannelRow(ctx, i);
            if (showAlpha) RenderAlphaRow(ctx);
        }

        if (dropdownOpen) RenderDropdownPopup(ctx);
    }

    // ===================================================================
    // Event handling
    // ===================================================================
    bool UltraCanvasColorPicker::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:  return HandleMouseDown(event);
            // A rapid second click arrives as a double-click instead of a
            // MouseDown; the swap arrow and buttons must react to every click.
            case UCEventType::MouseDoubleClick: return HandleMouseDown(event);
            case UCEventType::MouseMove:  return HandleMouseMove(event);
            case UCEventType::MouseUp:    return HandleMouseUp(event);
            case UCEventType::KeyDown:    return HandleKeyDown(event);
            case UCEventType::MouseLeave:
                SetHovered(false);
                UltraCanvasTooltipManager::HideTooltip();
                if (hoverSwatch != HoverSwatch::NoneSwatch) {
                    hoverSwatch = HoverSwatch::NoneSwatch;
                    RequestRedraw();
                }
                return false;
            default: break;
        }
        return false;
    }

    bool UltraCanvasColorPicker::HandleMouseDown(const UCEvent& event) {
        Point2Df p(event.pointer.x, event.pointer.y);
        SetFocus(true);
        // Adjust (right) button on the colour controls edits the background
        // colour instead of the foreground (mirrors the eyedropper's
        // left=foreground / right=background rule).
        const bool adjust = (event.button == UCMouseButton::Right);

        // Dropdown popup takes priority while open
        if (dropdownOpen) {
            float itemH = Scaled(style.rowHeight);
            float x = modeButtonRect.x;
            float y = modeButtonRect.y + modeButtonRect.height + 2.0f;
            Rect2Df popup(x, y, modeButtonRect.width, itemH * 3.0f);
            if (popup.Contains(p)) {
                int idx = std::clamp((int)((p.y - y) / itemH), 0, 2);
                SetModel((ColorPickerModel)idx);
            }
            dropdownOpen = false;
            RequestRedraw();
            return true;
        }

        // Clicking inside the field being edited moves the caret (drag extends
        // the selection, double-click selects all); clicking anywhere else
        // commits the edit first.
        if (editField != EditField::NoEdit) {
            if (EditTextRect(editField).Contains(p)) {
                if (event.type == UCEventType::MouseDoubleClick) {
                    EditSelectAll();
                } else {
                    editCaret = CaretIndexFromPoint(p);
                    editAnchor = editCaret;
                    dragTarget = DragTarget::TextDrag;
                }
                RequestRedraw();
                return true;
            }
            CommitEdit();
        }

        if (showColorWheel) {
            // Eyedropper: with an onScreenColorPick host callback the host does
            // the sampling (left mouse = foreground, right mouse = background).
            // Without a callback the built-in mode arms: the pointer becomes an
            // eyedropper and the next click in the window is sampled. The mouse
            // button used on the icon selects the target swatch (left/Select ->
            // foreground, right/Adjust -> background) for both the live preview
            // and the committed sample.
            if (screenPickRect.Contains(p)) {
                bool foreground = (event.button != UCMouseButton::Right);
                if (onScreenColorPick) {
                    onScreenColorPick(foreground);
                } else {
                    StartScreenPick(foreground);
                }
                return true;
            }

            if (wheelStyle == ColorPickerWheelStyle::Ring) {
                float dx = p.x - wheelCenter.x;
                float dy = p.y - wheelCenter.y;
                float dist = std::sqrt(dx * dx + dy * dy);
                if (dist >= ringInner && dist <= ringOuter) {
                    if (adjust) BeginBackgroundEdit();
                    dragTarget = DragTarget::HueRing;
                    UpdateHueFromPoint(p);
                    Changed(false);
                    return true;
                }
            } else if (hueBarRect.Contains(p)) {
                if (adjust) BeginBackgroundEdit();
                dragTarget = DragTarget::HueBar;
                UpdateHueFromBar(p);
                Changed(false);
                return true;
            }
            if (svRect.Contains(p)) {
                if (adjust) BeginBackgroundEdit();
                dragTarget = DragTarget::SVSquare;
                UpdateSVFromPoint(p);
                Changed(false);
                return true;
            }
            if (currentSwatchRect.Contains(p)) {
                return true; // current swatch is informational
            }
            if (previousSwatchRect.Contains(p)) {
                SetColor(previousColor, true);
                return true;
            }
            if (swapArrowRect.Contains(p)) {
                Color cur = GetColor();
                SetColor(previousColor, false);
                previousColor = cur;
                Changed(true);
                return true;
            }
        }

        if (modeSelector == ColorPickerModeSelector::Dropdown &&
            modeButtonRect.Contains(p)) {
            dropdownOpen = true;
            RequestRedraw();
            return true;
        }
        if (hexFieldRect.Contains(p)) {
            BeginEdit(EditField::Hex, &p);
            dragTarget = DragTarget::TextDrag;
            return true;
        }
        if (modeSelector == ColorPickerModeSelector::TabBar) {
            for (int i = 0; i < 3; ++i) {
                if (tabRects[i].Contains(p)) {
                    SetModel((ColorPickerModel)i);
                    return true;
                }
            }
        }
        if (slidersCollapsible && slidersHeaderRect.Contains(p)) {
            SetSlidersExpanded(!slidersExpanded);
            return true;
        }

        if (SlidersVisible()) {
            int nRows = 3 + (showAlpha ? 1 : 0);
            for (int i = 0; i < nRows; ++i) {
                if (rowValueRects[i].Contains(p)) {
                    if (showValueSpinners) {
                        if (SpinnerDownRect(i).Contains(p)) { StepChannel(i, -1.0f); return true; }
                        if (SpinnerUpRect(i).Contains(p))   { StepChannel(i, +1.0f); return true; }
                    }
                    EditField fields[4] = {EditField::Channel0, EditField::Channel1,
                                           EditField::Channel2, EditField::Alpha};
                    BeginEdit(fields[i], &p);
                    dragTarget = DragTarget::TextDrag;
                    return true;
                }
                if (rowSliderRects[i].Contains(p)) {
                    if (adjust) BeginBackgroundEdit();
                    DragTarget targets[4] = {DragTarget::Channel0, DragTarget::Channel1,
                                             DragTarget::Channel2, DragTarget::Alpha};
                    dragTarget = targets[i];
                    ApplyDrag(p, false);
                    return true;
                }
            }
        }
        return false;
    }

    bool UltraCanvasColorPicker::HandleMouseMove(const UCEvent& event) {
        Point2Df p(event.pointer.x, event.pointer.y);
        if (dragTarget != DragTarget::NoneTarget) {
            ApplyDrag(p, false);
            return true;
        }
        // Not dragging: track which swatch (if any) the pointer hovers so the
        // whole surface can preview that colour.
        UpdateSwatchHover(p);

        // Explain the model choices and channel / hex labels on hover.
        UpdateHoverTooltip(event);

        // I-beam over the editable text fields (hex + value boxes, minus the
        // spinner arrow zones), default arrow elsewhere.
        bool overText = hexFieldRect.Contains(p);
        if (!overText && SlidersVisible()) {
            int nRows = 3 + (showAlpha ? 1 : 0);
            for (int i = 0; i < nRows && !overText; ++i) {
                if (rowValueRects[i].Contains(p)) {
                    overText = !showValueSpinners ||
                               (!SpinnerDownRect(i).Contains(p) &&
                                !SpinnerUpRect(i).Contains(p));
                }
            }
        }
        mouseCursor = overText ? UCMouseCursor::Text : UCMouseCursor::Default;
        return false;
    }

    void UltraCanvasColorPicker::UpdateSwatchHover(const Point2Df& p) {
        HoverSwatch now = HoverSwatch::NoneSwatch;
        if (showColorWheel) {
            // The foreground (current) swatch sits on top of the background
            // (previous) swatch, so test it first.
            if (currentSwatchRect.Contains(p))       now = HoverSwatch::Foreground;
            else if (previousSwatchRect.Contains(p)) now = HoverSwatch::Background;
        }
        if (now != hoverSwatch) {
            hoverSwatch = now;
            RequestRedraw();
        }
    }

    std::string UltraCanvasColorPicker::ModelTooltip(ColorPickerModel m) const {
        switch (m) {
            case ColorPickerModel::HSV: return "HSV — Hue, Saturation, Value";
            case ColorPickerModel::HSL: return "HSL — Hue, Saturation, Lightness";
            case ColorPickerModel::RGB: return "RGB — Red, Green, Blue";
        }
        return "";
    }

    std::string UltraCanvasColorPicker::ChannelTooltip(const std::string& label) const {
        if (label == "H")   return "Hue (0–360°)";
        if (label == "S")   return "Saturation (0–100%)";
        if (label == "V")   return "Value / Brightness (0–100%)";
        if (label == "L")   return "Lightness (0–100%)";
        if (label == "R")   return "Red (0–255)";
        if (label == "G")   return "Green (0–255)";
        if (label == "B")   return "Blue (0–255)";
        if (label == "A")   return "Alpha / Opacity (0–255)";
        if (label == "Hex") return "Hex colour code (#RRGGBB or #RRGGBBAA)";
        return "";
    }

    void UltraCanvasColorPicker::UpdateHoverTooltip(const UCEvent& event) {
        auto* win = GetWindow();
        if (!win) return;
        Point2Df p(event.pointer.x, event.pointer.y);
        auto over = [&](const Rect2Df& r) {
            return r.width > 0.0f && r.height > 0.0f && r.Contains(p);
        };
        std::string text;

        // Model choices: HSV / HSL / RGB (tab bar, dropdown button, or the open
        // dropdown list).
        if (modeSelector == ColorPickerModeSelector::TabBar) {
            for (int i = 0; i < 3; ++i) {
                if (over(tabRects[i])) { text = ModelTooltip((ColorPickerModel)i); break; }
            }
        } else if (over(modeButtonRect)) {
            text = ModelTooltip(model);
        }
        if (text.empty() && dropdownOpen) {
            float itemH = Scaled(style.rowHeight);
            float x = modeButtonRect.x;
            float y = modeButtonRect.y + modeButtonRect.height + 2.0f;
            Rect2Df popup(x, y, modeButtonRect.width, itemH * 3.0f);
            if (over(popup)) {
                int idx = std::clamp((int)((p.y - y) / itemH), 0, 2);
                text = ModelTooltip((ColorPickerModel)idx);
            }
        }

        // Channel labels: H/S/V, H/S/L or R/G/B (per model) plus A for alpha.
        if (text.empty() && SlidersVisible()) {
            int nRows = 3 + (showAlpha ? 1 : 0);
            for (int i = 0; i < nRows; ++i) {
                if (over(rowLabelRects[i])) {
                    std::string label;
                    if (i < 3) {
                        float v, mn, mx;
                        GetChannelInfo(i, label, v, mn, mx);
                    } else {
                        label = "A";
                    }
                    text = ChannelTooltip(label);
                    break;
                }
            }
        }

        // Hex label.
        if (text.empty() && over(hexLabelRect)) {
            text = ChannelTooltip("Hex");
        }

        if (text.empty()) {
            UltraCanvasTooltipManager::HideTooltip();
        } else {
            UltraCanvasTooltipManager::UpdateAndShowTooltip(
                    win, text, Point2Di(event.pointerWindow.x, event.pointerWindow.y));
        }
    }

    bool UltraCanvasColorPicker::HandleMouseUp(const UCEvent& event) {
        if (dragTarget == DragTarget::NoneTarget) return false;
        Point2Df p(event.pointer.x, event.pointer.y);
        ApplyDrag(p, true);
        dragTarget = DragTarget::NoneTarget;
        // Commit the background colour and restore the foreground working state.
        if (editingBackground) EndBackgroundEdit();
        return true;
    }

    void UltraCanvasColorPicker::ApplyDrag(const Point2Df& p, bool finished) {
        switch (dragTarget) {
            case DragTarget::TextDrag:
                // Extend the text selection towards the pointer; no colour change.
                editCaret = CaretIndexFromPoint(p);
                RequestRedraw();
                return;
            case DragTarget::HueRing:  UpdateHueFromPoint(p); break;
            case DragTarget::HueBar:   UpdateHueFromBar(p); break;
            case DragTarget::SVSquare: UpdateSVFromPoint(p); break;
            case DragTarget::Channel0:
            case DragTarget::Channel1:
            case DragTarget::Channel2: {
                int row = (int)dragTarget - (int)DragTarget::Channel0;
                const Rect2Df& tr = rowSliderRects[row];
                float t = std::clamp((p.x - tr.x) / std::max(1.0f, tr.width), 0.0f, 1.0f);
                std::string label; float value, mn, mx;
                GetChannelInfo(row, label, value, mn, mx);
                SetChannelValue(row, mn + t * (mx - mn));
                break;
            }
            case DragTarget::Alpha: {
                const Rect2Df& tr = rowSliderRects[3];
                float t = std::clamp((p.x - tr.x) / std::max(1.0f, tr.width), 0.0f, 1.0f);
                alpha = (uint8_t)std::lround(t * 255.0f);
                break;
            }
            default: return;
        }
        Changed(finished);
    }

    void UltraCanvasColorPicker::UpdateHueFromPoint(const Point2Df& p) {
        float dx = p.x - wheelCenter.x;
        float dy = p.y - wheelCenter.y;
        float ang = std::atan2(dy, dx) * 180.0f / (float)M_PI; // screen angle
        float h = ang - 180.0f;                                // invert the +180 offset
        h = std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f);
        hue = h;
    }

    void UltraCanvasColorPicker::UpdateHueFromBar(const Point2Df& p) {
        // Match the handle travel used in RenderHueBar (end caps excluded).
        float radius = hueBarRect.height * 0.5f;
        float span = std::max(1.0f, hueBarRect.width - 2.0f * radius);
        float t = std::clamp((p.x - hueBarRect.x - radius) / span, 0.0f, 1.0f);
        hue = t * 360.0f;
    }

    void UltraCanvasColorPicker::UpdateSVFromPoint(const Point2Df& p) {
        float s = (p.x - svRect.x) / std::max(1.0f, svRect.width);
        float v = 1.0f - (p.y - svRect.y) / std::max(1.0f, svRect.height);
        sat = std::clamp(s, 0.0f, 1.0f);
        val = std::clamp(v, 0.0f, 1.0f);
    }

    void UltraCanvasColorPicker::BeginBackgroundEdit() {
        if (editingBackground) return;
        // Save the foreground working state, then load the background colour
        // into it so every existing edit/render path operates on the background
        // for the duration of the Adjust drag.
        fgSaveHue = hue; fgSaveSat = sat; fgSaveVal = val; fgSaveAlpha = alpha;
        float h, s, v;
        RGBToHSV(previousColor, h, s, v);
        if (s > 1e-4f && v > 1e-4f) hue = h;   // keep current hue on a grey bg
        sat = s; val = v; alpha = previousColor.a;
        editingBackground = true;
    }

    void UltraCanvasColorPicker::EndBackgroundEdit() {
        if (!editingBackground) return;
        previousColor = GetColor();            // commit the edited background
        hue = fgSaveHue; sat = fgSaveSat; val = fgSaveVal; alpha = fgSaveAlpha;
        editingBackground = false;
        RequestRedraw();
    }

    // ===================================================================
    // Built-in screen colour picking (eyedropper)
    // ===================================================================
    std::string UltraCanvasColorPicker::ScreenPickFilterId() const {
        return "UCColorPickerScreenPick_" + GetIdentifier();
    }

    void UltraCanvasColorPicker::StartScreenPick(bool foreground) {
        if (screenPickActive) return;
        auto* win = GetWindow();
        if (!win) return;
        screenPickActive = true;
        screenPickForeground = foreground;
        screenPickPreviewValid = false;

        // Switch the pointer to an eyedropper cursor; fall back to the
        // crosshair when the cursor image cannot be loaded.
        std::string cursorFile = NormalizePath(GetResourcesDir() +
                                               "media/lib/cursor/color-picker.png");
        // Hotspot at the dropper tip (bottom-left of the 24x24 image).
        if (win->SelectMouseCursor(UCMouseCursor::Custom1, cursorFile.c_str(), 1, 22)) {
            screenPickCursor = UCMouseCursor::Custom1;
        } else {
            screenPickCursor = UCMouseCursor::Cross;
            win->SelectMouseCursor(screenPickCursor);
        }

        // Window-level filter: the sampling click can land on any widget in the
        // window, so intercept events before normal dispatch. MouseMove is
        // filtered too so the hover logic cannot swap the eyedropper cursor for
        // the hovered element's cursor. Installed once and left in place (the
        // screenPickActive guard makes it a no-op while disarmed); removing it
        // from inside its own invocation would destroy the running lambda.
        if (!screenPickFilterInstalled) {
            screenPickFilterInstalled = true;
            win->InstallEventFilter(ScreenPickFilterId(),
                [this](const UCEvent& e) -> bool {
                    if (!screenPickActive) return false;
                    switch (e.type) {
                        case UCEventType::MouseMove:
                            if (auto* w = GetWindow()) w->SelectMouseCursor(screenPickCursor);
                            // Live-preview the pixel under the pointer in the
                            // foreground swatch so the user sees the colour
                            // before clicking to commit it.
                            UpdateScreenPickPreview(e);
                            return false;               // hover handling may continue
                        case UCEventType::KeyDown:
                            if (e.virtualKey == UCKeys::Escape) {
                                EndScreenPick();
                                return true;
                            }
                            return false;
                        case UCEventType::MouseDown:
                            HandleScreenPickClick(e);
                            return true;                // consume the sampling click
                        default:
                            return false;
                    }
                },
                { UCEventType::MouseDown, UCEventType::MouseMove, UCEventType::KeyDown });
        }
        RequestRedraw();
    }

    void UltraCanvasColorPicker::UpdateScreenPickPreview(const UCEvent& event) {
        if (!screenPickActive) return;
        auto* win = GetWindow();
        Color sampled;
        if (win && win->GetPixelColor(event.pointerWindow.x, event.pointerWindow.y, sampled)) {
            sampled.a = 255;
            screenPickPreview = sampled;
            screenPickPreviewValid = true;
        } else {
            screenPickPreviewValid = false;
        }
        RequestRedraw();
    }

    void UltraCanvasColorPicker::HandleScreenPickClick(const UCEvent& event) {
        // The target swatch was chosen by the button used on the eyedropper icon
        // (see screenPickForeground); the sampling click commits into it.
        Color sampled;
        auto* win = GetWindow();
        if (win && win->GetPixelColor(event.pointerWindow.x, event.pointerWindow.y, sampled)) {
            sampled.a = 255;
            if (screenPickForeground) SetForegroundColor(sampled, true);
            else                      SetBackgroundColor(sampled);
        }
        EndScreenPick();
    }

    void UltraCanvasColorPicker::EndScreenPick(bool restoreCursor) {
        if (!screenPickActive) return;
        screenPickActive = false;
        screenPickPreviewValid = false;
        // The event filter stays installed (guarded by screenPickActive); it is
        // only removed in the destructor.
        if (restoreCursor) {
            if (auto* win = GetWindow()) win->SelectMouseCursor(UCMouseCursor::Default);
        }
        RequestRedraw();
    }

    // ===================================================================
    // Inline editing
    // ===================================================================
    std::string UltraCanvasColorPicker::CurrentFieldText(EditField field) const {
        switch (field) {
            case EditField::Hex:      return GetColor().ToHexStringWithAlpha();
            case EditField::Channel0: return FormatChannel(0);
            case EditField::Channel1: return FormatChannel(1);
            case EditField::Channel2: return FormatChannel(2);
            case EditField::Alpha: {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d", (int)alpha);
                return std::string(buf);
            }
            default: return "";
        }
    }

    void UltraCanvasColorPicker::BeginEdit(EditField field, const Point2Df* clickAt) {
        if (dragTarget != DragTarget::NoneTarget) return;
        editField = field;
        editBuffer = CurrentFieldText(field);
        if (clickAt) {
            // Mouse entry: place the caret under the click.
            editCaret = CaretIndexFromPoint(*clickAt);
            editAnchor = editCaret;
        } else {
            // Keyboard entry (Tab): select the whole text for quick replacement.
            editAnchor = 0;
            editCaret = editBuffer.size();
        }
        RequestRedraw();
    }

    void UltraCanvasColorPicker::CancelEdit() {
        editField = EditField::NoEdit;
        editBuffer.clear();
        editCaret = editAnchor = 0;
        RequestRedraw();
    }

    void UltraCanvasColorPicker::CommitEdit() {
        if (editField == EditField::NoEdit) return;
        EditField field = editField;
        std::string text = editBuffer;
        editField = EditField::NoEdit;
        editBuffer.clear();
        editCaret = editAnchor = 0;

        auto trim = [](std::string s) {
            size_t a = s.find_first_not_of(" \t");
            size_t b = s.find_last_not_of(" \t");
            return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
        };
        text = trim(text);

        try {
            if (field == EditField::Hex) {
                std::string hex = text;
                if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
                if (hex.size() == 3) { // shorthand #RGB
                    std::string e;
                    for (char c : hex) { e += c; e += c; }
                    hex = e;
                }
                if (hex.size() == 6 || hex.size() == 8) {
                    unsigned long r = std::stoul(hex.substr(0, 2), nullptr, 16);
                    unsigned long g = std::stoul(hex.substr(2, 2), nullptr, 16);
                    unsigned long b = std::stoul(hex.substr(4, 2), nullptr, 16);
                    unsigned long a = (hex.size() == 8)
                                      ? std::stoul(hex.substr(6, 2), nullptr, 16) : alpha;
                    SetColor(Color((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a), true);
                }
            } else if (field == EditField::Alpha) {
                int a = std::stoi(text);
                alpha = (uint8_t)std::clamp(a, 0, 255);
                Changed(true);
            } else {
                int row = (int)field - (int)EditField::Channel0;
                float v = std::stof(text);
                SetChannelValue(row, v);
                Changed(true);
            }
        } catch (...) {
            // Invalid input: discard, keep previous colour
        }
        RequestRedraw();
    }

    // ===================================================================
    // Inline text editor: selection / clipboard / navigation helpers
    // ===================================================================
    void UltraCanvasColorPicker::EditSelectAll() {
        editAnchor = 0;
        editCaret = editBuffer.size();
        RequestRedraw();
    }

    void UltraCanvasColorPicker::EditDeleteSelection() {
        if (!HasEditSelection()) return;
        size_t a = EditSelMin(), b = EditSelMax();
        editBuffer.erase(a, b - a);
        editCaret = editAnchor = a;
    }

    void UltraCanvasColorPicker::EditInsert(const std::string& s) {
        EditDeleteSelection();
        std::string filtered;
        for (char c : s) {
            if (EditAcceptsChar(c)) filtered += c;
        }
        // Keep the buffer bounded; the longest valid content is #RRGGBBAA.
        size_t room = (editBuffer.size() < 16) ? (16 - editBuffer.size()) : 0;
        if (filtered.size() > room) filtered.resize(room);
        if (!filtered.empty()) {
            editBuffer.insert(editCaret, filtered);
            editCaret += filtered.size();
            editAnchor = editCaret;
        }
        RequestRedraw();
    }

    void UltraCanvasColorPicker::EditCopy() {
        if (!HasEditSelection()) return;
        SetClipboardText(editBuffer.substr(EditSelMin(), EditSelMax() - EditSelMin()));
    }

    void UltraCanvasColorPicker::EditCut() {
        if (!HasEditSelection()) return;
        EditCopy();
        EditDeleteSelection();
        RequestRedraw();
    }

    void UltraCanvasColorPicker::EditPaste() {
        std::string clip;
        GetClipboardText(clip);   // leaves the string empty on failure
        // Single-line fields: strip line breaks entirely.
        std::string flat;
        for (char c : clip) {
            if (c == '\r' || c == '\n') continue;
            flat += c;
        }
        if (!flat.empty()) EditInsert(flat);
    }

    bool UltraCanvasColorPicker::EditAcceptsChar(char c) const {
        switch (editField) {
            case EditField::Hex:
                return c == '#' || (c >= '0' && c <= '9') ||
                       (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            case EditField::Alpha:
                return (c >= '0' && c <= '9');
            case EditField::Channel0:
            case EditField::Channel1:
            case EditField::Channel2:
                return (c >= '0' && c <= '9') || c == '.' || c == '-';
            default:
                return false;
        }
    }

    void UltraCanvasColorPicker::EditMoveToNextField(bool backwards) {
        // Editable field cycle: Hex -> Ch0 -> Ch1 -> Ch2 [-> Alpha] -> Hex.
        std::vector<EditField> order = {EditField::Hex};
        if (SlidersVisible()) {
            order.push_back(EditField::Channel0);
            order.push_back(EditField::Channel1);
            order.push_back(EditField::Channel2);
            if (showAlpha) order.push_back(EditField::Alpha);
        }
        size_t idx = 0;
        for (size_t i = 0; i < order.size(); ++i) {
            if (order[i] == editField) { idx = i; break; }
        }
        size_t n = order.size();
        size_t next = backwards ? (idx + n - 1) % n : (idx + 1) % n;
        CommitEdit();
        BeginEdit(order[next]);   // keyboard entry: selects all
    }

    Rect2Df UltraCanvasColorPicker::EditFieldRect(EditField field) const {
        switch (field) {
            case EditField::Hex:      return hexFieldRect;
            case EditField::Channel0: return rowValueRects[0];
            case EditField::Channel1: return rowValueRects[1];
            case EditField::Channel2: return rowValueRects[2];
            case EditField::Alpha:    return rowValueRects[3];
            default:                  return Rect2Df();
        }
    }

    Rect2Df UltraCanvasColorPicker::EditTextRect(EditField field) const {
        Rect2Df fr = EditFieldRect(field);
        if (field == EditField::Hex) {
            float padL = Scaled(8.0f);
            return Rect2Df(fr.x + padL, fr.y, std::max(4.0f, fr.width - padL - Scaled(4.0f)),
                           fr.height);
        }
        float aw = showValueSpinners ? Scaled(14.0f) : Scaled(4.0f);
        return Rect2Df(fr.x + aw, fr.y, std::max(4.0f, fr.width - 2.0f * aw), fr.height);
    }

    bool UltraCanvasColorPicker::EditTextCentered(EditField field) const {
        return field != EditField::Hex;   // value boxes draw centered text
    }

    float UltraCanvasColorPicker::EditTextWidth(IRenderContext* ctx,
                                                const std::string& s) const {
        if (s.empty()) return 0.0f;
        return (float)ctx->GetTextLineDimensions(s).width;
    }

    float UltraCanvasColorPicker::EditTextStartX(IRenderContext* ctx) const {
        Rect2Df tr = EditTextRect(editField);
        if (!EditTextCentered(editField)) return tr.x;
        float w = EditTextWidth(ctx, editBuffer);
        return std::max(tr.x, tr.x + (tr.width - w) * 0.5f);
    }

    size_t UltraCanvasColorPicker::CaretIndexFromPoint(const Point2Df& p) {
        IRenderContext* ctx = GetRenderContext();
        if (!ctx) return editBuffer.size();
        SetFont(ctx);
        float startX = EditTextStartX(ctx);
        size_t best = 0;
        float bestDist = std::fabs(p.x - startX);
        for (size_t i = 1; i <= editBuffer.size(); ++i) {
            float cx = startX + EditTextWidth(ctx, editBuffer.substr(0, i));
            float d = std::fabs(p.x - cx);
            if (d < bestDist) { bestDist = d; best = i; }
        }
        return best;
    }

    void UltraCanvasColorPicker::RenderEditableText(IRenderContext* ctx) {
        Rect2Df tr = EditTextRect(editField);
        SetFont(ctx);
        float startX = EditTextStartX(ctx);

        // Selection highlight behind the text
        if (HasEditSelection()) {
            float x1 = startX + EditTextWidth(ctx, editBuffer.substr(0, EditSelMin()));
            float x2 = startX + EditTextWidth(ctx, editBuffer.substr(0, EditSelMax()));
            ctx->SetFillPaint(style.accentColor.WithAlpha(110));
            ctx->FillRectangle(Rect2Dd(x1, tr.y + 2.0f, x2 - x1, tr.height - 4.0f));
        }

        SetFont(ctx);
        ctx->SetTextAlignment(TextAlignment::Left);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect(editBuffer,
                            Rect2Dd(startX, tr.y, std::max(4.0f, tr.x + tr.width - startX),
                                    tr.height));

        // Caret
        float cx = startX + EditTextWidth(ctx, editBuffer.substr(0, editCaret));
        ctx->SetStrokePaint(style.textColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawLine(Point2Dd(cx, tr.y + 3.0f), Point2Dd(cx, tr.y + tr.height - 3.0f));
    }

    bool UltraCanvasColorPicker::HandleKeyDown(const UCEvent& event) {
        if (editField == EditField::NoEdit) return false;

        const size_t len = editBuffer.size();

        // ----- Navigation, selection and clipboard shortcuts -----
        switch (event.virtualKey) {
            case UCKeys::Left:
                if (event.shift) {
                    if (editCaret > 0) editCaret--;
                } else if (HasEditSelection()) {
                    editCaret = editAnchor = EditSelMin();
                } else if (editCaret > 0) {
                    editCaret = editAnchor = editCaret - 1;
                }
                if (!event.shift) editAnchor = editCaret;
                RequestRedraw();
                return true;

            case UCKeys::Right:
                if (event.shift) {
                    if (editCaret < len) editCaret++;
                } else if (HasEditSelection()) {
                    editCaret = editAnchor = EditSelMax();
                } else if (editCaret < len) {
                    editCaret = editAnchor = editCaret + 1;
                }
                if (!event.shift) editAnchor = editCaret;
                RequestRedraw();
                return true;

            case UCKeys::Home:
                editCaret = 0;
                if (!event.shift) editAnchor = editCaret;
                RequestRedraw();
                return true;

            case UCKeys::End:
                editCaret = len;
                if (!event.shift) editAnchor = editCaret;
                RequestRedraw();
                return true;

            case UCKeys::A:
                if (event.ctrl) { EditSelectAll(); return true; }
                break;

            case UCKeys::C:
                if (event.ctrl) { EditCopy(); return true; }
                break;

            case UCKeys::X:
                if (event.ctrl) { EditCut(); return true; }
                break;

            case UCKeys::V:
                if (event.ctrl) { EditPaste(); return true; }
                break;

            case UCKeys::Insert:
                // Ctrl+Insert = copy, Shift+Insert = paste (classic bindings).
                if (event.ctrl)  { EditCopy(); return true; }
                if (event.shift) { EditPaste(); return true; }
                break;

            case UCKeys::Return:
                CommitEdit();
                return true;

            case UCKeys::Escape:
                CancelEdit();
                return true;

            case UCKeys::Tab:
                EditMoveToNextField(event.shift);
                return true;

            case UCKeys::Backspace:
                if (HasEditSelection()) {
                    EditDeleteSelection();
                } else if (editCaret > 0) {
                    editBuffer.erase(editCaret - 1, 1);
                    editCaret = editAnchor = editCaret - 1;
                }
                RequestRedraw();
                return true;

            case UCKeys::Delete:
                if (event.ctrl) { EditCut(); return true; }
                if (HasEditSelection()) {
                    EditDeleteSelection();
                } else if (editCaret < len) {
                    editBuffer.erase(editCaret, 1);
                }
                RequestRedraw();
                return true;

            default:
                break;
        }

        // ----- Printable characters (never with Ctrl held: those are shortcuts) -----
        if (!event.ctrl && event.character != 0 &&
            event.character >= 32 && event.character < 127) {
            EditInsert(std::string(1, event.character));
            return true;
        }
        return true; // swallow remaining keys while editing
    }

} // namespace UltraCanvas
