// Apps/SmartHomeCenter/ui/SmartHomeCenterStyle.h
// SmartHome Center's look, shared by the window and its pages: one palette
// (a cool grey canvas, white cards, one accent), a warm colour per kind of
// "active" so a glance says what is on - amber for light and power, coral
// for heat, green for an open lock or blind, blue for a sensor reporting,
// red for an alarm - and the few building blocks every page is made of
// (rows and columns that lay out by flex, cards, labels, buttons, tables).
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "SmartHomeCenterModel.h"

#include "UltraCanvasBadge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasTextInput.h"

#include <cstdint>
#include <memory>
#include <string>

namespace SmartHomeCenter {

using UltraCanvas::Color;

// ===== PALETTE =====
inline const Color kCanvas(243, 245, 250);
inline const Color kSurface(255, 255, 255);
inline const Color kBorder(228, 232, 240);
inline const Color kInk(27, 33, 50);
inline const Color kMuted(107, 115, 133);
inline const Color kFaint(160, 167, 182);
inline const Color kAccent(69, 96, 230);
inline const Color kAccentSoft(232, 236, 253);
inline const Color kHover(240, 242, 247);

struct Tone { Color fg, soft, border; };
inline const Tone kIdle    {Color(120, 128, 146), Color(241, 243, 247), kBorder};
inline const Tone kLit     {Color(232, 150, 20),  Color(255, 246, 229), Color(249, 213, 140)};
inline const Tone kHeat    {Color(240, 100, 60),  Color(255, 240, 234), Color(250, 196, 176)};
inline const Tone kOpen    {Color(34, 160, 107),  Color(229, 246, 238), Color(160, 220, 190)};
inline const Tone kSense   {Color(46, 124, 246),  Color(232, 241, 253), Color(170, 200, 248)};
inline const Tone kAlarm   {Color(229, 72, 77),   Color(253, 236, 236), Color(246, 180, 182)};
inline const Tone kClimate {Color(46, 124, 246),  Color(236, 244, 255), kBorder};

// The colour a device's tile, badge and icon take from its state.
Tone ToneOf(const DeviceView& v);
// The status pill in the same colours: amber for light, heat and power,
// green for open, blue for a sensor reporting, red for an alarm.
UltraCanvas::BadgeVariant StatusVariant(const DeviceView& v);

// ===== TIME AND WORDS =====
int64_t NowSeconds();
std::string ClockText(int64_t seconds);          // "14:05:09", local
std::string DateTimeText(int64_t seconds);       // "21 Oct 14:05", local
std::string AgoText(uint64_t lastSeenMs);        // "3 min ago"
std::string SecurityText(UltraCanvas::SmartHome::SmartHomeSecurityLevel level);
std::string RoomText(const std::string& room);   // the name, or "Not in a room"

// ===== BUILDING BLOCKS =====
void NoScrollbars(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& c);
std::shared_ptr<UltraCanvas::UltraCanvasContainer> Column(const std::string& id, float gap);
std::shared_ptr<UltraCanvas::UltraCanvasContainer> Row(
    const std::string& id, float gap,
    UltraCanvas::CSSLayout::AlignItems align = UltraCanvas::CSSLayout::AlignItems::Center);
// A fixed size; -1 leaves that side to the layout.
void Fixed(const std::shared_ptr<UltraCanvas::UltraCanvasUIElement>& e, float w, float h);
// Takes what is left along the parent's direction, stretched across it.
void Grow(const std::shared_ptr<UltraCanvas::UltraCanvasUIElement>& e);
// A flexible gap that pushes what follows it to the far end of a row.
std::shared_ptr<UltraCanvas::UltraCanvasContainer> Spacer(const std::string& id);
// A column that scrolls when its content is taller than it.
std::shared_ptr<UltraCanvas::UltraCanvasContainer> ScrollColumn(const std::string& id, float gap);
void Card(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& c, float padding = 16.0f);
std::shared_ptr<UltraCanvas::UltraCanvasLabel> Text(const std::string& id, const std::string& text, float size,
                                                    const Color& color, bool bold = false, float height = -1);
// A label that wraps, `lines` lines high.
std::shared_ptr<UltraCanvas::UltraCanvasLabel> Paragraph(const std::string& id, const std::string& text,
                                                         float size, const Color& color, int lines);
// A section caption in small capitals: "CONTROLS".
std::shared_ptr<UltraCanvas::UltraCanvasLabel> Caption(const std::string& id, const std::string& text);

enum class ButtonKind { Primary, Secondary, Danger, Quiet };
std::shared_ptr<UltraCanvas::UltraCanvasButton> MakeButton(const std::string& id, const std::string& text,
                                                           ButtonKind kind, float height = 40);
std::shared_ptr<UltraCanvas::UltraCanvasSwitch> MakeSwitch(const std::string& id, bool on);
std::shared_ptr<UltraCanvas::UltraCanvasTextInput> MakeInput(const std::string& id, const std::string& text,
                                                             const std::string& placeholder);
// A list view in the window's ink and type sizes, a quiet header, hover rows.
void StyleTable(const std::shared_ptr<UltraCanvas::UltraCanvasListView>& list);

} // namespace SmartHomeCenter
