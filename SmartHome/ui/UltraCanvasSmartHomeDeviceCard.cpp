// ui/UltraCanvasSmartHomeDeviceCard.cpp
// The two card widgets of the Smart Home dashboard: one device, one scene.
//
// These are the first Smart Home widgets with a body rather than only a
// declaration, and they exist to prove the ported widget API against the real
// framework: an UltraCanvasUIElement subclass, Render(ctx, dirtyRect), and a
// single OnEvent(UCEvent) in place of the per-event virtuals the original
// headers assumed. The remaining widgets follow this shape.
//
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHomePanel.h"

#include <chrono>

namespace UltraCanvas {
namespace SmartHome {

namespace {

uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// A press held this long is a long press rather than a click; the dashboard
// uses it to open device settings without a second control on the card.
constexpr uint64_t kLongPressMs = 500;

// Category colours, as the module README describes them: lights amber, locks
// green, sensors blue, everything else the neutral accent.
Color CategoryColor(SmartHomeDeviceCategory category) {
    switch (category) {
        case SmartHomeDeviceCategory::Light:      return Color(255, 193, 7);
        case SmartHomeDeviceCategory::Lock:       return Color(76, 175, 80);
        case SmartHomeDeviceCategory::Sensor:     return Color(33, 150, 243);
        case SmartHomeDeviceCategory::Thermostat: return Color(255, 112, 67);
        case SmartHomeDeviceCategory::Camera:     return Color(156, 39, 176);
        case SmartHomeDeviceCategory::Blind:      return Color(121, 85, 72);
        default:                                  return Color(96, 125, 139);
    }
}

}  // namespace

// ===== SmartHomeDeviceCard =====

SmartHomeDeviceCard::SmartHomeDeviceCard(const std::string& identifier,
                                         float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomeDeviceCard::~SmartHomeDeviceCard() = default;

void SmartHomeDeviceCard::SetDevice(const SmartHomeDeviceInfo& d) {
    device = d;
    RequestRedraw();
}

SmartHomeDeviceInfo SmartHomeDeviceCard::GetDevice() const { return device; }

void SmartHomeDeviceCard::SetSelected(bool value) {
    if (selected == value) return;
    selected = value;
    RequestRedraw();
}

bool SmartHomeDeviceCard::IsSelected() const { return selected; }

void SmartHomeDeviceCard::SetCompact(bool value)    { compact = value;    RequestRedraw(); }
void SmartHomeDeviceCard::SetShowName(bool value)   { showName = value;   RequestRedraw(); }
void SmartHomeDeviceCard::SetShowStatus(bool value) { showStatus = value; RequestRedraw(); }

void SmartHomeDeviceCard::AnimateState(bool newState) {
    animatingState = true;
    animationProgress = 0.0f;
    if (onToggle) onToggle(newState);
    RequestRedraw();
}

void SmartHomeDeviceCard::UpdateState() { RequestRedraw(); }

Color SmartHomeDeviceCard::GetStateColor() const {
    switch (device.State) {
        case SmartHomeDeviceState::Online:  return Color(76, 175, 80);
        case SmartHomeDeviceState::Offline: return Color(158, 158, 158);
        case SmartHomeDeviceState::Error:   return Color(244, 67, 54);
        case SmartHomeDeviceState::Pairing:
        case SmartHomeDeviceState::Updating: return Color(255, 152, 0);
        default:                             return Color(158, 158, 158);
    }
}

std::string SmartHomeDeviceCard::GetDeviceIcon() const {
    return DeviceCategoryToString(device.Category);
}

void SmartHomeDeviceCard::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!ctx || !IsVisible()) return;

    const Rect2Df bounds = GetLocalBounds();
    const double radius = 8.0;

    // Card body. A pressed card darkens rather than moving, so the grid does
    // not reflow under the finger.
    ctx->SetFillPaint(pressed ? Color(0xEE, 0xEE, 0xEE) : Colors::White);
    ctx->FillRoundedRectangle(Rect2Dd(bounds), radius);

    // Selection reads as a border, so it survives any card background.
    if (selected) {
        ctx->SetStrokePaint(Color(33, 150, 243));
        ctx->SetStrokeWidth(2.0);
        ctx->DrawRoundedRectangle(Rect2Dd(bounds), radius);
    }

    // Category swatch.
    const double pad = compact ? 6.0 : 10.0;
    const double swatch = compact ? 14.0 : 22.0;
    ctx->SetFillPaint(CategoryColor(device.Category));
    ctx->FillEllipse(Rect2Dd(bounds.x + pad, bounds.y + pad, swatch, swatch));

    // Online/offline dot, top right.
    if (showStatus) {
        const double dot = 8.0;
        ctx->SetFillPaint(GetStateColor());
        ctx->FillEllipse(Rect2Dd(bounds.x + bounds.width - pad - dot,
                                 bounds.y + pad, dot, dot));
    }

    if (showName && !compact) {
        ctx->SetTextPaint(Color(0x21, 0x21, 0x21));
        ctx->SetFontSize(12.0);
        ctx->DrawTextInRect(device.Name,
                            Rect2Dd(bounds.x + pad,
                                    bounds.y + pad + swatch + 6.0,
                                    bounds.width - 2 * pad,
                                    16.0));

        // Battery, for the wireless devices that report one.
        if (device.Category == SmartHomeDeviceCategory::Lock ||
            device.Category == SmartHomeDeviceCategory::Sensor) {
            ctx->SetTextPaint(Color(0x75, 0x75, 0x75));
            ctx->SetFontSize(10.0);
            ctx->DrawTextInRect(DeviceStateToString(device.State),
                                Rect2Dd(bounds.x + pad,
                                        bounds.y + bounds.height - pad - 12.0,
                                        bounds.width - 2 * pad, 12.0));
        }
    }
}

bool SmartHomeDeviceCard::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    // Mouse and touch arrive through the same fields, so one path serves both.
    switch (event.type) {
        case UCEventType::MouseDown:
        case UCEventType::TouchStart: {
            if (!Contains(Point2Df(static_cast<float>(event.pointer.x),
                                   static_cast<float>(event.pointer.y)))) {
                return false;
            }
            pressed = true;
            pressStartTime = NowMs();
            RequestRedraw();
            return true;
        }

        case UCEventType::MouseUp:
        case UCEventType::TouchEnd: {
            if (!pressed) return false;
            pressed = false;
            RequestRedraw();

            const bool inside = Contains(Point2Df(static_cast<float>(event.pointer.x),
                                                  static_cast<float>(event.pointer.y)));
            if (!inside) return true;   // released off the card: cancel, not click

            if (NowMs() - pressStartTime >= kLongPressMs) {
                if (onLongPress) onLongPress();
            } else if (onClick) {
                onClick();
            }
            return true;
        }

        case UCEventType::MouseLeave:
            if (pressed) { pressed = false; RequestRedraw(); }
            return false;

        default:
            return false;
    }
}

// ===== SmartHomeSceneCard =====

SmartHomeSceneCard::SmartHomeSceneCard(const std::string& identifier,
                                       float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomeSceneCard::~SmartHomeSceneCard() = default;

void SmartHomeSceneCard::SetScene(const SmartHomeScene& s) {
    scene = s;
    RequestRedraw();
}

SmartHomeScene SmartHomeSceneCard::GetScene() const { return scene; }

void SmartHomeSceneCard::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!ctx || !IsVisible()) return;

    const Rect2Df bounds = GetLocalBounds();
    const double radius = 8.0;

    // An active scene is filled, an inactive one outlined: the state is legible
    // without reading the label.
    if (scene.Active) {
        ctx->SetFillPaint(Color(33, 150, 243));
        ctx->FillRoundedRectangle(Rect2Dd(bounds), radius);
        ctx->SetTextPaint(Colors::White);
    } else {
        ctx->SetFillPaint(pressed ? Color(0xEE, 0xEE, 0xEE) : Colors::White);
        ctx->FillRoundedRectangle(Rect2Dd(bounds), radius);
        ctx->SetStrokePaint(Color(0xE0, 0xE0, 0xE0));
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRoundedRectangle(Rect2Dd(bounds), radius);
        ctx->SetTextPaint(Color(0x21, 0x21, 0x21));
    }

    ctx->SetFontSize(12.0);
    ctx->DrawTextInRect(scene.Name,
                        Rect2Dd(bounds.x + 10.0,
                                bounds.y + bounds.height / 2.0 - 8.0,
                                bounds.width - 20.0, 16.0));
}

bool SmartHomeSceneCard::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    const Point2Df at(static_cast<float>(event.pointer.x),
                      static_cast<float>(event.pointer.y));

    switch (event.type) {
        case UCEventType::MouseDown:
        case UCEventType::TouchStart:
            if (!Contains(at)) return false;
            pressed = true;
            RequestRedraw();
            return true;

        case UCEventType::MouseUp:
        case UCEventType::TouchEnd: {
            if (!pressed) return false;
            pressed = false;
            RequestRedraw();
            if (Contains(at) && onActivate) onActivate();
            return true;
        }

        default:
            return false;
    }
}

}  // namespace SmartHome
}  // namespace UltraCanvas
