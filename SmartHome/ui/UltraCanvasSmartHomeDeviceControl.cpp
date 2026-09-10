// ui/UltraCanvasSmartHomeDeviceControl.cpp
// The per-device controls: light, thermostat, lock, blind, and the read-only
// sensor display.
//
// Each one drives its device through SmartHomeAPI and reports back through its
// own callback, so a host can either let the control talk to the module or
// intercept the change. Layout is computed from the element's local bounds
// rather than fixed pixel positions, so the same control works in a dashboard
// cell and in a full-width detail pane.
//
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHomeDeviceControl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {
namespace SmartHome {

namespace {

constexpr double kPad = 12.0;
constexpr double kRowH = 28.0;

const Color kText      (0x21, 0x21, 0x21);
const Color kTextDim   (0x75, 0x75, 0x75);
const Color kTrack     (0xE0, 0xE0, 0xE0);
const Color kAccent    (33, 150, 243);
const Color kPanelBg   (0xFA, 0xFA, 0xFA);

// A slider whose value runs left to right across `track`.
double ValueFromX(const Rect2Dd& track, int x, double lo, double hi) {
    if (track.width <= 0.0) return lo;
    const double t = std::clamp((x - track.x) / track.width, 0.0, 1.0);
    return lo + t * (hi - lo);
}

double XFromValue(const Rect2Dd& track, double value, double lo, double hi) {
    if (hi <= lo) return track.x;
    const double t = std::clamp((value - lo) / (hi - lo), 0.0, 1.0);
    return track.x + t * track.width;
}

void DrawSlider(IRenderContext* ctx, const Rect2Dd& track, double t, const Color& fill) {
    ctx->SetFillPaint(kTrack);
    ctx->FillRoundedRectangle(track, track.height / 2.0);
    ctx->SetFillPaint(fill);
    ctx->FillRoundedRectangle(Rect2Dd(track.x, track.y, track.width * std::clamp(t, 0.0, 1.0),
                                      track.height),
                              track.height / 2.0);
    // Handle, so the control reads as draggable rather than as a progress bar.
    const double hx = track.x + track.width * std::clamp(t, 0.0, 1.0);
    ctx->SetFillPaint(Colors::White);
    ctx->FillEllipse(Rect2Dd(hx - 7.0, track.y + track.height / 2.0 - 7.0, 14.0, 14.0));
    ctx->SetStrokePaint(kTrack);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawEllipse(Rect2Dd(hx - 7.0, track.y + track.height / 2.0 - 7.0, 14.0, 14.0));
}

std::string Fixed1(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

}  // namespace

// ============================================================================
// Light
// ============================================================================

SmartHomeLightControl::SmartHomeLightControl(const std::string& identifier,
                                             float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomeLightControl::~SmartHomeLightControl() = default;

void SmartHomeLightControl::SetDevice(const std::string& id) {
    deviceId = id;
    currentState = SMARTHOME_API.GetLightState(id);
    RequestRedraw();
}

void SmartHomeLightControl::SetState(const SmartHomeLightState& state) {
    currentState = state;
    RequestRedraw();
}

void SmartHomeLightControl::SetColorTempRange(uint16_t min, uint16_t max) {
    if (min > max) std::swap(min, max);
    colorTempMin = min;
    colorTempMax = max;
    RequestRedraw();
}

void SmartHomeLightControl::AddColorPreset(const std::string& name,
                                           uint8_t r, uint8_t g, uint8_t b) {
    colorPresets.push_back(ColorPreset{name, r, g, b});
    RequestRedraw();
}

void SmartHomeLightControl::SendState() {
    if (!deviceId.empty()) SMARTHOME_API.SetLightState(deviceId, currentState);
    if (onStateChange) onStateChange(currentState);
    RequestRedraw();
}

void SmartHomeLightControl::HandlePowerClick() {
    currentState.On = !currentState.On;
    SendState();
}

void SmartHomeLightControl::HandleBrightnessChange(int x) {
    const Rect2Df b = GetLocalBounds();
    const Rect2Dd track(kPad, kRowH + kPad, b.width - 2 * kPad, 6.0);
    currentState.Brightness = static_cast<uint8_t>(ValueFromX(track, x, 0.0, 255.0));
    // Dragging the brightness up from nothing is a request for light.
    if (currentState.Brightness > 0) currentState.On = true;
    SendState();
}

void SmartHomeLightControl::HandleColorTempChange(int x) {
    const Rect2Df b = GetLocalBounds();
    const Rect2Dd track(kPad, kRowH + kPad + 34.0, b.width - 2 * kPad, 6.0);
    currentState.ColorTemp = static_cast<uint16_t>(
        ValueFromX(track, x, colorTempMin, colorTempMax));
    SendState();
}

void SmartHomeLightControl::HandleColorWheelClick(int x, int y) {
    // The wheel is a disc: angle picks hue, distance from the centre picks
    // saturation, which is the usual mapping and needs no extra control.
    const Rect2Df b = GetLocalBounds();
    const double cx = b.width / 2.0;
    const double cy = kRowH + kPad + 80.0 + 60.0;
    const double dx = x - cx;
    const double dy = y - cy;
    const double radius = 60.0;
    const double dist = std::sqrt(dx * dx + dy * dy);
    if (dist > radius) return;

    double angle = std::atan2(dy, dx) * 180.0 / M_PI;
    if (angle < 0) angle += 360.0;

    currentState.Hue = static_cast<uint16_t>(angle);
    currentState.Saturation = static_cast<uint8_t>(std::clamp(dist / radius, 0.0, 1.0) * 100.0);
    currentState.On = true;
    SendState();
}

void SmartHomeLightControl::HandlePresetClick(int index) {
    if (index < 0 || index >= static_cast<int>(colorPresets.size())) return;
    const ColorPreset& p = colorPresets[static_cast<size_t>(index)];
    currentState.Red = p.R;
    currentState.Green = p.G;
    currentState.Blue = p.B;
    currentState.On = true;
    SendState();
}

void SmartHomeLightControl::RenderPowerButton(IRenderContext* ctx) {
    const Rect2Dd r(kPad, kPad - 4.0, 56.0, kRowH - 4.0);
    ctx->SetFillPaint(currentState.On ? kAccent : kTrack);
    ctx->FillRoundedRectangle(r, (kRowH - 4.0) / 2.0);
    ctx->SetTextPaint(currentState.On ? Colors::White : kTextDim);
    ctx->SetFontSize(11.0);
    ctx->DrawTextInRect(currentState.On ? "On" : "Off",
                        Rect2Dd(r.x + 14.0, r.y + 4.0, r.width - 20.0, 16.0));
}

void SmartHomeLightControl::RenderBrightnessSlider(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const Rect2Dd track(kPad, kRowH + kPad, b.width - 2 * kPad, 6.0);
    DrawSlider(ctx, track, currentState.Brightness / 255.0, kAccent);
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(10.0);
    ctx->DrawTextInRect(std::to_string(currentState.Brightness * 100 / 255) + "%",
                        Rect2Dd(track.x, track.y + 12.0, 60.0, 14.0));
}

void SmartHomeLightControl::RenderColorTempSlider(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const Rect2Dd track(kPad, kRowH + kPad + 34.0, b.width - 2 * kPad, 6.0);
    const double span = static_cast<double>(colorTempMax - colorTempMin);
    const double t = span > 0.0 ? (currentState.ColorTemp - colorTempMin) / span : 0.0;
    DrawSlider(ctx, track, t, Color(0xFF, 0xC1, 0x07));
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(10.0);
    ctx->DrawTextInRect(std::to_string(currentState.ColorTemp) + "K",
                        Rect2Dd(track.x, track.y + 12.0, 60.0, 14.0));
}

void SmartHomeLightControl::RenderColorWheel(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const double cy = kRowH + kPad + 80.0 + 60.0;
    ctx->SetFillPaint(Color(currentState.Red, currentState.Green, currentState.Blue));
    ctx->FillEllipse(Rect2Dd(b.width / 2.0 - 60.0, cy - 60.0, 120.0, 120.0));
    ctx->SetStrokePaint(kTrack);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawEllipse(Rect2Dd(b.width / 2.0 - 60.0, cy - 60.0, 120.0, 120.0));
}

void SmartHomeLightControl::RenderColorPresets(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    double x = kPad;
    const double y = b.height - kPad - 22.0;
    for (const auto& p : colorPresets) {
        ctx->SetFillPaint(Color(p.R, p.G, p.B));
        ctx->FillRoundedRectangle(Rect2Dd(x, y, 22.0, 22.0), 4.0);
        x += 28.0;
        if (x + 22.0 > b.width - kPad) break;
    }
}

void SmartHomeLightControl::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx || !IsVisible()) return;
    ctx->SetFillPaint(kPanelBg);
    ctx->FillRectangle(Rect2Dd(GetLocalBounds()));

    RenderPowerButton(ctx);
    if (brightnessEnabled) RenderBrightnessSlider(ctx);
    if (colorTempEnabled)  RenderColorTempSlider(ctx);
    if (colorEnabled) {
        RenderColorWheel(ctx);
        RenderColorPresets(ctx);
    }
}

bool SmartHomeLightControl::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    const int x = event.pointer.x;
    const int y = event.pointer.y;
    const Rect2Df b = GetLocalBounds();

    switch (event.type) {
        case UCEventType::MouseDown:
        case UCEventType::TouchStart: {
            if (!Contains(Point2Df(static_cast<float>(x), static_cast<float>(y)))) return false;

            if (y < kRowH + kPad - 4.0 && x < kPad + 56.0) { HandlePowerClick(); return true; }

            if (brightnessEnabled && y >= kRowH + kPad - 8.0 && y < kRowH + kPad + 20.0) {
                dragging = 1; HandleBrightnessChange(x); return true;
            }
            if (colorTempEnabled && y >= kRowH + kPad + 26.0 && y < kRowH + kPad + 54.0) {
                dragging = 2; HandleColorTempChange(x); return true;
            }
            if (colorEnabled && y >= b.height - kPad - 22.0) {
                HandlePresetClick(static_cast<int>((x - kPad) / 28.0)); return true;
            }
            if (colorEnabled) { dragging = 3; HandleColorWheelClick(x, y); return true; }
            return true;
        }

        case UCEventType::MouseMove:
        case UCEventType::TouchMove:
            if (dragging == 1) { HandleBrightnessChange(x); return true; }
            if (dragging == 2) { HandleColorTempChange(x); return true; }
            if (dragging == 3) { HandleColorWheelClick(x, y); return true; }
            return false;

        case UCEventType::MouseUp:
        case UCEventType::TouchEnd:
            if (dragging == 0) return false;
            dragging = 0;
            return true;

        default:
            return false;
    }
}

// ============================================================================
// Thermostat
// ============================================================================

SmartHomeThermostatControl::SmartHomeThermostatControl(const std::string& identifier,
                                                       float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomeThermostatControl::~SmartHomeThermostatControl() = default;

void SmartHomeThermostatControl::SetDevice(const std::string& id) {
    deviceId = id;
    currentState = SMARTHOME_API.GetThermostatState(id);
    RequestRedraw();
}

void SmartHomeThermostatControl::SetState(const SmartHomeThermostatState& state) {
    currentState = state;
    RequestRedraw();
}

void SmartHomeThermostatControl::SetTemperatureRange(float min, float max) {
    if (min > max) std::swap(min, max);
    tempMin = min;
    tempMax = max;
    RequestRedraw();
}

void SmartHomeThermostatControl::SetSupportedModes(const std::vector<std::string>& modes) {
    supportedModes = modes;
    RequestRedraw();
}

float SmartHomeThermostatControl::CelsiusToFahrenheit(float c) const { return c * 9.0f / 5.0f + 32.0f; }
float SmartHomeThermostatControl::FahrenheitToCelsius(float f) const { return (f - 32.0f) * 5.0f / 9.0f; }

void SmartHomeThermostatControl::SendState() {
    if (!deviceId.empty()) {
        SMARTHOME_API.SetThermostatTarget(deviceId, currentState.TargetTemperature);
        if (!currentState.Mode.empty()) {
            SMARTHOME_API.SetThermostatMode(deviceId, currentState.Mode);
        }
    }
    if (onStateChange) onStateChange(currentState);
    RequestRedraw();
}

void SmartHomeThermostatControl::AdjustTarget(float delta) {
    currentState.TargetTemperature =
        std::clamp(currentState.TargetTemperature + delta, tempMin, tempMax);
    SendState();
}

void SmartHomeThermostatControl::HandleModeSelect(const std::string& mode) {
    currentState.Mode = mode;
    SendState();
}

void SmartHomeThermostatControl::HandleDialDrag(int x, int y) {
    // Angle around the dial maps onto the setpoint range, starting at the
    // bottom so the coldest and hottest ends are not adjacent.
    const Rect2Df b = GetLocalBounds();
    const double cx = b.width / 2.0;
    const double cy = b.height / 2.0 - 10.0;
    double angle = std::atan2(y - cy, x - cx) * 180.0 / M_PI + 90.0;
    if (angle < 0) angle += 360.0;

    const double t = std::clamp(angle / 360.0, 0.0, 1.0);
    currentState.TargetTemperature =
        static_cast<float>(tempMin + t * (tempMax - tempMin));
    SendState();
}

void SmartHomeThermostatControl::RenderTemperatureDial(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const double radius = std::min(b.width, b.height) / 2.0 - 24.0;
    const double cx = b.width / 2.0;
    const double cy = b.height / 2.0 - 10.0;

    ctx->SetStrokePaint(kTrack);
    ctx->SetStrokeWidth(10.0);
    ctx->DrawEllipse(Rect2Dd(cx - radius, cy - radius, radius * 2, radius * 2));

    // Heating and cooling get different colours; a dial that is only ever blue
    // makes "is it heating right now" a guess.
    const bool heating = currentState.Mode == "heat";
    ctx->SetStrokePaint(currentState.Running
                            ? (heating ? Color(0xFF, 0x70, 0x43) : kAccent)
                            : kTrack);
    ctx->SetStrokeWidth(6.0);
    ctx->DrawEllipse(Rect2Dd(cx - radius + 6.0, cy - radius + 6.0,
                             (radius - 6.0) * 2, (radius - 6.0) * 2));
}

void SmartHomeThermostatControl::RenderCurrentTemperature(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const float shown = useCelsius ? currentState.CurrentTemperature
                                   : CelsiusToFahrenheit(currentState.CurrentTemperature);
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(11.0);
    ctx->DrawTextInRect(Fixed1(shown) + (useCelsius ? "°C now" : "°F now"),
                        Rect2Dd(0.0, b.height / 2.0 + 14.0, b.width, 16.0));
}

void SmartHomeThermostatControl::RenderTargetTemperature(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const float shown = useCelsius ? currentState.TargetTemperature
                                   : CelsiusToFahrenheit(currentState.TargetTemperature);
    ctx->SetTextPaint(kText);
    ctx->SetFontSize(26.0);
    ctx->DrawTextInRect(Fixed1(shown) + "°",
                        Rect2Dd(0.0, b.height / 2.0 - 26.0, b.width, 34.0));
}

void SmartHomeThermostatControl::RenderModeSelector(IRenderContext* ctx) {
    if (supportedModes.empty()) return;
    const Rect2Df b = GetLocalBounds();
    const double w = b.width / static_cast<double>(supportedModes.size());
    const double y = b.height - 30.0;
    for (size_t i = 0; i < supportedModes.size(); ++i) {
        const bool active = supportedModes[i] == currentState.Mode;
        ctx->SetFillPaint(active ? kAccent : kTrack);
        ctx->FillRoundedRectangle(Rect2Dd(i * w + 4.0, y, w - 8.0, 22.0), 11.0);
        ctx->SetTextPaint(active ? Colors::White : kTextDim);
        ctx->SetFontSize(10.0);
        ctx->DrawTextInRect(supportedModes[i], Rect2Dd(i * w + 10.0, y + 4.0, w - 20.0, 14.0));
    }
}

void SmartHomeThermostatControl::RenderHumidity(IRenderContext* ctx) {
    if (currentState.Humidity <= 0.0f) return;
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(10.0);
    ctx->DrawTextInRect(Fixed1(currentState.Humidity) + "% RH",
                        Rect2Dd(0.0, b.height / 2.0 + 32.0, b.width, 14.0));
}

void SmartHomeThermostatControl::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx || !IsVisible()) return;
    ctx->SetFillPaint(kPanelBg);
    ctx->FillRectangle(Rect2Dd(GetLocalBounds()));
    RenderTemperatureDial(ctx);
    RenderTargetTemperature(ctx);
    RenderCurrentTemperature(ctx);
    RenderHumidity(ctx);
    RenderModeSelector(ctx);
}

bool SmartHomeThermostatControl::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    const Rect2Df b = GetLocalBounds();
    const int x = event.pointer.x;
    const int y = event.pointer.y;

    switch (event.type) {
        case UCEventType::MouseDown:
        case UCEventType::TouchStart: {
            if (!Contains(Point2Df(static_cast<float>(x), static_cast<float>(y)))) return false;
            if (!supportedModes.empty() && y >= b.height - 30.0) {
                const double w = b.width / static_cast<double>(supportedModes.size());
                const size_t i = static_cast<size_t>(std::clamp<double>(x / w, 0.0,
                                     static_cast<double>(supportedModes.size() - 1)));
                HandleModeSelect(supportedModes[i]);
                return true;
            }
            draggingDial = true;
            HandleDialDrag(x, y);
            return true;
        }

        case UCEventType::MouseMove:
        case UCEventType::TouchMove:
            if (!draggingDial) return false;
            HandleDialDrag(x, y);
            return true;

        case UCEventType::MouseUp:
        case UCEventType::TouchEnd:
            if (!draggingDial) return false;
            draggingDial = false;
            return true;

        case UCEventType::MouseWheel:
            // Half a degree per notch: a full degree per notch overshoots on a
            // trackpad, which sends many small deltas.
            AdjustTarget(event.wheelDelta > 0 ? 0.5f : -0.5f);
            return true;

        default:
            return false;
    }
}

// ============================================================================
// Lock
// ============================================================================

SmartHomeLockControl::SmartHomeLockControl(const std::string& identifier,
                                           float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomeLockControl::~SmartHomeLockControl() = default;

void SmartHomeLockControl::SetDevice(const std::string& id) {
    deviceId = id;
    currentState = SMARTHOME_API.GetLockState(id);
    RequestRedraw();
}

void SmartHomeLockControl::SetState(const SmartHomeLockState& state) {
    currentState = state;
    RequestRedraw();
}

void SmartHomeLockControl::AddActivity(uint64_t timestamp, const std::string& action,
                                       const std::string& user) {
    activityLog.push_back(ActivityEntry{timestamp, action, user});
    // Newest first: the last thing that happened is what anyone opening a lock
    // panel wants to see.
    std::stable_sort(activityLog.begin(), activityLog.end(),
                     [](const ActivityEntry& a, const ActivityEntry& b) {
                         return a.Timestamp > b.Timestamp;
                     });
    RequestRedraw();
}

void SmartHomeLockControl::ClearActivity() {
    activityLog.clear();
    RequestRedraw();
}

void SmartHomeLockControl::AnimateLock(bool locking) {
    animating = true;
    animatingToLocked = locking;
    animationProgress = 0.0f;
    RequestRedraw();
}

void SmartHomeLockControl::HandleLockToggle() {
    // A jammed lock is not a state to toggle out of blindly; it needs a look.
    if (currentState.Jammed) return;

    const bool wantLocked = !currentState.Locked;
    AnimateLock(wantLocked);
    if (!deviceId.empty()) SMARTHOME_API.SetLockState(deviceId, wantLocked);
    currentState.Locked = wantLocked;
    if (wantLocked) { if (onLock) onLock(); }
    else            { if (onUnlock) onUnlock(); }
    RequestRedraw();
}

void SmartHomeLockControl::RenderLockButton(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const double d = 72.0;
    const Rect2Dd r(b.width / 2.0 - d / 2.0, kPad + 6.0, d, d);

    Color fill = currentState.Locked ? Color(76, 175, 80) : Color(0xFF, 0x98, 0x00);
    if (currentState.Jammed) fill = Color(0xF4, 0x43, 0x36);
    ctx->SetFillPaint(fill);
    ctx->FillEllipse(r);

    ctx->SetTextPaint(Colors::White);
    ctx->SetFontSize(12.0);
    ctx->DrawTextInRect(currentState.Jammed ? "Jammed"
                                            : (currentState.Locked ? "Locked" : "Unlocked"),
                        Rect2Dd(r.x, r.y + d / 2.0 - 8.0, d, 16.0));
}

void SmartHomeLockControl::RenderStatus(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(10.0);
    ctx->DrawTextInRect(currentState.LastUser.empty()
                            ? std::string("No recent user")
                            : "Last: " + currentState.LastUser,
                        Rect2Dd(kPad, kPad + 88.0, b.width - 2 * kPad, 14.0));
}

void SmartHomeLockControl::RenderBattery(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const double w = 40.0;
    const Rect2Dd r(b.width - kPad - w, kPad, w, 14.0);
    ctx->SetStrokePaint(kTrack);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRoundedRectangle(r, 3.0);

    const double level = std::clamp(currentState.BatteryLevel / 100.0, 0.0, 1.0);
    ctx->SetFillPaint(level < 0.2 ? Color(0xF4, 0x43, 0x36) : Color(76, 175, 80));
    ctx->FillRoundedRectangle(Rect2Dd(r.x + 2.0, r.y + 2.0, (r.width - 4.0) * level,
                                      r.height - 4.0), 2.0);
}

void SmartHomeLockControl::RenderActivityLog(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    double y = kPad + 110.0;
    ctx->SetFontSize(10.0);
    for (const auto& entry : activityLog) {
        if (y + 14.0 > b.height - kPad) break;   // clip rather than overflow
        ctx->SetTextPaint(kTextDim);
        ctx->DrawTextInRect(entry.User.empty() ? entry.Action
                                               : entry.Action + " — " + entry.User,
                            Rect2Dd(kPad, y, b.width - 2 * kPad, 14.0));
        y += 16.0;
    }
}

void SmartHomeLockControl::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx || !IsVisible()) return;
    ctx->SetFillPaint(kPanelBg);
    ctx->FillRectangle(Rect2Dd(GetLocalBounds()));
    RenderBattery(ctx);
    RenderLockButton(ctx);
    RenderStatus(ctx);
    RenderActivityLog(ctx);
}

bool SmartHomeLockControl::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    if (event.type != UCEventType::MouseUp && event.type != UCEventType::TouchEnd) {
        // Consume the press so the click does not fall through to whatever is
        // behind the panel, but act only on release.
        return (event.type == UCEventType::MouseDown ||
                event.type == UCEventType::TouchStart) &&
               Contains(Point2Df(static_cast<float>(event.pointer.x),
                                 static_cast<float>(event.pointer.y)));
    }

    const Rect2Df b = GetLocalBounds();
    const double d = 72.0;
    const Rect2Dd button(b.width / 2.0 - d / 2.0, kPad + 6.0, d, d);
    const double dx = event.pointer.x - (button.x + d / 2.0);
    const double dy = event.pointer.y - (button.y + d / 2.0);
    if (std::sqrt(dx * dx + dy * dy) <= d / 2.0) {
        HandleLockToggle();
        return true;
    }
    return false;
}

// ============================================================================
// Blind
// ============================================================================

SmartHomeBlindControl::SmartHomeBlindControl(const std::string& identifier,
                                             float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomeBlindControl::~SmartHomeBlindControl() = default;

void SmartHomeBlindControl::SetDevice(const std::string& id) {
    deviceId = id;
    RequestRedraw();
}

void SmartHomeBlindControl::SetPosition(uint8_t position) {
    currentPosition = std::min<uint8_t>(position, 100);
    RequestRedraw();
}

void SmartHomeBlindControl::SetTilt(uint8_t tilt) {
    currentTilt = std::min<uint8_t>(tilt, 100);
    RequestRedraw();
}

void SmartHomeBlindControl::AddPreset(const std::string& name, uint8_t position) {
    presets.push_back(BlindPreset{name, std::min<uint8_t>(position, 100)});
    RequestRedraw();
}

void SmartHomeBlindControl::HandlePositionChange(int y) {
    // The slider runs down the panel and 100 is fully open, so the top of the
    // track is open and the bottom is closed — the way a real blind moves.
    const Rect2Df b = GetLocalBounds();
    const double top = kPad + 90.0;
    const double height = b.height - top - 60.0;
    if (height <= 0.0) return;
    const double t = std::clamp((y - top) / height, 0.0, 1.0);
    currentPosition = static_cast<uint8_t>((1.0 - t) * 100.0);
    if (!deviceId.empty()) SMARTHOME_API.SetBlindPosition(deviceId, currentPosition);
    if (onPositionChange) onPositionChange(currentPosition);
    RequestRedraw();
}

void SmartHomeBlindControl::HandleTiltChange(int x) {
    if (!tiltEnabled) return;
    const Rect2Df b = GetLocalBounds();
    const Rect2Dd track(kPad, b.height - 44.0, b.width - 2 * kPad, 6.0);
    currentTilt = static_cast<uint8_t>(ValueFromX(track, x, 0.0, 100.0));
    if (onTiltChange) onTiltChange(currentTilt);
    RequestRedraw();
}

void SmartHomeBlindControl::HandlePresetClick(int index) {
    if (index < 0 || index >= static_cast<int>(presets.size())) return;
    SetPosition(presets[static_cast<size_t>(index)].Position);
    if (!deviceId.empty()) SMARTHOME_API.SetBlindPosition(deviceId, currentPosition);
    if (onPositionChange) onPositionChange(currentPosition);
}

void SmartHomeBlindControl::HandleQuickAction(const std::string& action) {
    if (action == "open")       SetPosition(100);
    else if (action == "close") SetPosition(0);
    else if (action == "stop")  { /* leave the position where it is */ }
    else return;

    if (!deviceId.empty()) SMARTHOME_API.SetBlindPosition(deviceId, currentPosition);
    if (onPositionChange) onPositionChange(currentPosition);
}

void SmartHomeBlindControl::RenderBlindPreview(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const Rect2Dd window(b.width / 2.0 - 45.0, kPad, 90.0, 70.0);

    ctx->SetFillPaint(Color(0xBB, 0xDE, 0xFB));   // the view outside
    ctx->FillRectangle(window);

    // The slats cover the top part: position 100 is open, so nothing covered.
    const double covered = window.height * (1.0 - currentPosition / 100.0);
    ctx->SetFillPaint(Color(0x90, 0xA4, 0xAE));
    ctx->FillRectangle(Rect2Dd(window.x, window.y, window.width, covered));

    ctx->SetStrokePaint(kTrack);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRectangle(window);
}

void SmartHomeBlindControl::RenderPositionSlider(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    const double top = kPad + 90.0;
    const double height = b.height - top - 60.0;
    if (height <= 0.0) return;

    const Rect2Dd track(b.width / 2.0 - 3.0, top, 6.0, height);
    ctx->SetFillPaint(kTrack);
    ctx->FillRoundedRectangle(track, 3.0);

    const double hy = top + height * (1.0 - currentPosition / 100.0);
    ctx->SetFillPaint(Colors::White);
    ctx->FillEllipse(Rect2Dd(b.width / 2.0 - 8.0, hy - 8.0, 16.0, 16.0));
    ctx->SetStrokePaint(kAccent);
    ctx->SetStrokeWidth(2.0);
    ctx->DrawEllipse(Rect2Dd(b.width / 2.0 - 8.0, hy - 8.0, 16.0, 16.0));

    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(10.0);
    ctx->DrawTextInRect(std::to_string(currentPosition) + "%",
                        Rect2Dd(b.width / 2.0 + 14.0, hy - 7.0, 50.0, 14.0));
}

void SmartHomeBlindControl::RenderTiltSlider(IRenderContext* ctx) {
    if (!tiltEnabled) return;
    const Rect2Df b = GetLocalBounds();
    const Rect2Dd track(kPad, b.height - 44.0, b.width - 2 * kPad, 6.0);
    DrawSlider(ctx, track, currentTilt / 100.0, kAccent);
}

void SmartHomeBlindControl::RenderPresets(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    double x = kPad;
    const double y = b.height - 22.0;
    ctx->SetFontSize(10.0);
    for (const auto& p : presets) {
        if (x + 54.0 > b.width - kPad) break;
        ctx->SetFillPaint(kTrack);
        ctx->FillRoundedRectangle(Rect2Dd(x, y, 50.0, 18.0), 9.0);
        ctx->SetTextPaint(kTextDim);
        ctx->DrawTextInRect(p.Name, Rect2Dd(x + 6.0, y + 2.0, 42.0, 14.0));
        x += 54.0;
    }
}

void SmartHomeBlindControl::RenderQuickButtons(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    static const char* kLabels[] = {"Open", "Stop", "Close"};
    const double w = (b.width - 2 * kPad) / 3.0;
    const double y = kPad + 68.0;
    ctx->SetFontSize(10.0);
    for (int i = 0; i < 3; ++i) {
        ctx->SetFillPaint(kTrack);
        ctx->FillRoundedRectangle(Rect2Dd(kPad + i * w + 2.0, y, w - 4.0, 18.0), 9.0);
        ctx->SetTextPaint(kTextDim);
        ctx->DrawTextInRect(kLabels[i], Rect2Dd(kPad + i * w + 8.0, y + 2.0, w - 16.0, 14.0));
    }
}

void SmartHomeBlindControl::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx || !IsVisible()) return;
    ctx->SetFillPaint(kPanelBg);
    ctx->FillRectangle(Rect2Dd(GetLocalBounds()));
    RenderBlindPreview(ctx);
    RenderQuickButtons(ctx);
    RenderPositionSlider(ctx);
    RenderTiltSlider(ctx);
    RenderPresets(ctx);
}

bool SmartHomeBlindControl::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    if (UltraCanvasUIElement::OnEvent(event)) return true;

    const Rect2Df b = GetLocalBounds();
    const int x = event.pointer.x;
    const int y = event.pointer.y;

    switch (event.type) {
        case UCEventType::MouseDown:
        case UCEventType::TouchStart: {
            if (!Contains(Point2Df(static_cast<float>(x), static_cast<float>(y)))) return false;

            if (y >= kPad + 68.0 && y < kPad + 86.0) {
                static const char* kActions[] = {"open", "stop", "close"};
                const double w = (b.width - 2 * kPad) / 3.0;
                const int i = static_cast<int>(std::clamp((x - kPad) / w, 0.0, 2.0));
                HandleQuickAction(kActions[i]);
                return true;
            }
            if (y >= b.height - 22.0) {
                HandlePresetClick(static_cast<int>((x - kPad) / 54.0));
                return true;
            }
            if (tiltEnabled && y >= b.height - 52.0 && y < b.height - 30.0) {
                dragging = 2; HandleTiltChange(x); return true;
            }
            dragging = 1;
            HandlePositionChange(y);
            return true;
        }

        case UCEventType::MouseMove:
        case UCEventType::TouchMove:
            if (dragging == 1) { HandlePositionChange(y); return true; }
            if (dragging == 2) { HandleTiltChange(x); return true; }
            return false;

        case UCEventType::MouseUp:
        case UCEventType::TouchEnd:
            if (dragging == 0) return false;
            dragging = 0;
            return true;

        default:
            return false;
    }
}

// ============================================================================
// Sensor display — read only
// ============================================================================

SmartHomeSensorDisplay::SmartHomeSensorDisplay(const std::string& identifier,
                                               float x, float y, float w, float h)
    : UltraCanvasUIElement(identifier, x, y, w, h) {}

SmartHomeSensorDisplay::~SmartHomeSensorDisplay() = default;

void SmartHomeSensorDisplay::SetDevice(const std::string& id) {
    deviceId = id;
    currentReading = SMARTHOME_API.GetSensorReading(id);
    sensorType = currentReading.Type;
    RequestRedraw();
}

void SmartHomeSensorDisplay::SetSensorType(SmartHomeSensorType type) {
    sensorType = type;
    RequestRedraw();
}

void SmartHomeSensorDisplay::AddReading(const SmartHomeSensorReading& reading) {
    history.push_back(reading);
    // Keep the window bounded: a sensor reporting every few seconds would grow
    // this without limit otherwise.
    constexpr size_t kMaxHistory = 2048;
    if (history.size() > kMaxHistory) {
        history.erase(history.begin(), history.begin() + (history.size() - kMaxHistory));
    }
    RequestRedraw();
}

void SmartHomeSensorDisplay::SetCurrentReading(const SmartHomeSensorReading& reading) {
    currentReading = reading;
    if (sensorType == SmartHomeSensorType::Unknown) sensorType = reading.Type;
    RequestRedraw();
}

void SmartHomeSensorDisplay::SetThresholds(float low, float high) {
    if (low > high) std::swap(low, high);
    lowThreshold = low;
    highThreshold = high;
    thresholdsSet = true;
    RequestRedraw();
}

void SmartHomeSensorDisplay::SetDisplayOptions(bool graph, bool unit, bool timestamp) {
    showGraph = graph;
    showUnit = unit;
    showTimestamp = timestamp;
    RequestRedraw();
}

std::string SmartHomeSensorDisplay::GetUnitString() const {
    if (!currentReading.Unit.empty()) return currentReading.Unit;
    switch (sensorType) {
        case SmartHomeSensorType::Temperature: return "°C";
        case SmartHomeSensorType::Humidity:    return "%";
        case SmartHomeSensorType::Pressure:    return "hPa";
        case SmartHomeSensorType::Light:       return "lx";
        case SmartHomeSensorType::AirQuality:  return "AQI";
        default:                               return "";
    }
}

std::string SmartHomeSensorDisplay::FormatValue(float value) const {
    // Binary sensors have no meaningful number; they are open or closed.
    switch (sensorType) {
        case SmartHomeSensorType::Motion:
        case SmartHomeSensorType::Contact:
        case SmartHomeSensorType::Smoke:
        case SmartHomeSensorType::Water:
        case SmartHomeSensorType::Occupancy:
        case SmartHomeSensorType::Vibration:
            return currentReading.Triggered ? "Triggered" : "Clear";
        default:
            return Fixed1(value);
    }
}

Color SmartHomeSensorDisplay::GetValueColor() const {
    if (currentReading.Triggered) return Color(0xF4, 0x43, 0x36);
    if (thresholdsSet && (currentReading.Value < lowThreshold ||
                          currentReading.Value > highThreshold)) {
        return Color(0xFF, 0x98, 0x00);
    }
    return kText;
}

void SmartHomeSensorDisplay::RenderCurrentValue(IRenderContext* ctx) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(GetValueColor());
    ctx->SetFontSize(28.0);
    ctx->DrawTextInRect(FormatValue(currentReading.Value) + (showUnit ? GetUnitString() : ""),
                        Rect2Dd(kPad, kPad, b.width - 2 * kPad, 34.0));
}

void SmartHomeSensorDisplay::RenderGraph(IRenderContext* ctx) {
    if (!showGraph || history.size() < 2) return;

    const Rect2Df b = GetLocalBounds();
    const Rect2Dd plot(kPad, kPad + 52.0, b.width - 2 * kPad, b.height - kPad - 74.0);
    if (plot.width <= 0.0 || plot.height <= 0.0) return;

    // Only the requested window, taken from the newest reading backwards.
    const uint64_t newest = history.back().Timestamp;
    const uint64_t window = static_cast<uint64_t>(graphHours) * 3600ull * 1000ull;

    float lo = history.back().Value, hi = lo;
    size_t first = history.size() - 1;
    for (size_t i = history.size(); i-- > 0;) {
        if (newest > window && history[i].Timestamp < newest - window) break;
        first = i;
        lo = std::min(lo, history[i].Value);
        hi = std::max(hi, history[i].Value);
    }
    if (history.size() - first < 2) return;

    // A flat series would divide by zero; give it a band to sit in.
    if (hi - lo < 0.001f) { hi = lo + 0.5f; lo -= 0.5f; }

    ctx->SetStrokePaint(kAccent);
    ctx->SetStrokeWidth(1.5);
    const size_t count = history.size() - first;
    for (size_t i = first + 1; i < history.size(); ++i) {
        const double t0 = static_cast<double>(i - 1 - first) / static_cast<double>(count - 1);
        const double t1 = static_cast<double>(i - first)     / static_cast<double>(count - 1);
        const double y0 = plot.y + plot.height * (1.0 - (history[i - 1].Value - lo) / (hi - lo));
        const double y1 = plot.y + plot.height * (1.0 - (history[i].Value - lo) / (hi - lo));
        ctx->DrawLine(Point2Dd(plot.x + plot.width * t0, y0),
                      Point2Dd(plot.x + plot.width * t1, y1));
    }

    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(9.0);
    ctx->DrawTextInRect("min " + Fixed1(lo) + "   max " + Fixed1(hi),
                        Rect2Dd(plot.x, plot.y + plot.height + 2.0, plot.width, 12.0));
}

void SmartHomeSensorDisplay::RenderThresholds(IRenderContext* ctx) {
    if (!thresholdsSet) return;
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(9.0);
    ctx->DrawTextInRect("alert below " + Fixed1(lowThreshold) +
                            " or above " + Fixed1(highThreshold),
                        Rect2Dd(kPad, b.height - kPad - 12.0, b.width - 2 * kPad, 12.0));
}

void SmartHomeSensorDisplay::RenderStatus(IRenderContext* ctx) {
    if (!showTimestamp || currentReading.Timestamp == 0) return;
    const Rect2Df b = GetLocalBounds();
    ctx->SetTextPaint(kTextDim);
    ctx->SetFontSize(9.0);
    ctx->DrawTextInRect(SensorTypeToString(sensorType),
                        Rect2Dd(kPad, kPad + 38.0, b.width - 2 * kPad, 12.0));
}

void SmartHomeSensorDisplay::Render(IRenderContext* ctx, const Rect2Df&) {
    if (!ctx || !IsVisible()) return;
    ctx->SetFillPaint(kPanelBg);
    ctx->FillRectangle(Rect2Dd(GetLocalBounds()));
    RenderCurrentValue(ctx);
    RenderStatus(ctx);
    RenderGraph(ctx);
    RenderThresholds(ctx);
}

bool SmartHomeSensorDisplay::OnEvent(const UCEvent& event) {
    // A sensor readout has nothing to click; let events pass to whatever is
    // behind it rather than swallowing them.
    return UltraCanvasUIElement::OnEvent(event);
}

// ===== FACTORIES =====

std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeLightControlElement() {
    return std::make_shared<SmartHomeLightControl>("SmartHomeLightControl", 0, 0, 260, 320);
}
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeThermostatControlElement() {
    return std::make_shared<SmartHomeThermostatControl>("SmartHomeThermostatControl", 0, 0, 240, 260);
}
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeLockControlElement() {
    return std::make_shared<SmartHomeLockControl>("SmartHomeLockControl", 0, 0, 240, 260);
}
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeBlindControlElement() {
    return std::make_shared<SmartHomeBlindControl>("SmartHomeBlindControl", 0, 0, 220, 320);
}
std::shared_ptr<UltraCanvasUIElement> CreateSmartHomeSensorDisplayElement() {
    return std::make_shared<SmartHomeSensorDisplay>("SmartHomeSensorDisplay", 0, 0, 240, 180);
}

}  // namespace SmartHome
}  // namespace UltraCanvas
