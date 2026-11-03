// Apps/SmartHomeCenter/ui/SmartHomeCenterStyle.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SmartHomeCenterStyle.h"
#include "UltraCanvasSmartHomeIcons.h"

#include <algorithm>
#include <chrono>
#include <ctime>

using namespace UltraCanvas;
using namespace UltraCanvas::SmartHome;

namespace SmartHomeCenter {

Tone ToneOf(const DeviceView& v) {
    if (v.alert) return kAlarm;
    switch (v.kind) {
        case DeviceKind::Light:
        case DeviceKind::Switch: return v.active ? kLit : kIdle;
        case DeviceKind::Thermostat: return v.active ? kHeat : kClimate;
        case DeviceKind::Lock:
        case DeviceKind::Blind: return v.active ? kOpen : kIdle;
        case DeviceKind::ClimateSensor: return kClimate;
        case DeviceKind::BinarySensor: return v.active ? kSense : kIdle;
        default: return kIdle;
    }
}

UltraCanvas::BadgeVariant StatusVariant(const DeviceView& v) {
    if (v.alert) return BadgeVariant::Danger;
    if (v.active && (v.kind == DeviceKind::Light || v.kind == DeviceKind::Switch || v.kind == DeviceKind::Thermostat))
        return BadgeVariant::Warning;
    if (v.active && v.kind == DeviceKind::BinarySensor) return BadgeVariant::Info;
    if (v.active) return BadgeVariant::Successful;
    return BadgeVariant::Neutral;
}

// ===== TIME AND WORDS =====

namespace {

std::tm Local(int64_t seconds) {
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    return local;
}

} // namespace

int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string ClockText(int64_t seconds) {
    const std::tm local = Local(seconds);
    char text[16];
    std::strftime(text, sizeof text, "%H:%M:%S", &local);
    return text;
}

std::string DateTimeText(int64_t seconds) {
    const std::tm local = Local(seconds);
    char text[32];
    std::strftime(text, sizeof text, "%d %b %H:%M", &local);
    return text;
}

std::string AgoText(uint64_t lastSeenMs) {
    if (lastSeenMs == 0) return "never";
    const int64_t seconds = NowSeconds() - static_cast<int64_t>(lastSeenMs / 1000);
    if (seconds < 10) return "just now";
    if (seconds < 60) return std::to_string(seconds) + " s ago";
    if (seconds < 3600) return std::to_string(seconds / 60) + " min ago";
    return std::to_string(seconds / 3600) + " h ago";
}

std::string SecurityText(SmartHomeSecurityLevel level) {
    switch (level) {
        case SmartHomeSecurityLevel::Certified: return "Certified (attested)";
        case SmartHomeSecurityLevel::Encrypted: return "Encrypted";
        case SmartHomeSecurityLevel::Basic: return "Basic";
        default: return "Unencrypted";
    }
}

std::string RoomText(const std::string& room) {
    return room.empty() ? std::string(kNoRoom) : room;
}

// ===== BUILDING BLOCKS =====

void NoScrollbars(const std::shared_ptr<UltraCanvasContainer>& c) {
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    c->SetContainerStyle(plain);
}

std::shared_ptr<UltraCanvasContainer> Column(const std::string& id, float gap) {
    auto c = CreateContainer(id);
    c->layout.SetFlexColumn().SetFlexGap(gap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    NoScrollbars(c);
    return c;
}

std::shared_ptr<UltraCanvasContainer> Row(const std::string& id, float gap, CSSLayout::AlignItems align) {
    auto c = CreateContainer(id);
    c->layout.SetFlexRow().SetFlexGap(gap).SetFlexAlignItems(align);
    NoScrollbars(c);
    return c;
}

void Fixed(const std::shared_ptr<UltraCanvasUIElement>& e, float w, float h) {
    e->SetElementSize(w < 0 ? CSSLayout::Dimension::Auto() : CSSLayout::Dimension::Px(w),
                      h < 0 ? CSSLayout::Dimension::Auto() : CSSLayout::Dimension::Px(h));
    e->layoutItem.SetFlexGrow(0);
    e->layoutItem.SetFlexShrink(0);
}

void Grow(const std::shared_ptr<UltraCanvasUIElement>& e) {
    e->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    e->layoutItem.SetFlexGrow(1);
    e->layoutItem.SetFlexShrink(1);
    e->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

std::shared_ptr<UltraCanvasContainer> Spacer(const std::string& id) {
    auto s = CreateContainer(id);
    NoScrollbars(s);
    s->layoutItem.SetFlexGrow(1);
    s->layoutItem.SetFlexShrink(1);
    s->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    s->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(1));
    return s;
}

std::shared_ptr<UltraCanvasContainer> ScrollColumn(const std::string& id, float gap) {
    auto c = CreateScrollableContainer(id, 0, 0, 0, 0, false, false);
    ContainerStyle scroll = c->GetContainerStyle();
    scroll.autoShowHorizontalScrollbar = false;   // the content wraps; only height overflows
    c->SetContainerStyle(scroll);
    c->layout.SetFlexColumn().SetFlexGap(gap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    return c;
}

void PaintDeviceBadge(UltraCanvasContainer& badge, UltraCanvasImageElement& icon, const DeviceView& view) {
    badge.SetBackgroundColor(view.online ? SmartHomeCategoryColor(view.info.Category) : kFaint);
    icon.SetTintColor(Colors::White);
}

void Card(const std::shared_ptr<UltraCanvasContainer>& c, float padding) {
    c->SetBackgroundColor(kSurface);
    c->SetBorders(1.0f, kBorder, 14.0f);
    c->SetPadding(padding);
}

std::shared_ptr<UltraCanvasLabel> Text(const std::string& id, const std::string& text, float size,
                                       const Color& color, bool bold, float height) {
    auto label = CreateLabel(id, 0, 0, 0, height > 0 ? height : size + 10, text);
    label->SetFontSize(size);
    label->SetTextColor(color);
    if (bold) label->SetFontWeight(FontWeight::Bold);
    label->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(height > 0 ? height : size + 10));
    label->layoutItem.SetFlexShrink(0);
    return label;
}

std::shared_ptr<UltraCanvasLabel> Paragraph(const std::string& id, const std::string& text, float size,
                                            const Color& color, int lines) {
    auto label = Text(id, text, size, color, false, static_cast<float>(lines) * (size + 7.0f));
    label->SetWrap(TextWrap::WrapWord);
    label->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    return label;
}

std::shared_ptr<UltraCanvasLabel> Caption(const std::string& id, const std::string& text) {
    return Text(id, text, 11, kFaint, true, 18);
}

std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id, const std::string& text, ButtonKind kind,
                                              float height) {
    auto b = CreateButton(id, 0, 0, 120, height, text);
    b->SetFontSize(height >= 38 ? 14 : 13);
    b->SetCornerRadius(height >= 38 ? 10 : 8);
    b->SetPadding(0, 16);
    switch (kind) {
        case ButtonKind::Primary:
            b->SetColors(kAccent, Color(56, 82, 214), Color(46, 68, 190), Color(170, 180, 220));
            b->SetTextColors(Colors::White);
            b->SetBorder(0, Colors::Transparent);
            b->SetIconMaskColor(Colors::White);
            break;
        case ButtonKind::Secondary:
            b->SetColors(kSurface, kHover, Color(228, 232, 240), kSurface);
            b->SetTextColors(kInk);
            b->SetBorder(1, kBorder);
            b->SetIconMaskColor(kMuted);
            break;
        case ButtonKind::Danger:
            b->SetColors(kSurface, kAlarm.soft, Color(250, 220, 220), kSurface);
            b->SetTextColors(kAlarm.fg);
            b->SetBorder(1, kAlarm.border);
            b->SetIconMaskColor(kAlarm.fg);
            break;
        case ButtonKind::Quiet:
            b->SetColors(Colors::Transparent, kHover);
            b->SetTextColors(kMuted, kInk);
            b->SetBorder(0, Colors::Transparent);
            b->SetIconMaskColor(kMuted);
            break;
    }
    b->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(height));
    b->layoutItem.SetFlexShrink(0);
    return b;
}

std::shared_ptr<UltraCanvasSwitch> MakeSwitch(const std::string& id, bool on) {
    auto s = UltraCanvasSwitch::Create(id, 0, 0, "", on);
    s->SetTrackSize(46, 26);
    SwitchVisualStyle style = s->GetVisualStyle();
    style.trackOnColor = kAccent;
    style.trackOffColor = Color(214, 219, 230);
    style.trackBorderColor = Colors::Transparent;
    style.thumbBorderColor = Colors::Transparent;
    s->SetVisualStyle(style);
    Fixed(s, 48, 28);
    return s;
}

std::shared_ptr<UltraCanvasTextInput> MakeInput(const std::string& id, const std::string& text,
                                                const std::string& placeholder) {
    auto input = CreateTextInput(id, 0, 0, 240, 36);
    TextInputStyle style = input->GetStyle();
    style.backgroundColor = kSurface;
    style.borderColor = kBorder;
    style.focusBorderColor = kAccent;
    style.textColor = kInk;
    style.placeholderColor = kFaint;
    style.borderRadius = 8;
    style.paddingLeft = style.paddingRight = 10;
    style.fontStyle.fontSize = 14;
    input->SetStyle(style);
    input->SetText(text);
    input->SetPlaceholder(placeholder);
    input->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(36));
    input->layoutItem.SetFlexShrink(0);
    return input;
}

void StyleTable(const std::shared_ptr<UltraCanvasListView>& list) {
    ListViewStyle style;
    style.headerBackgroundColor = Color(247, 248, 251);
    style.headerTextColor = kMuted;
    style.headerFontSize = 11;
    style.headerHeight = 34;
    style.gridLineColor = Color(238, 240, 245);
    style.showGridLines = false;
    style.alternateRowColors = true;
    style.alternateRowColor = Color(250, 251, 253);
    style.selectionBackgroundColor = kAccentSoft;
    style.hoverBackgroundColor = Color(244, 246, 250);
    style.showHeader = true;
    list->SetStyle(style);
    list->SetBorders(0.0f, Colors::Transparent);   // the card around it is the frame
    if (auto* delegate = dynamic_cast<UltraCanvasDefaultListDelegate*>(list->GetDelegate())) {
        delegate->SetTextColor(kInk);
        delegate->SetSelectedTextColor(kAccent);
        delegate->SetFontSize(13);
        delegate->SetTextPadding(12);
    }
}

} // namespace SmartHomeCenter
