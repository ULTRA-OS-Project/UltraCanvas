// UltraCanvasTimecodeField.cpp
// The timecode field: drawing the four segments with the selected one
// highlighted and the leading zeros dimmed, the entry buffer that fills from
// the right, relative entries, stepping, validation and the keys.
// ui-reuse-exempt: UltraCanvasTimecodeField is the timecode input itself - four segments, no caret.
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#include "UltraCanvasTimecodeField.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasVideoTimelineModel.h"   // TimelineFramesToTimecode, TimelineTimecodeToFrames

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

namespace {

constexpr size_t kMaxDigits = 8;

long long PerSecond(double frameRate) {
    return std::max(1LL, static_cast<long long>(std::llround(frameRate > 0.0 ? frameRate : 25.0)));
}

// The segments of a frame count: hours, minutes, seconds, frames
void Split(long long frames, double frameRate, long long out[4]) {
    const long long perSecond = PerSecond(frameRate);
    frames = std::max(0LL, frames);
    out[3] = frames % perSecond;
    long long total = frames / perSecond;
    out[2] = total % 60;
    total /= 60;
    out[1] = total % 60;
    out[0] = total / 60;
}

long long Join(const long long segments[4], double frameRate) {
    return ((segments[0] * 60 + segments[1]) * 60 + segments[2]) * PerSecond(frameRate) + segments[3];
}

// The entry as an eight-digit mask, filled from the right
std::string Mask(const std::string& entry) {
    std::string mask(kMaxDigits, '0');
    const size_t n = std::min(entry.size(), kMaxDigits);
    for (size_t i = 0; i < n; ++i) mask[kMaxDigits - n + i] = entry[entry.size() - n + i];
    return mask;
}

std::string WithColons(const std::string& eightDigits) {
    return eightDigits.substr(0, 2) + ":" + eightDigits.substr(2, 2) + ":" + eightDigits.substr(4, 2) + ":" +
           eightDigits.substr(6, 2);
}

} // namespace

TimecodeFieldStyle TimecodeFieldStyle::LightPalette() { return TimecodeFieldStyle(); }

TimecodeFieldStyle TimecodeFieldStyle::DarkPalette() {
    TimecodeFieldStyle s;
    s.background = Color(27, 30, 34);
    s.border = Color(115, 123, 133);
    s.borderFocused = Color(91, 196, 245);
    s.borderInvalid = Color(255, 111, 97);
    s.ink = Color(236, 238, 240);
    s.inkFaint = Color(138, 145, 154);
    s.selection = Color(242, 165, 58);
    s.onSelection = Color(27, 19, 5);
    s.sign = Color(242, 165, 58);
    return s;
}

UltraCanvasTimecodeField::UltraCanvasTimecodeField(const std::string& id, int x, int y, int w, int h)
    : UltraCanvasUIElement(id, x, y, w, h) {
    SetMouseCursor(UCMouseCursor::Default);
}

// ---- value ----

long long UltraCanvasTimecodeField::Clamped(long long value) const {
    value = std::max(0LL, value);
    if (maximumFrames > 0) value = std::min(value, maximumFrames);
    return value;
}

void UltraCanvasTimecodeField::SetFrames(long long f) {
    frames = Clamped(f);
    entry.clear();
    sign = 0;
    invalid = false;
    RequestRedraw();
}

void UltraCanvasTimecodeField::SetSeconds(double seconds) {
    SetFrames(static_cast<long long>(std::llround(seconds * PerSecond(frameRate))));
}

double UltraCanvasTimecodeField::GetSeconds() const {
    return static_cast<double>(frames) / static_cast<double>(PerSecond(frameRate));
}

void UltraCanvasTimecodeField::SetFrameRate(double rate) {
    frameRate = rate > 0.0 ? rate : 25.0;
    Refresh();
}

void UltraCanvasTimecodeField::SetMaximumFrames(long long maximum) {
    maximumFrames = std::max(0LL, maximum);
    frames = Clamped(frames);
    Refresh();
}

void UltraCanvasTimecodeField::SetReadout(bool r) {
    readout = r;
    if (readout) {
        Cancel();
        selectedSegment = -1;
    }
    RequestRedraw();
}

void UltraCanvasTimecodeField::SetStyle(const TimecodeFieldStyle& s) {
    style = s;
    RequestRedraw();
}

// ---- entry ----

std::string UltraCanvasTimecodeField::GetText() const {
    if (!IsEditing()) return TimelineFramesToTimecode(frames, frameRate);
    std::string text = WithColons(Mask(entry));
    if (sign) text.insert(text.begin(), sign);
    return text;
}

bool UltraCanvasTimecodeField::EntryFrames(long long& result) const {
    if (!IsEditing()) { result = frames; return true; }
    if (entry.empty()) return false;                 // a sign alone is not yet an entry
    const std::string mask = Mask(entry);
    long long segments[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) segments[i] = (mask[static_cast<size_t>(i) * 2] - '0') * 10 + (mask[static_cast<size_t>(i) * 2 + 1] - '0');
    if (segments[1] >= 60 || segments[2] >= 60 || segments[3] >= PerSecond(frameRate)) return false;
    long long value = Join(segments, frameRate);
    if (sign == '+') value = frames + value;
    else if (sign == '-') value = frames - value;
    if (value < 0) return false;
    if (maximumFrames > 0 && value > maximumFrames) return false;
    result = value;
    return true;
}

void UltraCanvasTimecodeField::Refresh() {
    long long value = 0;
    invalid = IsEditing() && !entry.empty() && !EntryFrames(value);
    RequestRedraw();
}

bool UltraCanvasTimecodeField::Commit() {
    long long value = 0;
    if (!EntryFrames(value)) {
        invalid = IsEditing();
        RequestRedraw();
        return false;
    }
    const bool changed = value != frames || IsEditing();
    frames = value;
    entry.clear();
    sign = 0;
    invalid = false;
    if (changed && onCommit) onCommit(frames);
    RequestRedraw();
    return true;
}

void UltraCanvasTimecodeField::Cancel() {
    entry.clear();
    sign = 0;
    invalid = false;
    RequestRedraw();
}

void UltraCanvasTimecodeField::SelectSegment(int segment) {
    selectedSegment = segment < 0 || segment > 3 ? -1 : segment;
    RequestRedraw();
}

void UltraCanvasTimecodeField::Step(int segment, int direction) {
    if (readout || direction == 0) return;
    if (IsEditing() && !Commit()) return;
    segment = std::clamp(segment, 0, 3);
    long long segments[4];
    Split(frames, frameRate, segments);
    const long long units[4] = {3600 * PerSecond(frameRate), 60 * PerSecond(frameRate), PerSecond(frameRate), 1};
    const long long next = Clamped(frames + (direction > 0 ? 1 : -1) * units[segment]);
    if (next == frames) return;
    frames = next;
    if (onCommit) onCommit(frames);
    RequestRedraw();
}

bool UltraCanvasTimecodeField::SetAccessibleValueText(const std::string& text) {
    long long value = 0;
    if (!TimelineTimecodeToFrames(text, frameRate, value) || value < 0) return false;
    const long long clamped = Clamped(value);
    if (clamped != value) return false;
    entry.clear();
    sign = 0;
    invalid = false;
    if (clamped != frames) {
        frames = clamped;
        if (onCommit) onCommit(frames);
    }
    RequestRedraw();
    return true;
}

int UltraCanvasTimecodeField::SegmentAt(float localX) const {
    if (charAdvance <= 0.0f) return -1;
    const float offset = localX - textLeft;
    if (offset < 0.0f) return 0;
    const int character = static_cast<int>(offset / charAdvance);   // 0..10 over "HH:MM:SS:FF"
    return std::clamp(character / 3, 0, 3);
}

// ---- drawing ----

void UltraCanvasTimecodeField::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!ctx) return;
    const Rect2Df b = GetLocalBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) return;
    const bool focused = IsFocused() && !readout;

    if (!readout) {
        ctx->SetFillPaint(style.background);
        ctx->FillRoundedRectangle(Rect2Dd(b.x, b.y, b.width, b.height), style.cornerRadius);
        const Color borderColor = invalid ? style.borderInvalid : focused ? style.borderFocused : style.border;
        ctx->SetStrokePaint(borderColor);
        ctx->SetStrokeWidth(invalid || focused ? 2.0f : 1.0f);
        const double inset = invalid || focused ? 1.0 : 0.5;
        ctx->DrawRoundedRectangle(Rect2Dd(b.x + inset, b.y + inset, b.width - 2.0 * inset, b.height - 2.0 * inset),
                                  style.cornerRadius);
    }

    const float size = readout ? style.readoutSize : style.fontSize;
    ctx->SetFontFace(style.fontFamily, readout ? FontWeight::Bold : FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(size * 0.75f);   // pixels to points
    const Size2Di zero = ctx->GetTextDimensions("0", Size2Di(0, 0));
    charAdvance = static_cast<float>(zero.width);
    textHeight = static_cast<float>(zero.height);
    const std::string text = GetText();                     // "HH:MM:SS:FF", with a sign in front of an entry
    const bool signed_ = IsEditing() && sign != 0;
    const std::string digits = signed_ ? text.substr(1) : text;
    const float width = charAdvance * (11.0f + (signed_ ? 1.0f : 0.0f));
    float left = readout ? b.x + (b.width - width) / 2.0f : b.x + style.paddingX;
    if (!readout && b.width - 2.0f * style.paddingX > width) left = b.x + (b.width - width) / 2.0f;
    const float top = b.y + (b.height - textHeight) / 2.0f;
    textTop = top;
    if (signed_) {
        ctx->SetTextPaint(style.sign);
        ctx->SetTextAlignment(TextAlignment::Left);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Top);
        ctx->DrawText(std::string(1, sign), Point2Dd(left, top));
        left += charAdvance;
    }
    textLeft = left;

    // which characters are dimmed: at rest the zero segments before the
    // first non-zero one, during an entry the digits not typed
    bool dim[11] = {false, false, false, false, false, false, false, false, false, false, false};
    if (IsEditing()) {
        const size_t typed = std::min(entry.size(), kMaxDigits);
        for (size_t d = 0; d + typed < kMaxDigits; ++d) {   // digit index d of 8 is character d + d / 2
            const size_t character = d + d / 2;
            dim[character] = true;
        }
    } else {
        for (int s = 0; s < 4; ++s) {
            const bool zeroSegment = digits[static_cast<size_t>(s) * 3] == '0' && digits[static_cast<size_t>(s) * 3 + 1] == '0';
            if (!zeroSegment || s == 3) break;
            dim[static_cast<size_t>(s) * 3] = dim[static_cast<size_t>(s) * 3 + 1] = true;
            if (s < 3) dim[static_cast<size_t>(s) * 3 + 2] = true;
        }
    }
    if (focused && selectedSegment >= 0 && !IsEditing()) {
        const float sx = left + charAdvance * (3.0f * selectedSegment) - 2.0f;
        ctx->SetFillPaint(style.selection);
        ctx->FillRoundedRectangle(Rect2Dd(sx, top - 1.0, charAdvance * 2.0f + 4.0f, textHeight + 2.0), 2.0);
    }
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->SetTextVerticalAlignment(VerticalAlignment::Top);
    for (size_t i = 0; i < digits.size() && i < 11; ++i) {
        const bool inSelected = focused && selectedSegment >= 0 && !IsEditing() &&
                                static_cast<int>(i / 3) == selectedSegment && i % 3 != 2;
        ctx->SetTextPaint(inSelected ? style.onSelection : dim[i] ? style.inkFaint : style.ink);
        ctx->DrawText(std::string(1, digits[i]), Point2Dd(left + charAdvance * static_cast<float>(i), top));
    }
}

// ---- events ----

bool UltraCanvasTimecodeField::OnEvent(const UCEvent& event) {
    if (UltraCanvasUIElement::OnEvent(event)) return true;
    if (!IsVisible() || IsDisabled() || readout) return false;
    const Point2Df inWindow = GetPositionInWindow();
    const float x = static_cast<float>(event.pointer.x) - inWindow.x;
    const float y = static_cast<float>(event.pointer.y) - inWindow.y;
    const Rect2Df b = GetLocalBounds();
    const bool inside = x >= 0.0f && y >= 0.0f && x <= b.width && y <= b.height;

    switch (event.type) {
        case UCEventType::MouseDown:
            if (!inside || event.button != UCMouseButton::Left) return false;
            SetFocus(true);
            if (IsEditing()) Commit();
            SelectSegment(SegmentAt(x));
            return true;
        case UCEventType::MouseWheel: {
            if (!inside || event.wheelDelta == 0) return false;
            const int segment = IsFocused() && selectedSegment >= 0 ? selectedSegment : SegmentAt(x);
            Step(segment < 0 ? 3 : segment, event.wheelDelta > 0 ? 1 : -1);
            return true;
        }
        case UCEventType::FocusLost:
            if (IsEditing() && !Commit()) Cancel();
            RequestRedraw();
            return false;
        case UCEventType::KeyDown:
            break;
        default:
            return false;
    }
    if (!IsFocused()) return false;

    // Ctrl+C copies the timecode, Ctrl+V takes one (or fewer groups, or an offset)
    if ((event.ctrl || event.meta) && (event.virtualKey == UCKeys::C || event.virtualKey == UCKeys::V)) {
        UltraCanvasClipboard* clipboard = GetClipboard();
        if (!clipboard) return true;
        if (event.virtualKey == UCKeys::C) {
            clipboard->SetText(TimelineFramesToTimecode(frames, frameRate));
            return true;
        }
        std::string pasted;
        if (!clipboard->GetText(pasted)) return true;
        long long value = 0;
        if (TimelineTimecodeToFrames(pasted, frameRate, value)) {
            const bool relative = !pasted.empty() && (pasted[0] == '+' || pasted[0] == '-');
            const long long target = relative ? frames + value : value;
            if (target >= 0 && Clamped(target) == target) {
                entry.clear();
                sign = 0;
                invalid = false;
                if (target != frames) {
                    frames = target;
                    if (onCommit) onCommit(frames);
                }
                RequestRedraw();
                return true;
            }
        }
        invalid = true;   // what was pasted is not a timecode this field can take
        RequestRedraw();
        return true;
    }

    // typed characters: digits, and a sign at the start of an entry
    char c = event.character;
    if (c == 0 && event.text.size() == 1) c = event.text[0];
    if (c == 0) {
        if (event.virtualKey >= UCKeys::Key0 && event.virtualKey <= UCKeys::Key9)
            c = static_cast<char>('0' + (static_cast<int>(event.virtualKey) - static_cast<int>(UCKeys::Key0)));
        else if (event.virtualKey >= UCKeys::NumPad0 && event.virtualKey <= UCKeys::NumPad9)
            c = static_cast<char>('0' + (static_cast<int>(event.virtualKey) - static_cast<int>(UCKeys::NumPad0)));
        else if (event.virtualKey == UCKeys::Plus || event.virtualKey == UCKeys::NumPadPlus) c = '+';
        else if (event.virtualKey == UCKeys::Minus || event.virtualKey == UCKeys::NumPadMinus) c = '-';
    }
    if (c >= '0' && c <= '9' && !event.ctrl && !event.meta) {
        if (entry.size() >= kMaxDigits) entry.erase(entry.begin());
        entry.push_back(c);
        selectedSegment = -1;
        Refresh();
        return true;
    }
    if ((c == '+' || c == '-') && !event.ctrl && !event.meta) {
        if (entry.empty()) { sign = c; Refresh(); return true; }
        return true;
    }
    switch (event.virtualKey) {
        case UCKeys::Return:
        case UCKeys::NumPadEnter:
            Commit();
            return true;
        case UCKeys::Escape:
            if (IsEditing()) { Cancel(); return true; }
            return false;
        case UCKeys::Backspace:
            if (!entry.empty()) entry.pop_back();
            else if (sign) sign = 0;
            Refresh();
            return true;
        case UCKeys::Up:
            Step(selectedSegment < 0 ? 3 : selectedSegment, 1);
            return true;
        case UCKeys::Down:
            Step(selectedSegment < 0 ? 3 : selectedSegment, -1);
            return true;
        case UCKeys::Left:
            if (IsEditing()) return false;
            SelectSegment(selectedSegment < 0 ? 3 : (selectedSegment + 3) % 4);
            return true;
        case UCKeys::Right:
            if (IsEditing()) return false;
            SelectSegment(selectedSegment < 0 ? 0 : (selectedSegment + 1) % 4);
            return true;
        case UCKeys::Tab:
            if (IsEditing() && !Commit()) return true;
            SelectSegment(event.shift ? (selectedSegment <= 0 ? 3 : selectedSegment - 1)
                                      : (selectedSegment < 0 ? 0 : (selectedSegment + 1) % 4));
            return true;
        case UCKeys::Home:
            if (IsEditing()) return false;
            SelectSegment(0);
            return true;
        case UCKeys::End:
            if (IsEditing()) return false;
            SelectSegment(3);
            return true;
        default:
            return false;
    }
}

std::shared_ptr<UltraCanvasTimecodeField> CreateTimecodeField(const std::string& id, int x, int y, int w, int h) {
    return std::make_shared<UltraCanvasTimecodeField>(id, x, y, w, h);
}

} // namespace UltraCanvas
