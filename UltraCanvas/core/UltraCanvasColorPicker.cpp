// core/UltraCanvasColorPicker.cpp
// Implementation of the comprehensive colour picker widget.
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework

#include "UltraCanvasColorPicker.h"
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

    void UltraCanvasColorPicker::Changed(bool finished) {
        RequestRedraw();
        Color c = GetColor();
        if (onColorChanging) onColorChanging(c);
        if (finished && onColorChanged) onColorChanged(c);
    }

    void UltraCanvasColorPicker::SetFont(IRenderContext* ctx, bool muted) const {
        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style.fontSize);
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

        const float pad = style.padding;
        const float gap = style.rowGap;
        const float x0 = pad;
        const float innerW = std::max(40.0f, W - 2.0f * pad);
        const float rightEdge = x0 + innerW;
        const float rh = style.rowHeight;

        const int nRows = 3 + (showAlpha ? 1 : 0);
        const float rowsH = nRows * (rh + gap);
        const float previewRowH = 2.0f * rh + gap;       // mode button + hex field stacked
        const float bottomH = previewRowH + gap + style.tabHeight + gap + rowsH;

        // --- Optional colour wheel occupies the space above the controls ---
        float cursorY = pad;
        if (showColorWheel) {
            float wheelAreaH = H - pad - bottomH - gap - pad;
            float wheelSize = std::min(innerW, std::max(60.0f, wheelAreaH));
            wheelRect = Rect2Df(x0 + (innerW - wheelSize) * 0.5f, pad, wheelSize, wheelSize);
            wheelCenter = Point2Df(wheelRect.x + wheelSize * 0.5f, wheelRect.y + wheelSize * 0.5f);
            ringOuter = wheelSize * 0.5f;
            ringInner = std::max(10.0f, ringOuter - style.ringThickness);
            float sq = ringInner * 1.41421356f * 0.86f;   // inscribed square (with margin)
            svRect = Rect2Df(wheelCenter.x - sq * 0.5f, wheelCenter.y - sq * 0.5f, sq, sq);
            cursorY = wheelRect.y + wheelSize + pad;
        } else {
            wheelRect = Rect2Df();
            svRect = Rect2Df();
            cursorY = pad;
        }

        // --- Preview row: swatches on the left, mode + hex on the right ---
        const float swatch = std::min(style.swatchSize, previewRowH);
        currentSwatchRect  = Rect2Df(x0, cursorY, swatch, swatch);
        previousSwatchRect = Rect2Df(x0 + swatch * 0.55f, cursorY + swatch * 0.45f, swatch, swatch);
        swapArrowRect      = Rect2Df(x0, cursorY + swatch + 2.0f, 22.0f, 20.0f);

        const float modeW = 64.0f;
        modeButtonRect = Rect2Df(rightEdge - modeW, cursorY, modeW, rh);

        const float hexFieldW = std::min(120.0f, innerW * 0.45f);
        hexFieldRect = Rect2Df(rightEdge - hexFieldW, cursorY + rh + gap, hexFieldW, rh);
        hexLabelRect = Rect2Df(hexFieldRect.x - 40.0f, hexFieldRect.y, 34.0f, rh);

        cursorY += previewRowH + gap;

        // --- Mode tabs (HSV / HSL / RGB) ---
        tabRect = Rect2Df(x0, cursorY, innerW, style.tabHeight);
        const float tabW = innerW / 3.0f;
        for (int i = 0; i < 3; ++i) {
            tabRects[i] = Rect2Df(x0 + tabW * i, cursorY, tabW, style.tabHeight);
        }
        cursorY += style.tabHeight + gap;

        // --- Channel rows ---
        const float labelW = 18.0f;
        const float valueW = 56.0f;
        for (int i = 0; i < nRows; ++i) {
            float y = cursorY + i * (rh + gap);
            rowLabelRects[i] = Rect2Df(x0, y, labelW, rh);
            rowValueRects[i] = Rect2Df(rightEdge - valueW, y, valueW, rh);
            float sliderX = x0 + labelW + 6.0f;
            float sliderW = std::max(20.0f, (rowValueRects[i].x - 8.0f) - sliderX);
            rowSliderRects[i] = Rect2Df(sliderX, y, sliderW, rh);
        }
        for (int i = nRows; i < 4; ++i) {
            rowLabelRects[i] = Rect2Df();
            rowValueRects[i] = Rect2Df();
            rowSliderRects[i] = Rect2Df();
        }

        layoutValid = true;
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
        for (int i = 0; i < 360; i += step) {
            float a0 = (i + 180.0f) * (float)M_PI / 180.0f;
            float a1 = (i + step + 180.0f) * (float)M_PI / 180.0f;
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
        float mr = style.ringThickness * 0.42f;
        ctx->SetFillPaint(HSV(hue, 1.0f, 1.0f));
        ctx->FillCircle(mp, mr);
        ctx->SetStrokePaint(style.markerColor);
        ctx->SetStrokeWidth(2.0);
        ctx->DrawCircle(mp, mr);
        ctx->SetStrokePaint(style.markerOutline);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawCircle(mp, mr + 1.5f);
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
        ctx->DrawCircle(Point2Dd(mx, my), 6.0);
        ctx->SetStrokePaint(style.markerOutline);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawCircle(Point2Dd(mx, my), 7.5);
    }

    void UltraCanvasColorPicker::RenderSwatches(IRenderContext* ctx) {
        // Previous (behind), then current (in front)
        DrawCheckerboard(ctx, previousSwatchRect, 8.0f);
        ctx->SetFillPaint(previousColor);
        ctx->FillRectangle(Rect2Dd(previousSwatchRect.x, previousSwatchRect.y,
                                   previousSwatchRect.width, previousSwatchRect.height));
        ctx->SetStrokePaint(style.borderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRectangle(Rect2Dd(previousSwatchRect.x, previousSwatchRect.y,
                                   previousSwatchRect.width, previousSwatchRect.height));

        DrawCheckerboard(ctx, currentSwatchRect, 8.0f);
        ctx->SetFillPaint(GetColor());
        ctx->FillRectangle(Rect2Dd(currentSwatchRect.x, currentSwatchRect.y,
                                   currentSwatchRect.width, currentSwatchRect.height));
        ctx->SetStrokePaint(style.borderColor);
        ctx->DrawRectangle(Rect2Dd(currentSwatchRect.x, currentSwatchRect.y,
                                   currentSwatchRect.width, currentSwatchRect.height));

        // Swap arrow (two-way)
        ctx->SetStrokePaint(style.mutedTextColor);
        ctx->SetStrokeWidth(1.5);
        float ax = swapArrowRect.x, ay = swapArrowRect.y;
        ctx->DrawLine(Point2Dd(ax + 2, ay + 6), Point2Dd(ax + 14, ay + 6));
        ctx->DrawLine(Point2Dd(ax + 14, ay + 6), Point2Dd(ax + 10, ay + 2));
        ctx->DrawLine(Point2Dd(ax + 14, ay + 14), Point2Dd(ax + 2, ay + 14));
        ctx->DrawLine(Point2Dd(ax + 2, ay + 14), Point2Dd(ax + 6, ay + 18));
    }

    void UltraCanvasColorPicker::RenderModeButton(IRenderContext* ctx) {
        ctx->SetFillPaint(style.fieldColor);
        ctx->FillRoundedRectangle(Rect2Dd(modeButtonRect.x, modeButtonRect.y,
                                          modeButtonRect.width, modeButtonRect.height),
                                  style.cornerRadius);
        ctx->SetStrokePaint(style.fieldBorderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRoundedRectangle(Rect2Dd(modeButtonRect.x, modeButtonRect.y,
                                          modeButtonRect.width, modeButtonRect.height),
                                  style.cornerRadius);

        const char* names[3] = {"HSV", "HSL", "RGB"};
        SetFont(ctx);
        ctx->SetTextAlignment(TextAlignment::Left);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect(names[(int)model],
                            Rect2Dd(modeButtonRect.x + 8, modeButtonRect.y,
                                    modeButtonRect.width - 20, modeButtonRect.height));

        // Down triangle
        float tx = modeButtonRect.x + modeButtonRect.width - 14;
        float ty = modeButtonRect.y + modeButtonRect.height * 0.5f - 2;
        ctx->SetFillPaint(style.mutedTextColor);
        ctx->FillLinePath({Point2Dd(tx, ty), Point2Dd(tx + 8, ty), Point2Dd(tx + 4, ty + 5)});
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
                                  style.cornerRadius);
        ctx->SetStrokePaint(editing ? style.accentColor : style.fieldBorderColor);
        ctx->SetStrokeWidth(editing ? 1.5 : 1.0);
        ctx->DrawRoundedRectangle(Rect2Dd(hexFieldRect.x, hexFieldRect.y,
                                          hexFieldRect.width, hexFieldRect.height),
                                  style.cornerRadius);

        std::string text = editing ? (editBuffer + "|") : GetColor().ToHexStringWithAlpha();
        SetFont(ctx);
        ctx->SetTextAlignment(TextAlignment::Left);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect(text, Rect2Dd(hexFieldRect.x + 8, hexFieldRect.y,
                                          hexFieldRect.width - 12, hexFieldRect.height));
    }

    void UltraCanvasColorPicker::RenderTabs(IRenderContext* ctx) {
        const char* names[3] = {"HSV", "HSL", "RGB"};
        ctx->SetFillPaint(style.fieldColor);
        ctx->FillRoundedRectangle(Rect2Dd(tabRect.x, tabRect.y, tabRect.width, tabRect.height),
                                  style.cornerRadius);
        for (int i = 0; i < 3; ++i) {
            bool active = ((int)model == i);
            const Rect2Df& tb = tabRects[i];
            if (active) {
                ctx->SetFillPaint(style.panelColor);
                ctx->FillRoundedRectangle(Rect2Dd(tb.x + 1, tb.y + 1, tb.width - 2, tb.height - 2),
                                          style.cornerRadius);
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
                                  style.cornerRadius);
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

        // Slider track with gradient
        const Rect2Df& tr = rowSliderRects[row];
        Rect2Dd track(tr.x, tr.y + tr.height * 0.5f - 4.0f, tr.width, 8.0);
        auto grad = MakeChannelGradient(ctx, row);
        ctx->SetFillPaint(grad);
        ctx->FillRoundedRectangle(track, 4.0);
        ctx->SetStrokePaint(style.fieldBorderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRoundedRectangle(track, 4.0);

        // Handle
        float t = (mx > mn) ? (value - mn) / (mx - mn) : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        float hx = tr.x + t * tr.width;
        float hy = tr.y + tr.height * 0.5f;
        ctx->SetFillPaint(Colors::White);
        ctx->FillCircle(Point2Dd(hx, hy), 6.0);
        ctx->SetStrokePaint(style.accentColor);
        ctx->SetStrokeWidth(1.5);
        ctx->DrawCircle(Point2Dd(hx, hy), 6.0);

        // Value box
        bool editing = ((row == 0 && editField == EditField::Channel0) ||
                        (row == 1 && editField == EditField::Channel1) ||
                        (row == 2 && editField == EditField::Channel2));
        const Rect2Df& vb = rowValueRects[row];
        ctx->SetFillPaint(style.fieldColor);
        ctx->FillRoundedRectangle(Rect2Dd(vb.x, vb.y, vb.width, vb.height), style.cornerRadius);
        ctx->SetStrokePaint(editing ? style.accentColor : style.fieldBorderColor);
        ctx->SetStrokeWidth(editing ? 1.5 : 1.0);
        ctx->DrawRoundedRectangle(Rect2Dd(vb.x, vb.y, vb.width, vb.height), style.cornerRadius);

        std::string vtext = editing ? (editBuffer + "|") : FormatChannel(row);
        SetFont(ctx);
        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect(vtext, Rect2Dd(vb.x, vb.y, vb.width, vb.height));
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
        Rect2Df trackF(tr.x, tr.y + tr.height * 0.5f - 4.0f, tr.width, 8.0f);
        Rect2Dd track(trackF.x, trackF.y, trackF.width, trackF.height);
        DrawCheckerboard(ctx, trackF, 6.0f);
        Color opaque = GetColor(); opaque.a = 255;
        Color clear = opaque; clear.a = 0;
        auto grad = ctx->CreateLinearGradientPattern(
                track.x, track.y, track.x + track.width, track.y,
                {GradientStop(0.0, clear), GradientStop(1.0, opaque)});
        ctx->SetFillPaint(grad);
        ctx->FillRoundedRectangle(track, 4.0);
        ctx->SetStrokePaint(style.fieldBorderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRoundedRectangle(track, 4.0);

        float t = alpha / 255.0f;
        float hx = tr.x + t * tr.width;
        float hy = tr.y + tr.height * 0.5f;
        ctx->SetFillPaint(Colors::White);
        ctx->FillCircle(Point2Dd(hx, hy), 6.0);
        ctx->SetStrokePaint(style.accentColor);
        ctx->SetStrokeWidth(1.5);
        ctx->DrawCircle(Point2Dd(hx, hy), 6.0);

        bool editing = (editField == EditField::Alpha);
        const Rect2Df& vb = rowValueRects[row];
        ctx->SetFillPaint(style.fieldColor);
        ctx->FillRoundedRectangle(Rect2Dd(vb.x, vb.y, vb.width, vb.height), style.cornerRadius);
        ctx->SetStrokePaint(editing ? style.accentColor : style.fieldBorderColor);
        ctx->SetStrokeWidth(editing ? 1.5 : 1.0);
        ctx->DrawRoundedRectangle(Rect2Dd(vb.x, vb.y, vb.width, vb.height), style.cornerRadius);

        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", (int)alpha);
        std::string vtext = editing ? (editBuffer + "|") : std::string(buf);
        SetFont(ctx);
        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect(vtext, Rect2Dd(vb.x, vb.y, vb.width, vb.height));
    }

    void UltraCanvasColorPicker::RenderDropdownPopup(IRenderContext* ctx) {
        const char* names[3] = {"HSV", "HSL", "RGB"};
        float itemH = style.rowHeight;
        float w = modeButtonRect.width;
        float x = modeButtonRect.x;
        float y = modeButtonRect.y + modeButtonRect.height + 2.0f;
        Rect2Dd box(x, y, w, itemH * 3.0f);

        ctx->SetFillPaint(style.fieldColor);
        ctx->FillRoundedRectangle(box, style.cornerRadius);
        ctx->SetStrokePaint(style.fieldBorderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRoundedRectangle(box, style.cornerRadius);

        for (int i = 0; i < 3; ++i) {
            Rect2Dd item(x, y + i * itemH, w, itemH);
            if ((int)model == i) {
                ctx->SetFillPaint(style.accentColor.WithAlpha(120));
                ctx->FillRectangle(item);
            }
            SetFont(ctx);
            ctx->SetTextAlignment(TextAlignment::Left);
            ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
            ctx->DrawTextInRect(names[i], Rect2Dd(item.x + 8, item.y, item.width - 12, item.height));
        }
    }

    void UltraCanvasColorPicker::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        RecalculateLayout();

        // Background panel
        ctx->SetFillPaint(style.backgroundColor);
        ctx->FillRectangle(Rect2Dd(0, 0, GetWidth(), GetHeight()));

        if (showColorWheel) {
            RenderHueRing(ctx);
            RenderSVSquare(ctx);
            RenderSwatches(ctx);
        }
        RenderModeButton(ctx);
        RenderHexField(ctx);
        RenderTabs(ctx);

        int nRows = 3 + (showAlpha ? 1 : 0);
        for (int i = 0; i < 3; ++i) RenderChannelRow(ctx, i);
        if (showAlpha) RenderAlphaRow(ctx);
        (void)nRows;

        if (dropdownOpen) RenderDropdownPopup(ctx);
    }

    // ===================================================================
    // Event handling
    // ===================================================================
    bool UltraCanvasColorPicker::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:  return HandleMouseDown(event);
            case UCEventType::MouseMove:  return HandleMouseMove(event);
            case UCEventType::MouseUp:    return HandleMouseUp(event);
            case UCEventType::KeyDown:    return HandleKeyDown(event);
            case UCEventType::MouseLeave:
                SetHovered(false);
                return false;
            default: break;
        }
        return false;
    }

    bool UltraCanvasColorPicker::HandleMouseDown(const UCEvent& event) {
        Point2Df p(event.pointer.x, event.pointer.y);
        SetFocus(true);

        // Dropdown popup takes priority while open
        if (dropdownOpen) {
            float itemH = style.rowHeight;
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

        // Clicking away from an active edit commits it
        if (editField != EditField::NoEdit) {
            CommitEdit();
        }

        if (showColorWheel) {
            float dx = p.x - wheelCenter.x;
            float dy = p.y - wheelCenter.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist >= ringInner && dist <= ringOuter) {
                dragTarget = DragTarget::HueRing;
                UpdateHueFromPoint(p);
                Changed(false);
                return true;
            }
            if (svRect.Contains(p)) {
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

        if (modeButtonRect.Contains(p)) {
            dropdownOpen = true;
            RequestRedraw();
            return true;
        }
        if (hexFieldRect.Contains(p)) {
            BeginEdit(EditField::Hex);
            return true;
        }
        for (int i = 0; i < 3; ++i) {
            if (tabRects[i].Contains(p)) {
                SetModel((ColorPickerModel)i);
                return true;
            }
        }

        int nRows = 3 + (showAlpha ? 1 : 0);
        for (int i = 0; i < nRows; ++i) {
            if (rowValueRects[i].Contains(p)) {
                EditField fields[4] = {EditField::Channel0, EditField::Channel1,
                                       EditField::Channel2, EditField::Alpha};
                BeginEdit(fields[i]);
                return true;
            }
            if (rowSliderRects[i].Contains(p)) {
                DragTarget targets[4] = {DragTarget::Channel0, DragTarget::Channel1,
                                         DragTarget::Channel2, DragTarget::Alpha};
                dragTarget = targets[i];
                ApplyDrag(p, false);
                return true;
            }
        }
        return false;
    }

    bool UltraCanvasColorPicker::HandleMouseMove(const UCEvent& event) {
        if (dragTarget == DragTarget::NoneTarget) return false;
        Point2Df p(event.pointer.x, event.pointer.y);
        ApplyDrag(p, false);
        return true;
    }

    bool UltraCanvasColorPicker::HandleMouseUp(const UCEvent& event) {
        if (dragTarget == DragTarget::NoneTarget) return false;
        Point2Df p(event.pointer.x, event.pointer.y);
        ApplyDrag(p, true);
        dragTarget = DragTarget::NoneTarget;
        return true;
    }

    void UltraCanvasColorPicker::ApplyDrag(const Point2Df& p, bool finished) {
        switch (dragTarget) {
            case DragTarget::HueRing:  UpdateHueFromPoint(p); break;
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

    void UltraCanvasColorPicker::UpdateSVFromPoint(const Point2Df& p) {
        float s = (p.x - svRect.x) / std::max(1.0f, svRect.width);
        float v = 1.0f - (p.y - svRect.y) / std::max(1.0f, svRect.height);
        sat = std::clamp(s, 0.0f, 1.0f);
        val = std::clamp(v, 0.0f, 1.0f);
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

    void UltraCanvasColorPicker::BeginEdit(EditField field) {
        if (dragTarget != DragTarget::NoneTarget) return;
        editField = field;
        editBuffer = CurrentFieldText(field);
        RequestRedraw();
    }

    void UltraCanvasColorPicker::CancelEdit() {
        editField = EditField::NoEdit;
        editBuffer.clear();
        RequestRedraw();
    }

    void UltraCanvasColorPicker::CommitEdit() {
        if (editField == EditField::NoEdit) return;
        EditField field = editField;
        std::string text = editBuffer;
        editField = EditField::NoEdit;
        editBuffer.clear();

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

    bool UltraCanvasColorPicker::HandleKeyDown(const UCEvent& event) {
        if (editField == EditField::NoEdit) return false;

        if (event.character != 0 && event.character >= 32 && event.character < 127) {
            editBuffer += event.character;
            RequestRedraw();
            return true;
        }
        switch (event.virtualKey) {
            case UCKeys::Return:
                CommitEdit();
                return true;
            case UCKeys::Escape:
                CancelEdit();
                return true;
            case UCKeys::Backspace:
                if (!editBuffer.empty()) editBuffer.pop_back();
                RequestRedraw();
                return true;
            default:
                break;
        }
        return true; // swallow keys while editing
    }

} // namespace UltraCanvas
