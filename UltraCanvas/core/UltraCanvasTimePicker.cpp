// core/UltraCanvasTimePicker.cpp
// Platform-independent time-of-day picker implementation.
// Version: 1.1.0
// Last Modified: 2026-07-19
// Author: UltraCanvas Framework

#include "UltraCanvasTimePicker.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include <ctime>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

namespace UltraCanvas {

// ===================================================================
// UCTime
// ===================================================================

    UCTime UCTime::Now() {
        std::time_t t = std::time(nullptr);
        std::tm tmv {};
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        return UCTime(tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    }

    std::string UCTime::Format(const std::string& fmt) const {
        auto pad2 = [](int v) {
            char b[8];
            std::snprintf(b, sizeof(b), "%02d", v);
            return std::string(b);
        };
        std::string out;
        size_t i = 0;
        auto at = [&](const char* tok) {
            size_t n = std::strlen(tok);
            return fmt.compare(i, n, tok) == 0;
        };
        while (i < fmt.size()) {
            if      (at("HH")) { out += pad2(hour);          i += 2; }
            else if (at("H"))  { out += std::to_string(hour); i += 1; }
            else if (at("hh")) { out += pad2(Hour12());       i += 2; }
            else if (at("h"))  { out += std::to_string(Hour12()); i += 1; }
            else if (at("mm")) { out += pad2(minute);         i += 2; }
            else if (at("m"))  { out += std::to_string(minute); i += 1; }
            else if (at("ss")) { out += pad2(second);         i += 2; }
            else if (at("s"))  { out += std::to_string(second); i += 1; }
            else if (at("tt")) { out += IsPM() ? "PM" : "AM"; i += 2; }
            else if (at("t"))  { out += IsPM() ? "P" : "A";   i += 1; }
            else               { out += fmt[i];               i += 1; }
        }
        return out;
    }

    bool UCTime::Parse(const std::string& text, bool /*is24h*/, bool hasSeconds, UCTime& out) {
        std::string lower;
        lower.reserve(text.size());
        for (char c : text) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        bool ampmGiven = false, isPM = false;
        if (lower.find("pm") != std::string::npos) { ampmGiven = true; isPM = true; }
        else if (lower.find("am") != std::string::npos) { ampmGiven = true; isPM = false; }

        // Pull the numeric groups out of the text.
        std::vector<int> groups;
        std::string cur;
        for (char c : text) {
            if (std::isdigit(static_cast<unsigned char>(c))) {
                cur += c;
            } else if (!cur.empty()) {
                groups.push_back(std::stoi(cur));
                cur.clear();
            }
        }
        if (!cur.empty()) groups.push_back(std::stoi(cur));
        if (groups.empty()) return false;

        int h = groups[0];
        int m = groups.size() > 1 ? groups[1] : 0;
        int s = (hasSeconds && groups.size() > 2) ? groups[2] : 0;

        if (ampmGiven) {                 // 12-hour interpretation
            if (h == 12) h = 0;
            if (isPM) h += 12;
        }

        h = std::max(0, std::min(23, h));
        m = std::max(0, std::min(59, m));
        s = std::max(0, std::min(59, s));
        out = UCTime(h, m, s);
        return true;
    }

// ===================================================================
// UltraCanvasTimeClockView
// ===================================================================

    namespace {
        constexpr float kClockPi = 3.14159265358979323846f;
        constexpr float kClockSelR = 15.0f;   // selection / hover bubble radius

        std::string TwoDigits(int v) {
            char b[8];
            std::snprintf(b, sizeof(b), "%02d", v);
            return std::string(b);
        }
    }

    UltraCanvasTimeClockView::UltraCanvasTimeClockView(const std::string& identifier,
                                                       float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        value = UCTime(0, 0, 0);
    }

    void UltraCanvasTimeClockView::SetTime(const UCTime& t) {
        if (t.present) { value = t; hasValue = true; }
        else           { value = UCTime(0, 0, 0); hasValue = false; }
        hoverValue = -1;
        RequestRedraw();
    }

    void UltraCanvasTimeClockView::SetUse24HourFormat(bool use24) {
        if (use24h == use24) return;
        use24h = use24;
        hoverValue = -1;
        RequestRedraw();
    }

    void UltraCanvasTimeClockView::SetShowSeconds(bool show) {
        if (showSeconds == show) return;
        showSeconds = show;
        if (!show && section == Section::Seconds) section = Section::Minutes;
        RequestRedraw();
    }

    void UltraCanvasTimeClockView::SetSection(Section s) {
        if (s == Section::Seconds && !showSeconds) s = Section::Minutes;
        if (section == s) return;
        section = s;
        hoverValue = -1;
        RequestRedraw();
    }

    void UltraCanvasTimeClockView::EnsureValuePresent() {
        hasValue = true;
        value.present = true;
    }

    void UltraCanvasTimeClockView::FireChanged() {
        if (onTimeChanged) onTimeChanged(GetTime());
    }

    UltraCanvasTimeClockView::Layout
    UltraCanvasTimeClockView::ComputeLayout(IRenderContext* ctx) const {
        Layout l;
        Rect2Df b = GetLocalBounds();
        const float headerH = 56.0f;
        l.header = Rect2Df(b.x, b.y, b.width, headerH);

        float faceTop = b.y + headerH;
        float faceH = b.height - headerH;
        l.center = Point2Df(b.x + b.width / 2.0f, faceTop + faceH / 2.0f);
        l.faceR  = std::min(b.width, faceH) / 2.0f - 6.0f;
        l.outerR = l.faceR - 22.0f;
        l.innerR = l.outerR - 34.0f;

        l.hourText   = hasValue ? TwoDigits(use24h ? value.hour : value.Hour12()) : "--";
        l.minuteText = hasValue ? TwoDigits(value.minute) : "--";
        l.secondText = hasValue ? TwoDigits(value.second) : "--";

        if (!ctx) return l;

        FontStyle fs;
        fs.fontFamily = style.fontFamily;
        fs.fontSize   = style.clockHeaderFontSize;
        fs.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(fs);

        Point2Di hd = ctx->GetTextDimension(l.hourText);
        Point2Di md = ctx->GetTextDimension(l.minuteText);
        Point2Di sd = ctx->GetTextDimension(l.secondText);
        Point2Di cd = ctx->GetTextDimension(":");
        l.colonW = static_cast<float>(cd.x);

        float total = hd.x + l.colonW + md.x;
        if (showSeconds) total += l.colonW + sd.x;
        const float ampmW = use24h ? 0.0f : 38.0f;   // AM/PM column incl. gap

        float x = l.header.x + (l.header.width - total - ampmW) / 2.0f;
        float y = l.header.y + (l.header.height - hd.y) / 2.0f;

        l.hourRect = Rect2Df(x, y, static_cast<float>(hd.x), static_cast<float>(hd.y));
        x += hd.x + l.colonW;
        l.minuteRect = Rect2Df(x, y, static_cast<float>(md.x), static_cast<float>(md.y));
        x += md.x;
        if (showSeconds) {
            x += l.colonW;
            l.secondRect = Rect2Df(x, y, static_cast<float>(sd.x), static_cast<float>(sd.y));
            x += sd.x;
        }
        if (!use24h) {
            float ax = x + 10.0f;
            float ah = 16.0f;
            float ay = l.header.y + (l.header.height - 2 * ah - 2.0f) / 2.0f;
            l.amRect = Rect2Df(ax, ay, 28.0f, ah);
            l.pmRect = Rect2Df(ax, ay + ah + 2.0f, 28.0f, ah);
        }
        return l;
    }

    Point2Df UltraCanvasTimeClockView::PointForValue(const Layout& l, int v, float* outRingR) const {
        float ringR = l.outerR;
        float frac;
        if (section == Section::Hours) {
            if (use24h) {
                bool outer = (v == 0 || v >= 13);
                if (!outer) ringR = l.innerR;
                int p = outer ? (v == 0 ? 0 : v - 12) : (v % 12);
                frac = p / 12.0f;
            } else {
                frac = (v % 12) / 12.0f;
            }
        } else {
            frac = v / 60.0f;
        }
        float ang = frac * 2.0f * kClockPi;
        if (outRingR) *outRingR = ringR;
        return Point2Df(l.center.x + std::sin(ang) * ringR,
                        l.center.y - std::cos(ang) * ringR);
    }

    bool UltraCanvasTimeClockView::ValueAtPoint(const Layout& l, const Point2Df& p,
                                                int& outValue) const {
        float dx = p.x - l.center.x;
        float dy = p.y - l.center.y;
        float r = std::sqrt(dx * dx + dy * dy);
        if (r > l.faceR + 4.0f || r < 10.0f) return false;
        if (p.y < l.header.y + l.header.height) return false;

        float ang = std::atan2(dx, -dy);                 // clockwise from 12
        if (ang < 0) ang += 2.0f * kClockPi;
        float frac = ang / (2.0f * kClockPi);

        if (section == Section::Hours) {
            int p12 = static_cast<int>(frac * 12.0f + 0.5f) % 12;
            if (use24h) {
                bool outer = r >= (l.outerR + l.innerR) / 2.0f;
                outValue = outer ? (p12 == 0 ? 0 : p12 + 12)
                                 : (p12 == 0 ? 12 : p12);
            } else {
                outValue = (p12 == 0 ? 12 : p12);        // 1..12
            }
        } else {
            int step = (section == Section::Minutes) ? minuteStep : secondStep;
            int v = static_cast<int>(frac * 60.0f / step + 0.5f) * step;
            outValue = v % 60;
        }
        return true;
    }

    void UltraCanvasTimeClockView::ApplyFaceValue(int v, bool finishSection) {
        EnsureValuePresent();
        switch (section) {
            case Section::Hours:
                if (use24h) {
                    value.hour = v;
                } else {
                    bool pm = value.hour >= 12;          // keep the AM/PM half
                    value.hour = (v % 12) + (pm ? 12 : 0);
                }
                break;
            case Section::Minutes: value.minute = v; break;
            case Section::Seconds: value.second = v; break;
        }
        FireChanged();
        if (finishSection) AdvanceSection();
        RequestRedraw();
    }

    void UltraCanvasTimeClockView::AdvanceSection() {
        if (section == Section::Hours) {
            SetSection(Section::Minutes);
        } else if (section == Section::Minutes && showSeconds) {
            SetSection(Section::Seconds);
        } else if (onAccepted) {
            onAccepted(GetTime());
        }
    }

    void UltraCanvasTimeClockView::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        Layout l = ComputeLayout(ctx);
        RenderHeader(ctx, l);
        RenderFace(ctx, l);
    }

    void UltraCanvasTimeClockView::RenderHeader(IRenderContext* ctx, const Layout& l) {
        ctx->DrawFilledRectangle(l.header, style.clockHeaderColor);

        FontStyle fs;
        fs.fontFamily = style.fontFamily;
        fs.fontSize   = style.clockHeaderFontSize;
        fs.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(fs);

        auto segColor = [&](Section s, int hover) {
            bool active = (section == s) || headerHover == hover;
            return active ? style.clockHeaderTextColor : style.clockHeaderDimTextColor;
        };

        ctx->SetTextPaint(segColor(Section::Hours, 1));
        ctx->DrawText(l.hourText, Point2Dd(l.hourRect.x, l.hourRect.y));

        ctx->SetTextPaint(style.clockHeaderDimTextColor);
        ctx->DrawText(":", Point2Dd(l.hourRect.x + l.hourRect.width, l.hourRect.y));

        ctx->SetTextPaint(segColor(Section::Minutes, 2));
        ctx->DrawText(l.minuteText, Point2Dd(l.minuteRect.x, l.minuteRect.y));

        if (showSeconds) {
            ctx->SetTextPaint(style.clockHeaderDimTextColor);
            ctx->DrawText(":", Point2Dd(l.minuteRect.x + l.minuteRect.width, l.minuteRect.y));
            ctx->SetTextPaint(segColor(Section::Seconds, 3));
            ctx->DrawText(l.secondText, Point2Dd(l.secondRect.x, l.secondRect.y));
        }

        if (!use24h) {
            FontStyle small;
            small.fontFamily = style.fontFamily;
            small.fontSize   = style.fontSize;
            small.fontWeight = FontWeight::Bold;
            ctx->SetFontStyle(small);
            bool pm = value.IsPM();
            auto drawAmPm = [&](const Rect2Df& r, const char* txt, bool active, int hover) {
                ctx->SetTextPaint(active || headerHover == hover
                                  ? style.clockHeaderTextColor
                                  : style.clockHeaderDimTextColor);
                Point2Di d = ctx->GetTextDimension(txt);
                ctx->DrawText(txt, Point2Dd(r.x + (r.width - d.x) / 2.0f,
                                            r.y + (r.height - d.y) / 2.0f));
            };
            drawAmPm(l.amRect, "AM", !pm, 4);
            drawAmPm(l.pmRect, "PM", pm, 5);
        }
    }

    void UltraCanvasTimeClockView::DrawFaceNumber(IRenderContext* ctx, const Point2Df& pos,
                                                  const std::string& text, const Color& color) {
        ctx->SetTextPaint(color);
        Point2Di d = ctx->GetTextDimension(text);
        ctx->DrawText(text, Point2Dd(pos.x - d.x / 2.0f, pos.y - d.y / 2.0f));
    }

    void UltraCanvasTimeClockView::RenderFace(IRenderContext* ctx, const Layout& l) {
        ctx->DrawFilledCircle(Point2Dd(l.center.x, l.center.y), l.faceR,
                              style.clockFaceColor, Colors::Transparent, 0.0f);

        int selVal;
        switch (section) {
            case Section::Hours:   selVal = use24h ? value.hour : value.Hour12(); break;
            case Section::Minutes: selVal = value.minute; break;
            default:               selVal = value.second; break;
        }

        // Hand + selection bubble (always shown; an empty view points at 00:00).
        Point2Df selPos = PointForValue(l, selVal);
        ctx->SetStrokePaint(style.clockSelectionColor);
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawLine(Point2Dd(l.center.x, l.center.y), Point2Dd(selPos.x, selPos.y));
        ctx->DrawFilledCircle(Point2Dd(l.center.x, l.center.y), 3.5f,
                              style.clockSelectionColor, Colors::Transparent, 0.0f);
        ctx->DrawFilledCircle(Point2Dd(selPos.x, selPos.y), kClockSelR,
                              style.clockSelectionColor, Colors::Transparent, 0.0f);

        if (hoverValue >= 0 && hoverValue != selVal) {
            Point2Df hp = PointForValue(l, hoverValue);
            ctx->DrawFilledCircle(Point2Dd(hp.x, hp.y), kClockSelR,
                                  style.clockHoverColor, Colors::Transparent, 0.0f);
        }

        FontStyle fs;
        fs.fontFamily = style.fontFamily;
        fs.fontSize   = style.fontSize;
        ctx->SetFontStyle(fs);

        auto numberColor = [&](int v) {
            return v == selVal ? style.clockSelectionTextColor : style.clockNumberColor;
        };

        if (section == Section::Hours) {
            if (use24h) {
                for (int p = 0; p < 12; ++p) {
                    int outerV = (p == 0 ? 0 : p + 12);
                    int innerV = (p == 0 ? 12 : p);
                    DrawFaceNumber(ctx, PointForValue(l, outerV), TwoDigits(outerV), numberColor(outerV));
                    DrawFaceNumber(ctx, PointForValue(l, innerV), TwoDigits(innerV), numberColor(innerV));
                }
            } else {
                for (int p = 0; p < 12; ++p) {
                    int v = (p == 0 ? 12 : p);
                    DrawFaceNumber(ctx, PointForValue(l, v), std::to_string(v), numberColor(v));
                }
            }
        } else {
            for (int p = 0; p < 12; ++p) {
                int v = p * 5;
                DrawFaceNumber(ctx, PointForValue(l, v), TwoDigits(v), numberColor(v));
            }
            // A selection between the 5-minute labels still shows its value.
            if (selVal % 5 != 0) {
                DrawFaceNumber(ctx, selPos, TwoDigits(selVal), style.clockSelectionTextColor);
            }
        }
    }

    bool UltraCanvasTimeClockView::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled()) return false;
        if (UltraCanvasUIElement::OnEvent(event)) return true;

        switch (event.type) {
            case UCEventType::MouseDoubleClick:
            case UCEventType::MouseDown: {
                Layout l = ComputeLayout(GetRenderContext());
                Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
                if (l.header.Contains(p)) {
                    if (l.hourRect.Contains(p))   { SetSection(Section::Hours);   return true; }
                    if (l.minuteRect.Contains(p)) { SetSection(Section::Minutes); return true; }
                    if (showSeconds && l.secondRect.Contains(p)) { SetSection(Section::Seconds); return true; }
                    if (!use24h && l.amRect.Contains(p) && value.hour >= 12) {
                        EnsureValuePresent();
                        value.hour -= 12;
                        FireChanged();
                        RequestRedraw();
                        return true;
                    }
                    if (!use24h && l.pmRect.Contains(p) && value.hour < 12) {
                        EnsureValuePresent();
                        value.hour += 12;
                        FireChanged();
                        RequestRedraw();
                        return true;
                    }
                    return true;
                }
                int v;
                if (ValueAtPoint(l, p, v)) {
                    dragging = true;
                    ApplyFaceValue(v, false);
                    return true;
                }
                return false;
            }

            case UCEventType::MouseMove: {
                Layout l = ComputeLayout(GetRenderContext());
                Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
                int v = -1;
                bool overFace = ValueAtPoint(l, p, v);

                if (dragging && overFace) ApplyFaceValue(v, false);

                int hh = 0;
                if      (l.hourRect.Contains(p))   hh = 1;
                else if (l.minuteRect.Contains(p)) hh = 2;
                else if (showSeconds && l.secondRect.Contains(p)) hh = 3;
                else if (!use24h && l.amRect.Contains(p)) hh = 4;
                else if (!use24h && l.pmRect.Contains(p)) hh = 5;

                int newHover = (overFace && !dragging) ? v : -1;
                bool changed = (newHover != hoverValue) || (hh != headerHover);
                hoverValue = newHover;
                headerHover = hh;
                SetMouseCursor((overFace || hh != 0) ? UCMouseCursor::Hand
                                                     : UCMouseCursor::Default);
                if (changed) RequestRedraw();
                return dragging;
            }

            case UCEventType::MouseUp: {
                if (!dragging) return false;
                dragging = false;
                Layout l = ComputeLayout(GetRenderContext());
                Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
                int v;
                if (ValueAtPoint(l, p, v)) ApplyFaceValue(v, true);
                else                       AdvanceSection();
                return true;
            }

            case UCEventType::MouseLeave: {
                if (hoverValue >= 0 || headerHover != 0) {
                    hoverValue = -1;
                    headerHover = 0;
                    RequestRedraw();
                }
                return false;
            }

            case UCEventType::MouseWheel: {
                if (event.wheelDelta == 0) return false;
                EnsureValuePresent();
                int dir = event.wheelDelta > 0 ? 1 : -1;
                switch (section) {
                    case Section::Hours:   value = value.AddSeconds(dir * 3600); break;
                    case Section::Minutes: value = value.AddMinutes(dir * minuteStep); break;
                    case Section::Seconds: value = value.AddSeconds(dir * secondStep); break;
                }
                FireChanged();
                RequestRedraw();
                return true;
            }

            default:
                break;
        }
        return false;
    }

// ===================================================================
// UltraCanvasTimePicker
// ===================================================================

    UltraCanvasTimePicker::UltraCanvasTimePicker(const std::string& identifier,
                                                 float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        mouseCursor = UCMouseCursor::Default;
    }

    void UltraCanvasTimePicker::SetTime(const UCTime& t, bool runCallbacks) {
        UCTime nt = t;
        if (nt.present) ApplyConstraints(nt);
        bool changed = (nt != value);
        value = nt;
        editing = false;
        if (popupOpen) SyncSpinnersFromValue();
        if (changed && runCallbacks && onTimeChanged) onTimeChanged(value);
        RequestRedraw();
    }

    void UltraCanvasTimePicker::Clear(bool runCallbacks) {
        bool changed = value.present;
        value = UCTime();
        editing = false;
        editBuffer.clear();
        if (changed && runCallbacks && onTimeChanged) onTimeChanged(value);
        RequestRedraw();
    }

    void UltraCanvasTimePicker::SetUse24HourFormat(bool use24) {
        if (use24h == use24) return;
        use24h = use24;
        if (popupOpen) { ClosePopup(); }   // rebuilt on next open
        RequestRedraw();
    }

    void UltraCanvasTimePicker::SetShowSeconds(bool show) {
        if (showSeconds == show) return;
        showSeconds = show;
        if (popupOpen) { ClosePopup(); }
        RequestRedraw();
    }

    void UltraCanvasTimePicker::SetPopupStyle(TimePickerPopupStyle s) {
        if (popupStyle == s) return;
        popupStyle = s;
        if (popupOpen) ClosePopup();   // rebuilt with the new style on next open
    }

    void UltraCanvasTimePicker::SetAllowTextInput(bool allow) {
        allowTextInput = allow;
        if (!allow && editing) { editing = false; editBuffer.clear(); RequestRedraw(); }
    }

    std::string UltraCanvasTimePicker::EffectiveFormat() const {
        if (!explicitFormat.empty()) return explicitFormat;
        if (use24h) return showSeconds ? "HH:mm:ss" : "HH:mm";
        return showSeconds ? "hh:mm:ss tt" : "hh:mm tt";
    }

    std::string UltraCanvasTimePicker::BuildDisplayText() const {
        if (!value.present) return "";
        return value.Format(EffectiveFormat());
    }

    void UltraCanvasTimePicker::ApplyConstraints(UCTime& t) const {
        if (!t.present) return;
        if (minTime.present && t < minTime) t = minTime;
        if (maxTime.present && t > maxTime) t = maxTime;
    }

    void UltraCanvasTimePicker::CommitTextInput() {
        editing = false;
        caretPos = 0;
        if (editBuffer.empty()) { RequestRedraw(); return; }
        UCTime parsed;
        if (UCTime::Parse(editBuffer, use24h, showSeconds, parsed)) {
            SetTime(parsed, true);
        } else {
            RequestRedraw();   // revert to last valid display
        }
    }

// ===== GEOMETRY =====

    Rect2Df UltraCanvasTimePicker::ButtonRect() const {
        Rect2Df b = GetLocalBounds();
        float bw = std::min(style.buttonWidth, b.width);
        return Rect2Df(b.x + b.width - bw, b.y, bw, b.height);
    }

    Rect2Df UltraCanvasTimePicker::TextRect() const {
        Rect2Df b = GetLocalBounds();
        float bw = std::min(style.buttonWidth, b.width);
        return Rect2Df(b.x + style.paddingLeft, b.y,
                       b.width - bw - style.paddingLeft, b.height);
    }

// ===== RENDERING =====

    void UltraCanvasTimePicker::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        RenderField(ctx);
    }

    void UltraCanvasTimePicker::RenderField(IRenderContext* ctx) {
        Rect2Df b = GetLocalBounds();

        Color bg = IsDisabled() ? style.disabledColor
                                : (IsHovered() ? style.hoverColor : style.backgroundColor);
        Color border = (IsFocused() || popupOpen) ? style.focusBorderColor : style.borderColor;
        ctx->DrawFilledRectangle(b, bg, style.borderWidth, border, style.cornerRadius);

        FontStyle fs;
        fs.fontFamily = style.fontFamily;
        fs.fontSize = style.fontSize;
        ctx->SetFontStyle(fs);

        // Text (or placeholder / edit buffer).
        Rect2Df tr = TextRect();
        std::string text = editing ? editBuffer : BuildDisplayText();
        bool isPlaceholder = false;
        if (text.empty() && !editing) { text = placeholder; isPlaceholder = true; }

        Color txt = IsDisabled() ? style.disabledTextColor
                    : (isPlaceholder ? style.placeholderColor : style.textColor);
        ctx->SetTextPaint(txt);

        Point2Di ts = ctx->GetTextDimension(text);
        int textY = static_cast<int>(tr.y + (tr.height - ts.y) / 2);
        ctx->DrawText(text, Point2Di(static_cast<int>(tr.x), textY));

        if (editing && IsFocused()) {
            std::string upToCaret = editBuffer.substr(0, std::min(caretPos, editBuffer.size()));
            int caretX = static_cast<int>(tr.x) + ctx->GetTextLineWidth(upToCaret) + 1;
            caretX = std::min(caretX, static_cast<int>(tr.x + tr.width));
            ctx->SetStrokePaint(style.caretColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(Point2Di(caretX, static_cast<int>(b.y + 4)),
                          Point2Di(caretX, static_cast<int>(b.y + b.height - 4)));
        }

        // Clock icon button.
        Rect2Df br = ButtonRect();
        RenderClockIcon(ctx, br, IsDisabled() ? style.disabledTextColor : style.iconColor);
    }

    void UltraCanvasTimePicker::RenderClockIcon(IRenderContext* ctx, const Rect2Df& rect,
                                                const Color& color) {
        float cx = rect.x + rect.width / 2.0f;
        float cy = rect.y + rect.height / 2.0f;
        float r = std::min(rect.width, rect.height) * 0.28f;

        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(1.4f);
        ctx->DrawCircle(Point2Df(cx, cy), r);
        // Hands: one pointing up, one pointing right (a generic "clock" glyph).
        ctx->DrawLine(Point2Df(cx, cy), Point2Df(cx, cy - r * 0.62f));
        ctx->DrawLine(Point2Df(cx, cy), Point2Df(cx + r * 0.5f, cy));
    }

// ===== POPUP =====

    void UltraCanvasTimePicker::BuildPopup() {
        if (popupStyle == TimePickerPopupStyle::Clock) {
            BuildClockPopup();
            return;
        }
        clockView.reset();

        const float pad = 8.0f;
        const float spinW = 52.0f, spinH = 30.0f;
        const float sepW = 10.0f;
        const float ampmW = 58.0f;
        const float gap = 6.0f;
        const float footerH = 26.0f;
        const float footerGap = 8.0f;

        float x = pad;
        float rowY = pad;

        // Measure total width first.
        float contentW = spinW;                 // hour
        contentW += sepW + spinW;               // : minute
        if (showSeconds) contentW += sepW + spinW; // : second
        if (!use24h)     contentW += gap + ampmW;  // AM/PM
        float popupW = pad + contentW + pad;
        float popupH = pad + spinH + footerGap + footerH + pad;

        popup = std::make_shared<UltraCanvasContainer>("TimePickerPopup", 0, 0, popupW, popupH);
        popup->SetBackgroundColor(style.popupBackgroundColor);
        popup->SetBorders(1.0f, style.popupBorderColor, 4.0f);

        auto makeSpinner = [&](const std::string& id, int lo, int hi, int step) {
            auto sp = CreateIntSpinner(id, x, rowY, spinW, spinH, lo, hi, lo, step);
            sp->SetWrap(true);
            sp->SetTextAlignment(TextAlignment::Center);
            sp->onValueChanged = [this](double) {
                if (updatingSpinners) return;
                RecomputeFromSpinners();
            };
            popup->AddChild(sp);
            x += spinW;
            return sp;
        };

        auto addSeparator = [&]() {
            auto colon = CreateLabel("TPsep", x, rowY, sepW, spinH);
            colon->SetText(":");
            colon->SetAlignment(TextAlignment::Center);
            colon->SetFontSize(style.fontSize + 2);
            popup->AddChild(colon);
            x += sepW;
        };

        // Hour spinner (0-23 or 1-12).
        hourSpin = makeSpinner("TPhour", use24h ? 0 : 1, use24h ? 23 : 12, 1);
        addSeparator();
        minuteSpin = makeSpinner("TPminute", 0, 59, minuteStep);
        secondSpin.reset();
        ampmSpin.reset();
        if (showSeconds) {
            addSeparator();
            secondSpin = makeSpinner("TPsecond", 0, 59, secondStep);
        }
        if (!use24h) {
            x += gap;
            ampmSpin = CreateListSpinner("TPampm", x, rowY, ampmW, spinH, {"AM", "PM"});
            ampmSpin->SetWrap(true);
            ampmSpin->SetTextAlignment(TextAlignment::Center);
            ampmSpin->onValueChanged = [this](double) {
                if (updatingSpinners) return;
                RecomputeFromSpinners();
            };
            popup->AddChild(ampmSpin);
        }

        // Footer: "Now" and "Clear".
        float footerY = rowY + spinH + footerGap;
        auto nowBtn = std::make_shared<UltraCanvasButton>("TPnow", pad, footerY, 60.0f, footerH, "Now");
        nowBtn->onClick = [this]() { SetNow(true); ClosePopup(); };
        popup->AddChild(nowBtn);

        auto clearBtn = std::make_shared<UltraCanvasButton>(
                "TPclear", popupW - pad - 60.0f, footerY, 60.0f, footerH, "Clear");
        clearBtn->onClick = [this]() { Clear(true); ClosePopup(); };
        popup->AddChild(clearBtn);
    }

    void UltraCanvasTimePicker::BuildClockPopup() {
        const float pad = 8.0f;
        const float clockW = 264.0f;
        const float clockH = 56.0f + 232.0f;   // header + face area
        const float footerH = 26.0f;
        const float footerGap = 6.0f;

        float popupW = clockW;
        float popupH = clockH + footerGap + footerH + pad;

        popup = std::make_shared<UltraCanvasContainer>("TimePickerPopup", 0, 0, popupW, popupH);
        popup->SetBackgroundColor(style.popupBackgroundColor);
        popup->SetBorders(1.0f, style.popupBorderColor, 4.0f);

        hourSpin.reset();
        minuteSpin.reset();
        secondSpin.reset();
        ampmSpin.reset();

        clockView = std::make_shared<UltraCanvasTimeClockView>("TPclock", 2, 2, clockW - 6, clockH);
        clockView->SetUse24HourFormat(use24h);
        clockView->SetShowSeconds(showSeconds);
        clockView->SetMinuteStep(minuteStep);
        clockView->SetSecondStep(secondStep);
        clockView->SetStyle(style);
        clockView->onTimeChanged = [this](const UCTime& t) {
            if (updatingSpinners) return;
            UCTime nt = t;
            ApplyConstraints(nt);
            bool changed = (nt != value);
            value = nt;
            if (nt != t) SyncSpinnersFromValue();   // constraint moved it: reflect back
            if (changed && onTimeChanged) onTimeChanged(value);
            RequestRedraw();
        };
        clockView->onAccepted = [this](const UCTime&) { ClosePopup(); };
        popup->AddChild(clockView);

        float footerY = clockH + footerGap;
        auto nowBtn = std::make_shared<UltraCanvasButton>("TPnow", pad, footerY, 60.0f, footerH, "Now");
        nowBtn->onClick = [this]() { SetNow(true); ClosePopup(); };
        popup->AddChild(nowBtn);

        auto clearBtn = std::make_shared<UltraCanvasButton>(
                "TPclear", popupW - pad - 60.0f, footerY, 60.0f, footerH, "Clear");
        clearBtn->onClick = [this]() { Clear(true); ClosePopup(); };
        popup->AddChild(clearBtn);
    }

    void UltraCanvasTimePicker::SyncSpinnersFromValue() {
        if (clockView) {
            updatingSpinners = true;
            clockView->SetTime(value);
            updatingSpinners = false;
        }
        if (!hourSpin || !minuteSpin) return;
        updatingSpinners = true;
        UCTime t = value.present ? value : UCTime(0, 0, 0);
        if (use24h) {
            hourSpin->SetValue(t.hour);
        } else {
            hourSpin->SetValue(t.Hour12());
            if (ampmSpin) ampmSpin->SetSelectedIndex(t.IsPM() ? 1 : 0);
        }
        minuteSpin->SetValue(t.minute);
        if (showSeconds && secondSpin) secondSpin->SetValue(t.second);
        updatingSpinners = false;
    }

    void UltraCanvasTimePicker::RecomputeFromSpinners() {
        if (!hourSpin || !minuteSpin) return;
        int h;
        if (use24h) {
            h = hourSpin->GetIntValue();
        } else {
            int h12 = hourSpin->GetIntValue();          // 1..12
            bool pm = ampmSpin && ampmSpin->GetSelectedIndex() == 1;
            h = (h12 % 12) + (pm ? 12 : 0);
        }
        int m = minuteSpin->GetIntValue();
        int s = (showSeconds && secondSpin) ? secondSpin->GetIntValue() : 0;

        UCTime t(h, m, s);
        ApplyConstraints(t);
        bool changed = (t != value);
        value = t;
        // If constraints moved the value, reflect that back into the spinners.
        if (t.hour != h || (showSeconds && t.second != s) || t.minute != m) {
            SyncSpinnersFromValue();
        }
        if (changed && onTimeChanged) onTimeChanged(value);
        RequestRedraw();
    }

    Point2Df UltraCanvasTimePicker::CalculatePopupPosition() const {
        Point2Df globalPos = GetPositionInWindow();
        Size2Df fieldSize = const_cast<UltraCanvasTimePicker*>(this)->GetSize();

        float windowHeight = window ? window->GetHeight() : 9999.0f;
        float windowWidth  = window ? window->GetWidth()  : 9999.0f;

        float popupW = popup ? popup->GetBounds().width  : 0.0f;
        float popupH = popup ? popup->GetBounds().height : 0.0f;

        float spaceBelow = windowHeight - (globalPos.y + fieldSize.height);
        float spaceAbove = globalPos.y;

        float py;
        if (popupH <= spaceBelow || spaceBelow >= spaceAbove)
            py = globalPos.y + fieldSize.height;
        else
            py = globalPos.y - popupH;

        float px = globalPos.x;
        if (px + popupW > windowWidth) px = std::max(0.0f, windowWidth - popupW);

        return Point2Df(px, py);
    }

    void UltraCanvasTimePicker::OpenPopup() {
        if (isPopup || !window || popupOpen) return;

        BuildPopup();
        SyncSpinnersFromValue();

        Point2Df pos = CalculatePopupPosition();

        PopupElementSettings settings;
        settings.popupOwner = shared_from_this();
        settings.closeByEscapeKey = true;
        settings.closeByClickOutside = true;

        popupOpen = true;
        window->OpenPopup(Point2Di(static_cast<int>(pos.x), static_cast<int>(pos.y)),
                          *popup, settings);

        popup->onPopupClosed = [this](ClosePopupReason) {
            popupOpen = false;
            if (onPopupClosed) onPopupClosed();
            RequestRedraw();
        };

        if (onPopupOpened) onPopupOpened();
        RequestRedraw();
    }

    void UltraCanvasTimePicker::ClosePopup() {
        if (window && popup && popupOpen) {
            window->ClosePopup(*popup);
        }
    }

// ===== OVERRIDES =====

    void UltraCanvasTimePicker::SetWindow(UltraCanvasWindowBase* win) {
        UltraCanvasUIElement::SetWindow(win);
        if (popup) popup->SetWindow(win);
    }

    bool UltraCanvasTimePicker::SetFocus(bool focus) {
        bool result = UltraCanvasUIElement::SetFocus(focus);
        if (!focus && editing) CommitTextInput();
        RequestRedraw();
        return result;
    }

    bool UltraCanvasTimePicker::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled()) return false;
        if (UltraCanvasUIElement::OnEvent(event)) return true;

        switch (event.type) {
            case UCEventType::MouseDown:  return HandleMouseDown(event);
            // A rapid second click arrives as a double-click instead of a
            // MouseDown; the popup toggle button must react to every click.
            case UCEventType::MouseDoubleClick: return HandleMouseDown(event);
            case UCEventType::KeyDown:    return HandleKeyDown(event);
            case UCEventType::MouseWheel: return HandleWheel(event);
            case UCEventType::MouseMove: {
                // I-beam over the editable text, arrow over the clock button.
                Point2Df p(static_cast<float>(event.pointer.x),
                           static_cast<float>(event.pointer.y));
                bool overText = allowTextInput && GetLocalBounds().Contains(p) &&
                                !ButtonRect().Contains(p);
                SetMouseCursor(overText ? UCMouseCursor::Text : UCMouseCursor::Default);
                return false;
            }
            case UCEventType::MouseEnter: SetHovered(true);  return true;
            case UCEventType::MouseLeave: SetHovered(false); return true;
            case UCEventType::FocusLost:
                if (editing) CommitTextInput();
                return false;
            default:
                break;
        }
        return false;
    }

    bool UltraCanvasTimePicker::HandleMouseDown(const UCEvent& event) {
        Point2Df p(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
        if (!GetLocalBounds().Contains(p)) return false;

        SetFocus(true);
        bool onButton = ButtonRect().Contains(p);

        if (onButton || !allowTextInput) {
            if (popupOpen) ClosePopup();
            else OpenPopup();
        } else {
            if (!editing) {
                editing = true;
                editBuffer = BuildDisplayText();   // seed with current text
            }
            // Place the caret between the characters nearest the click.
            caretPos = CaretIndexFromX(p.x);
        }
        RequestRedraw();
        return true;
    }

    size_t UltraCanvasTimePicker::CaretIndexFromX(float x) const {
        IRenderContext* ctx = GetRenderContext();
        if (!ctx || editBuffer.empty()) return editBuffer.size();

        FontStyle fs;
        fs.fontFamily = style.fontFamily;
        fs.fontSize = style.fontSize;
        ctx->SetFontStyle(fs);

        float rel = x - TextRect().x;
        if (rel <= 0) return 0;

        // The buffer is short (a formatted time), so scan every boundary and
        // pick the one closest to the click.
        size_t bestIdx = 0;
        float bestDist = rel;                     // boundary 0 sits at width 0
        for (size_t i = 1; i <= editBuffer.size(); ++i) {
            float w = static_cast<float>(ctx->GetTextLineWidth(editBuffer.substr(0, i)));
            float d = std::fabs(w - rel);
            if (d < bestDist) {
                bestDist = d;
                bestIdx = i;
            }
        }
        return bestIdx;
    }

    bool UltraCanvasTimePicker::HandleKeyDown(const UCEvent& event) {
        switch (event.virtualKey) {
            case UCKeys::Down:
            case UCKeys::Up:
                if (!popupOpen) { OpenPopup(); return true; }
                return false;
            case UCKeys::Escape:
                if (popupOpen) { ClosePopup(); return true; }
                if (editing)   { editing = false; caretPos = 0; RequestRedraw(); return true; }
                return false;
            case UCKeys::Return:
            case UCKeys::NumPadEnter:
                if (editing) { CommitTextInput(); return true; }
                if (popupOpen) { ClosePopup(); return true; }
                return false;
            case UCKeys::Backspace:
                if (editing && caretPos > 0) {
                    editBuffer.erase(caretPos - 1, 1);
                    caretPos--;
                    RequestRedraw();
                    return true;
                }
                return false;
            case UCKeys::Delete:
                if (editing && caretPos < editBuffer.size()) {
                    editBuffer.erase(caretPos, 1);
                    RequestRedraw();
                    return true;
                }
                return false;
            case UCKeys::Left:
                if (editing) {
                    if (caretPos > 0) { caretPos--; RequestRedraw(); }
                    return true;
                }
                return false;
            case UCKeys::Right:
                if (editing) {
                    if (caretPos < editBuffer.size()) { caretPos++; RequestRedraw(); }
                    return true;
                }
                return false;
            case UCKeys::Home:
                if (editing) { caretPos = 0; RequestRedraw(); return true; }
                return false;
            case UCKeys::End:
                if (editing) { caretPos = editBuffer.size(); RequestRedraw(); return true; }
                return false;
            default:
                HandleKeyChar(event);
                break;
        }
        return false;
    }

    bool UltraCanvasTimePicker::HandleKeyChar(const UCEvent& event) {
        if (!IsFocused() || !allowTextInput) return false;
        char c = event.character;
        // Accept digits and the separators used by the time formats.
        bool ok = std::isdigit(static_cast<unsigned char>(c)) ||
                  c == ':' || c == ' ' || c == '.' ||
                  c == 'A' || c == 'P' || c == 'M' ||
                  c == 'a' || c == 'p' || c == 'm';
        if (!ok) return false;
        if (!editing) {
            // Typing without clicking first starts a fresh entry.
            editing = true;
            editBuffer.clear();
            caretPos = 0;
        }
        caretPos = std::min(caretPos, editBuffer.size());
        editBuffer.insert(caretPos, 1, c);
        caretPos++;
        RequestRedraw();
        return true;
    }

    bool UltraCanvasTimePicker::HandleWheel(const UCEvent& event) {
        if (!IsHovered() && !IsFocused()) return false;
        if (event.wheelDelta == 0 || popupOpen) return false;
        UCTime base = value.present ? value : UCTime::Now();
        UCTime t = base.AddMinutes(event.wheelDelta > 0 ? minuteStep : -minuteStep);
        SetTime(t, true);
        return true;
    }

} // namespace UltraCanvas
