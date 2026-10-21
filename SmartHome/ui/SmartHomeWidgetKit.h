// ui/SmartHomeWidgetKit.h
// What the Smart Home widgets are built from, in one place: their colours,
// and small makers for the framework elements they are composed of - columns
// and rows that lay out by flex, labels, buttons, sliders, switches. Internal
// to SmartHome/ui; an application uses the widgets, not this.
//
// The widgets are UltraCanvasContainers of real elements (proposal §4.8):
// anything that takes input is a button, slider, switch, segmented control,
// text input, list view or colour control, so focus, keyboard, theming,
// tooltips and accessibility come with it. What is only a picture of the
// device (a thermostat's ring, a blind in its window, a sensor's history) is
// a small view element of its own inside that layout.
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSwitch.h"

#include <memory>
#include <string>

namespace UltraCanvas {
namespace SmartHome {
namespace Kit {

inline const Color kText(0x21, 0x21, 0x21);
inline const Color kTextDim(0x75, 0x75, 0x75);
inline const Color kTrack(0xE0, 0xE0, 0xE0);
inline const Color kAccent(33, 150, 243);
inline const Color kPanelBg(0xFA, 0xFA, 0xFA);
inline const Color kGood(76, 175, 80);
inline const Color kWarn(0xFF, 0x98, 0x00);
inline const Color kBad(0xF4, 0x43, 0x36);

inline void Plain(UltraCanvasContainer& c) {
    ContainerStyle style;
    style.autoShowScrollbars = false;
    c.SetContainerStyle(style);
}

// Lays out `c` as a column (or a row) of its children.
inline void AsColumn(UltraCanvasContainer& c, float gap, float padding) {
    c.layout.SetFlexColumn().SetFlexGap(gap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    c.SetPadding(padding);
    Plain(c);
}

inline std::shared_ptr<UltraCanvasContainer> Row(const std::string& id, float gap, float height) {
    auto r = CreateContainer(id);
    r->layout.SetFlexRow().SetFlexGap(gap).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    r->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(height));
    r->layoutItem.SetFlexShrink(0);
    Plain(*r);
    return r;
}

// Takes what is left along its parent's direction.
inline void Grow(UltraCanvasUIElement& e) {
    e.layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    e.layoutItem.SetFlexGrow(1);
    e.layoutItem.SetFlexShrink(1);
}

inline void Height(UltraCanvasUIElement& e, float h) {
    e.SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(h));
    e.layoutItem.SetFlexShrink(0);
}

inline void Size(UltraCanvasUIElement& e, float w, float h) {
    e.SetElementSize(CSSLayout::Dimension::Px(w), CSSLayout::Dimension::Px(h));
    e.layoutItem.SetFlexGrow(0);
    e.layoutItem.SetFlexShrink(0);
}

inline std::shared_ptr<UltraCanvasLabel> Label(const std::string& id, const std::string& text, float size,
                                               const Color& color, bool bold = false) {
    auto l = CreateLabel(id, 0, 0, 0, size + 8.0f, text);
    l->SetFontSize(size);
    l->SetTextColor(color);
    if (bold) l->SetFontWeight(FontWeight::Bold);
    Height(*l, size + 8.0f);
    return l;
}

inline std::shared_ptr<UltraCanvasButton> Button(const std::string& id, const std::string& text, bool primary = false,
                                                 float height = 28.0f) {
    auto b = CreateButton(id, 0, 0, 80, height, text);
    b->SetFontSize(11);
    b->SetCornerRadius(height / 2.0f);
    if (primary) {
        b->SetColors(kAccent, Color(25, 118, 210), Color(21, 101, 192), kTrack);
        b->SetTextColors(Colors::White);
        b->SetBorder(0, Colors::Transparent);
    } else {
        b->SetColors(kTrack, Color(0xD6, 0xD6, 0xD6), Color(0xCC, 0xCC, 0xCC), kTrack);
        b->SetTextColors(kText);
        b->SetBorder(0, Colors::Transparent);
    }
    Height(*b, height);
    return b;
}

inline std::shared_ptr<UltraCanvasSlider> Slider(const std::string& id, float min, float max, const Color& fill) {
    auto s = CreateHorizontalSlider(id, 0, 0, 200, 24, min, max);
    s->SetColors(kTrack, fill, Colors::White);
    s->SetTrackHeight(6);
    s->SetHandleSize(16);
    s->SetValueDisplay(SliderValueDisplay::NoDisplay);
    Height(*s, 24);
    return s;
}

inline std::shared_ptr<UltraCanvasSwitch> Switch(const std::string& id, bool on) {
    auto s = UltraCanvasSwitch::Create(id, 0, 0, "", on);
    s->SetTrackSize(42, 24);
    SwitchVisualStyle style = s->GetVisualStyle();
    style.trackOnColor = kAccent;
    style.trackOffColor = kTrack;
    style.trackBorderColor = Colors::Transparent;
    style.thumbBorderColor = Colors::Transparent;
    s->SetVisualStyle(style);
    Size(*s, 44, 26);
    return s;
}

} // namespace Kit
} // namespace SmartHome
} // namespace UltraCanvas
