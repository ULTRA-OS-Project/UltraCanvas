// core/UltraCanvasTimePicker.cpp
// Platform-independent time-of-day picker implementation.
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework

#include "UltraCanvasTimePicker.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include <ctime>
#include <cctype>
#include <cstdio>
#include <cstring>
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
            int caretX = static_cast<int>(tr.x) + ts.x + 1;
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

    void UltraCanvasTimePicker::SyncSpinnersFromValue() {
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
            case UCEventType::KeyDown:    return HandleKeyDown(event);
            case UCEventType::KeyChar:    return HandleKeyChar(event);
            case UCEventType::MouseWheel: return HandleWheel(event);
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
            editing = true;
            editBuffer = BuildDisplayText();   // seed with current text
        }
        RequestRedraw();
        return true;
    }

    bool UltraCanvasTimePicker::HandleKeyDown(const UCEvent& event) {
        switch (event.virtualKey) {
            case UCKeys::Down:
            case UCKeys::Up:
                if (!popupOpen) { OpenPopup(); return true; }
                return false;
            case UCKeys::Escape:
                if (popupOpen) { ClosePopup(); return true; }
                if (editing)   { editing = false; RequestRedraw(); return true; }
                return false;
            case UCKeys::Return:
            case UCKeys::NumPadEnter:
                if (editing) { CommitTextInput(); return true; }
                if (popupOpen) { ClosePopup(); return true; }
                return false;
            case UCKeys::Backspace:
                if (editing && !editBuffer.empty()) {
                    editBuffer.pop_back();
                    RequestRedraw();
                    return true;
                }
                return false;
            default:
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
        editing = true;
        editBuffer.push_back(c);
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
