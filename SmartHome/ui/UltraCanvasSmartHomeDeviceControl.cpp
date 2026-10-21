// ui/UltraCanvasSmartHomeDeviceControl.cpp
// The per-device controls: light, thermostat, lock, blind, and the read-only
// sensor display.
//
// Each control is a container of framework elements laid out by flex: what
// takes input is a switch, a slider, a button, a spinner, a segmented
// control, a colour picker or swatch bar, so focus, keyboard, hover and
// theming are the framework's. Each drives its device through SmartHomeAPI
// and reports back through its own callback, so a host can either let the
// control talk to the module or intercept the change. Two pictures take no
// input: a blind in its window is a small view element of its own, and a
// sensor's recent history is a line chart.
//
// SetState / SetPosition / SetTilt put a state into the elements without
// sending it back to the device (the `syncing` guard): a host that mirrors
// the device into the control must not get its own update echoed as a
// command.
//
// Version: 2.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasSmartHomeDeviceControl.h"
#include "SmartHomeWidgetKit.h"

#include "UltraCanvasListModel.h"
#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace UltraCanvas {
namespace SmartHome {

using namespace Kit;

namespace {

std::string Fixed1(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", v);   // locale-ok: display text
    return buf;
}

std::string Percent(int pct) { return std::to_string(pct) + " %"; }

// The control's own column: a light panel background, padding, no
// scrollbars unless `scroll` - a light with every feature is taller than
// its default cell.
void AsControl(UltraCanvasContainer& c, float gap, bool scroll = false) {
    AsColumn(c, gap, 12.0f);
    c.SetBackgroundColor(kPanelBg);
    if (scroll) {
        ContainerStyle style;
        style.autoShowScrollbars = true;
        style.autoShowHorizontalScrollbar = false;
        c.SetContainerStyle(style);
    }
}

// "Brightness ........ 40 %" over its slider.
std::shared_ptr<UltraCanvasContainer> CaptionRow(const std::string& id, const std::string& caption,
                                                 std::shared_ptr<UltraCanvasLabel>& value) {
    auto row = Row(id, 6, 18);
    auto label = Label(id + "Caption", caption, 11, kTextDim);
    Grow(*label);
    value = Label(id + "Value", "", 11, kText, true);
    value->SetAlignment(TextAlignment::Right);
    Size(*value, 70, 18);
    row->AddChild(label);
    row->AddChild(value);
    return row;
}

}  // namespace

// ============================================================================
// Light
// ============================================================================

SmartHomeLightControl::SmartHomeLightControl(const std::string& identifier,
                                             float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    AsControl(*this, 0, true);
    // The rows sit in a column of their own: a scrolling container lays its
    // children out across its full width but clips them a scrollbar short,
    // so the column keeps that strip free and nothing is cut at the right.
    auto body = CreateContainer(identifier + "_Body");
    AsColumn(*body, 8, 0);
    body->SetPadding(0, GetContainerStyle().scrollbarStyle.trackSize, 0, 0);
    body->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    body->layoutItem.SetFlexShrink(0);
    AddChild(body);

    auto top = Row(identifier + "_Power", 10, 28);
    powerSwitch = Kit::Switch(identifier + "_Switch", false);
    powerSwitch->onStateChanged = [this](CheckedState, CheckedState now) {
        if (syncing) return;
        currentState.On = now == CheckedState::Checked;
        SendState();
    };
    powerLabel = Label(identifier + "_PowerLabel", "Off", 13, kText, true);
    Grow(*powerLabel);
    top->AddChild(powerSwitch);
    top->AddChild(powerLabel);
    body->AddChild(top);

    brightnessRow = CreateContainer(identifier + "_BrightnessRow");
    AsColumn(*brightnessRow, 2, 0);
    Height(*brightnessRow, 44);
    brightnessRow->AddChild(CaptionRow(identifier + "_Brightness", "Brightness", brightnessValue));
    brightness = Kit::Slider(identifier + "_BrightnessSlider", 0, 100, kAccent);
    brightness->onValueChanging = [this](float pct) {
        brightnessValue->SetText(Percent(static_cast<int>(std::lround(pct))));
    };
    brightness->onValueChanged = [this](float pct) {
        brightnessValue->SetText(Percent(static_cast<int>(std::lround(pct))));
        if (syncing) return;
        currentState.Brightness = static_cast<uint8_t>(std::lround(pct * 255.0f / 100.0f));
        // Turning the brightness up from nothing is a request for light.
        if (currentState.Brightness > 0) currentState.On = true;
        SendState();
    };
    brightnessRow->AddChild(brightness);
    body->AddChild(brightnessRow);

    colorTempRow = CreateContainer(identifier + "_ColorTempRow");
    AsColumn(*colorTempRow, 2, 0);
    Height(*colorTempRow, 44);
    colorTempRow->AddChild(CaptionRow(identifier + "_ColorTemp", "Colour temperature", colorTempValue));
    colorTemp = Kit::Slider(identifier + "_ColorTempSlider", colorTempMin, colorTempMax, Color(0xFF, 0xC1, 0x07));
    colorTemp->SetStep(50);
    colorTemp->SetTrackGradient({GradientStop(0.0, Color(255, 176, 80)),
                                 GradientStop(0.5, Color(255, 236, 210)),
                                 GradientStop(1.0, Color(200, 222, 255))});
    colorTemp->onValueChanging = [this](float k) {
        colorTempValue->SetText(std::to_string(static_cast<int>(std::lround(k))) + " K");
    };
    colorTemp->onValueChanged = [this](float k) {
        colorTempValue->SetText(std::to_string(static_cast<int>(std::lround(k))) + " K");
        if (syncing) return;
        currentState.ColorTemp = static_cast<uint16_t>(std::lround(k));
        SendState();
    };
    colorTempRow->AddChild(colorTemp);
    body->AddChild(colorTempRow);

    // Hue and intensity as two bars: the compact design, and the one that
    // maps onto what a bulb can do.
    colorPicker = CreateColorPicker(identifier + "_ColorPicker", 0, 0, 236, 120);
    colorPicker->SetStyle(ColorPickerStyle::Light());
    colorPicker->SetWheelStyle(ColorPickerWheelStyle::HueLightnessSliders);
    colorPicker->SetShowAlpha(false);
    colorPicker->SetShowBackgroundColor(false);
    colorPicker->SetSlidersCollapsible(true, false);
    colorPicker->SetAutoHeight(true);
    colorPicker->layoutItem.SetFlexShrink(0);
    colorPicker->onColorChanged = [this](const Color& c) {
        if (!syncing) PickColor(c);
    };
    body->AddChild(colorPicker);

    presetBar = CreateColorSwatchBar(identifier + "_Presets", 0, 0, 236, 26, {});
    presetBar->SetWrap(true);
    Height(*presetBar, 26);
    presetBar->onColorSelected = [this](const Color& c) {
        PickColor(c);
        syncing = true;
        colorPicker->SetColor(c, false);
        syncing = false;
    };
    presetBar->SetVisible(false);   // until there is a preset
    body->AddChild(presetBar);

    Sync();
}

SmartHomeLightControl::~SmartHomeLightControl() = default;

void SmartHomeLightControl::SetDevice(const std::string& id) {
    deviceId = id;
    currentState = SMARTHOME_API.GetLightState(id);
    Sync();
}

void SmartHomeLightControl::SetState(const SmartHomeLightState& state) {
    currentState = state;
    Sync();
}

void SmartHomeLightControl::SetBrightnessEnabled(bool enable) { brightnessRow->SetVisible(enable); }
void SmartHomeLightControl::SetColorTempEnabled(bool enable) { colorTempRow->SetVisible(enable); }

void SmartHomeLightControl::SetColorEnabled(bool enable) {
    colorPicker->SetVisible(enable);
    presetBar->SetVisible(enable && !colorPresets.empty());
}

void SmartHomeLightControl::SetColorTempRange(uint16_t min, uint16_t max) {
    if (min > max) std::swap(min, max);
    colorTempMin = min;
    colorTempMax = max;
    syncing = true;
    colorTemp->SetRange(min, max);
    syncing = false;
    Sync();
}

void SmartHomeLightControl::AddColorPreset(const std::string& name,
                                           uint8_t r, uint8_t g, uint8_t b) {
    colorPresets.push_back(ColorPreset{name, r, g, b});
    std::vector<Color> colors;
    for (const auto& p : colorPresets) colors.emplace_back(p.R, p.G, p.B);
    presetBar->SetColors(colors);
    presetBar->SetVisible(colorPicker->IsVisible());
}

void SmartHomeLightControl::PickColor(const Color& c) {
    currentState.Red = c.r;
    currentState.Green = c.g;
    currentState.Blue = c.b;
    currentState.On = true;
    SendState();
}

void SmartHomeLightControl::Sync() {
    syncing = true;
    powerSwitch->SetChecked(currentState.On);
    powerLabel->SetText(currentState.On ? "On" : "Off");
    const int pct = static_cast<int>(std::lround(currentState.Brightness * 100.0 / 255.0));
    if (!brightness->IsDragging()) brightness->SetValue(static_cast<float>(pct));
    brightnessValue->SetText(Percent(pct));
    if (currentState.ColorTemp > 0) {
        if (!colorTemp->IsDragging()) colorTemp->SetValue(currentState.ColorTemp);
        colorTempValue->SetText(std::to_string(currentState.ColorTemp) + " K");
    } else {
        colorTempValue->SetText("-");
    }
    if (currentState.Red || currentState.Green || currentState.Blue) {
        colorPicker->SetColor(Color(currentState.Red, currentState.Green, currentState.Blue), false);
    }
    syncing = false;
    RequestRedraw();
}

void SmartHomeLightControl::SendState() {
    powerLabel->SetText(currentState.On ? "On" : "Off");
    if (!deviceId.empty()) SMARTHOME_API.SetLightState(deviceId, currentState);
    if (onStateChange) onStateChange(currentState);
    RequestRedraw();
}

// ============================================================================
// Thermostat
// ============================================================================

SmartHomeThermostatControl::SmartHomeThermostatControl(const std::string& identifier,
                                                       float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    AsControl(*this, 8);

    // The target in a ring, coloured by what the thermostat is doing.
    gauge = CreateGaugeDiagramElement(identifier + "_Gauge", 0, 0, 216, 110);
    gauge->SetMode(GaugeMode::CircularRing);
    gauge->SetRingThickness(10);
    gauge->SetMinValue(tempMin);
    gauge->SetMaxValue(tempMax);
    gauge->SetDecimalPlaces(1);
    gauge->SetUnit("°C");
    gauge->SetRingCenterContent(GaugeRingCenterContent::TextLabel);
    gauge->SetRingCenterLabel("target");
    gauge->SetTrackColor(kTrack);
    gauge->SetBackgroundColor(kPanelBg);
    gauge->SetTextColor(kText);
    gauge->layoutItem.SetFlexGrow(1);
    gauge->layoutItem.SetFlexShrink(1);
    gauge->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(80));
    AddChild(gauge);

    nowLabel = Label(identifier + "_Now", "", 11, kTextDim);
    nowLabel->SetAlignment(TextAlignment::Center);
    AddChild(nowLabel);

    auto row = Row(identifier + "_TargetRow", 8, 30);
    auto caption = Label(identifier + "_TargetCaption", "Target", 12, kTextDim);
    Grow(*caption);
    target = CreateDecimalSpinner(identifier + "_Target", 0, 0, 110, 30, tempMin, tempMax, 21.0, 0.5, 1);
    target->SetSuffix(" °C");
    Size(*target, 110, 30);
    target->onValueChanged = [this](double v) {
        if (syncing) return;
        currentState.TargetTemperature = useCelsius ? static_cast<float>(v)
                                                    : FahrenheitToCelsius(static_cast<float>(v));
        SendState();
    };
    row->AddChild(caption);
    row->AddChild(target);
    AddChild(row);

    modes = CreateSegmentedControl(identifier + "_Modes", 0, 0, 216, 26);
    SegmentedControlStyle modeStyle;
    modeStyle.fontSize = 10.0f;
    modeStyle.selectedColor = kAccent;
    modes->SetStyle(modeStyle);
    Height(*modes, 26);
    modes->onSegmentSelected = [this](int index) {
        if (syncing || index < 0 || index >= static_cast<int>(supportedModes.size())) return;
        currentState.Mode = supportedModes[static_cast<size_t>(index)];
        SendState();
    };
    modes->SetVisible(false);   // until the modes are known
    AddChild(modes);

    Sync();
}

SmartHomeThermostatControl::~SmartHomeThermostatControl() = default;

void SmartHomeThermostatControl::SetDevice(const std::string& id) {
    deviceId = id;
    currentState = SMARTHOME_API.GetThermostatState(id);
    Sync();
}

void SmartHomeThermostatControl::SetState(const SmartHomeThermostatState& state) {
    currentState = state;
    Sync();
}

void SmartHomeThermostatControl::SetCelsius(bool celsius) {
    useCelsius = celsius;
    Sync();
}

void SmartHomeThermostatControl::SetTemperatureRange(float min, float max) {
    if (min > max) std::swap(min, max);
    tempMin = min;
    tempMax = max;
    Sync();
}

void SmartHomeThermostatControl::SetSupportedModes(const std::vector<std::string>& list) {
    supportedModes = list;
    syncing = true;
    modes->ClearSegments();
    for (const auto& m : supportedModes) {
        std::string label = m;
        if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        modes->AddSegment(label);
    }
    syncing = false;
    modes->SetVisible(!supportedModes.empty());
    Sync();
}

float SmartHomeThermostatControl::CelsiusToFahrenheit(float c) const { return c * 9.0f / 5.0f + 32.0f; }
float SmartHomeThermostatControl::FahrenheitToCelsius(float f) const { return (f - 32.0f) * 5.0f / 9.0f; }

void SmartHomeThermostatControl::Sync() {
    syncing = true;
    auto shown = [this](float c) { return useCelsius ? c : CelsiusToFahrenheit(c); };
    const std::string unit = useCelsius ? "°C" : "°F";

    gauge->SetMinValue(shown(tempMin));
    gauge->SetMaxValue(shown(tempMax));
    gauge->SetUnit(unit);
    gauge->SetValue(shown(currentState.TargetTemperature));
    // Heating and cooling in different colours; idle in grey, so "is it
    // running right now" is not a guess.
    gauge->SetGaugeColor(!currentState.Running ? Color(0x90, 0xA4, 0xAE)
                         : currentState.Mode == "cool" ? kAccent : Color(0xFF, 0x70, 0x43));

    std::string now = Fixed1(shown(currentState.CurrentTemperature)) + " " + unit + " now";
    if (currentState.Humidity > 0.0f) now += "  ·  " + Fixed1(currentState.Humidity) + " % RH";
    nowLabel->SetText(now);

    target->SetRange(shown(tempMin), shown(tempMax));
    target->SetSuffix(" " + unit);
    target->SetValue(shown(currentState.TargetTemperature));

    int index = -1;
    for (size_t i = 0; i < supportedModes.size(); ++i) {
        if (supportedModes[i] == currentState.Mode) index = static_cast<int>(i);
    }
    if (index >= 0) modes->SetSelectedIndex(index);
    syncing = false;
    RequestRedraw();
}

void SmartHomeThermostatControl::SendState() {
    if (!deviceId.empty()) {
        SMARTHOME_API.SetThermostatTarget(deviceId, currentState.TargetTemperature);
        if (!currentState.Mode.empty()) {
            SMARTHOME_API.SetThermostatMode(deviceId, currentState.Mode);
        }
    }
    if (onStateChange) onStateChange(currentState);
    Sync();
}

void SmartHomeThermostatControl::AdjustTarget(float delta) {
    currentState.TargetTemperature =
        std::clamp(currentState.TargetTemperature + delta, tempMin, tempMax);
    SendState();
}

bool SmartHomeThermostatControl::OnEvent(const UCEvent& event) {
    if (UltraCanvasContainer::OnEvent(event)) return true;
    if (!IsVisible() || IsDisabled()) return false;
    if (event.type == UCEventType::MouseWheel) {
        // Half a degree per notch: a full degree per notch overshoots on a
        // trackpad, which sends many small deltas.
        AdjustTarget(event.wheelDelta > 0 ? 0.5f : -0.5f);
        return true;
    }
    return false;
}

// ============================================================================
// Lock
// ============================================================================

// The activity, newest first, one line each: "Unlocked - Anna  14:02".
class SmartHomeLockControl::ActivityModel : public IListModel {
public:
    explicit ActivityModel(const std::vector<ActivityEntry>& log) : entries(log) {}
    void Changed() { NotifyDataChanged(); }
    int GetRowCount() const override { return static_cast<int>(entries.size()); }
    int GetColumnCount() const override { return 1; }
    ListDataValue GetData(const ListIndex& index, ListDataRole role) const override {
        if (role != ListDataRole::DisplayRole || index.row < 0 || index.row >= GetRowCount()) return {};
        const ActivityEntry& e = entries[static_cast<size_t>(index.row)];
        return e.User.empty() ? e.Action : e.Action + " - " + e.User;
    }
    bool SetData(const ListIndex&, ListDataRole, const ListDataValue&) override { return false; }

private:
    const std::vector<ActivityEntry>& entries;
};

SmartHomeLockControl::SmartHomeLockControl(const std::string& identifier,
                                           float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    AsControl(*this, 8);

    auto top = Row(identifier + "_Top", 8, 20);
    statusLabel = Label(identifier + "_Status", "", 11, kTextDim);
    Grow(*statusLabel);
    // The battery as a bar; the number is in the status line.
    battery = CreateGaugeDiagramElement(identifier + "_Battery", 0, 0, 40, 8);
    battery->SetMode(GaugeMode::LinearBar);
    battery->SetOrientation(GaugeOrientation::Horizontal);
    battery->SetMinValue(0);
    battery->SetMaxValue(100);
    battery->SetShowLabels(false);
    battery->SetTrackColor(kTrack);
    battery->SetBackgroundColor(kPanelBg);
    Size(*battery, 40, 8);
    batteryLabel = Label(identifier + "_BatteryLevel", "", 11, kTextDim);
    batteryLabel->SetAlignment(TextAlignment::Right);
    Size(*batteryLabel, 38, 18);
    top->AddChild(statusLabel);
    top->AddChild(battery);
    top->AddChild(batteryLabel);
    AddChild(top);

    lockButton = Kit::Button(identifier + "_LockButton", "Lock", true, 44);
    lockButton->SetFontSize(14);
    lockButton->SetOnClick([this]() { HandleLockToggle(); });
    AddChild(lockButton);

    AddChild(Label(identifier + "_ActivityCaption", "Recent activity", 11, kTextDim, true));
    activityModel = std::make_shared<ActivityModel>(activityLog);
    activityList = std::make_shared<UltraCanvasListView>(identifier + "_Activity", 0, 0, 216, 100);
    activityList->SetModel(activityModel);
    activityList->SetShowHeader(false);
    activityList->SetRowHeight(20);
    activityList->SetAccessibleName("Recent activity");
    activityList->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(100));
    Grow(*activityList);
    AddChild(activityList);

    Sync();
}

SmartHomeLockControl::~SmartHomeLockControl() = default;

void SmartHomeLockControl::SetDevice(const std::string& id) {
    deviceId = id;
    currentState = SMARTHOME_API.GetLockState(id);
    Sync();
}

void SmartHomeLockControl::SetState(const SmartHomeLockState& state) {
    currentState = state;
    Sync();
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
    activityModel->Changed();
}

void SmartHomeLockControl::ClearActivity() {
    activityLog.clear();
    activityModel->Changed();
}

void SmartHomeLockControl::Sync() {
    // The button says what a press does; its colour says what the lock is.
    if (currentState.Jammed) {
        lockButton->SetText("Jammed");
        lockButton->SetColors(kBad, kBad, kBad, kBad);
    } else if (currentState.Locked) {
        lockButton->SetText("Unlock");
        lockButton->SetColors(kGood, Color(67, 160, 71), Color(56, 142, 60), kTrack);
    } else {
        lockButton->SetText("Lock");
        lockButton->SetColors(kWarn, Color(0xFB, 0x8C, 0x00), Color(0xF5, 0x7C, 0x00), kTrack);
    }
    std::string status = currentState.Jammed ? "Jammed - check the door"
                         : currentState.Locked ? "Locked" : "Unlocked";
    if (!currentState.LastUser.empty()) status += " · " + currentState.LastUser;
    const int level = std::clamp(static_cast<int>(currentState.BatteryLevel), 0, 100);
    statusLabel->SetText(status);
    batteryLabel->SetText(Percent(level));
    battery->SetGaugeColor(level < 20 ? kBad : kGood);
    battery->SetValue(level);
    RequestRedraw();
}

void SmartHomeLockControl::HandleLockToggle() {
    // A jammed lock is not a state to toggle out of blindly; it needs a look.
    if (currentState.Jammed) return;

    const bool wantLocked = !currentState.Locked;
    if (!deviceId.empty()) SMARTHOME_API.SetLockState(deviceId, wantLocked);
    currentState.Locked = wantLocked;
    if (wantLocked) { if (onLock) onLock(); }
    else            { if (onUnlock) onUnlock(); }
    Sync();
}

// ============================================================================
// Blind
// ============================================================================

// The blind in its window: the slats cover the top part, position 100 is
// fully open. A picture only; the controls are the buttons and sliders.
class SmartHomeBlindPreview : public UltraCanvasUIElement {
public:
    explicit SmartHomeBlindPreview(const std::string& id) : UltraCanvasUIElement(id, 0, 0, 90, 70) {}
    void Set(uint8_t pos, uint8_t tiltValue) { position = pos; tilt = tiltValue; RequestRedraw(); }

    void Render(IRenderContext* ctx, const Rect2Df&) override {
        if (!ctx || !IsVisible()) return;
        const Rect2Df b = GetLocalBounds();
        const double side = std::min<double>(b.height * 1.3, b.width);
        const Rect2Dd window(b.width / 2.0 - side / 2.0, 0.0, side, b.height);
        ctx->SetFillPaint(Color(0xBB, 0xDE, 0xFB));   // the view outside
        ctx->FillRectangle(window);

        const double covered = window.height * (1.0 - position / 100.0);
        ctx->SetFillPaint(Color(0x90, 0xA4, 0xAE));
        ctx->FillRectangle(Rect2Dd(window.x, window.y, window.width, covered));
        // Slat lines, closer together the more the slats are tilted shut.
        ctx->SetStrokePaint(Color(0x78, 0x90, 0x9C));
        ctx->SetStrokeWidth(1.0);
        const double pitch = 4.0 + 4.0 * (1.0 - std::abs(tilt - 50) / 50.0);
        for (double y = window.y + pitch; y < window.y + covered; y += pitch) {
            ctx->DrawLine(Point2Dd(window.x, y), Point2Dd(window.x + window.width, y));
        }
        ctx->SetStrokePaint(kTrack);
        ctx->DrawRectangle(window);
    }

private:
    uint8_t position = 0;
    uint8_t tilt = 50;
};

SmartHomeBlindControl::SmartHomeBlindControl(const std::string& identifier,
                                             float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    AsControl(*this, 8);

    preview = std::make_shared<SmartHomeBlindPreview>(identifier + "_Preview");
    preview->layoutItem.SetFlexGrow(1);
    preview->layoutItem.SetFlexShrink(1);
    preview->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(50));
    AddChild(preview);

    auto quick = Row(identifier + "_Quick", 6, 28);
    openButton = Kit::Button(identifier + "_Open", "Open");
    stopButton = Kit::Button(identifier + "_Stop", "Stop");
    closeButton = Kit::Button(identifier + "_Close", "Close");
    for (const auto& b : {openButton, stopButton, closeButton}) {
        Grow(*b);
        quick->AddChild(b);
    }
    openButton->SetOnClick([this]() { MoveTo(100); });
    closeButton->SetOnClick([this]() { MoveTo(0); });
    // Stop leaves the position where it is, and says so to the device.
    stopButton->SetOnClick([this]() { MoveTo(currentPosition); });
    AddChild(quick);

    std::shared_ptr<UltraCanvasLabel> positionValue;
    AddChild(CaptionRow(identifier + "_Position", "Position", positionValue));
    positionLabel = positionValue;
    position = Kit::Slider(identifier + "_PositionSlider", 0, 100, kAccent);
    position->onValueChanging = [this](float v) { positionLabel->SetText(Percent(static_cast<int>(std::lround(v)))); };
    position->onValueChanged = [this](float v) {
        if (!syncing) MoveTo(static_cast<uint8_t>(std::lround(v)));
    };
    AddChild(position);

    tiltRow = CreateContainer(identifier + "_TiltRow");
    AsColumn(*tiltRow, 2, 0);
    Height(*tiltRow, 44);
    std::shared_ptr<UltraCanvasLabel> tiltValue;
    tiltRow->AddChild(CaptionRow(identifier + "_Tilt", "Tilt", tiltValue));
    tilt = Kit::Slider(identifier + "_TiltSlider", 0, 100, kAccent);
    tilt->onValueChanged = [this, tiltValue](float v) {
        tiltValue->SetText(Percent(static_cast<int>(std::lround(v))));
        if (syncing) return;
        currentTilt = static_cast<uint8_t>(std::lround(v));
        preview->Set(currentPosition, currentTilt);
        if (onTiltChange) onTiltChange(currentTilt);
    };
    tiltRow->AddChild(tilt);
    tiltRow->SetVisible(false);
    AddChild(tiltRow);

    presetRow = Row(identifier + "_PresetRow", 6, 24);
    presetRow->SetVisible(false);   // until there is a preset
    AddChild(presetRow);

    Sync();
}

SmartHomeBlindControl::~SmartHomeBlindControl() = default;

void SmartHomeBlindControl::SetDevice(const std::string& id) {
    deviceId = id;
    if (!id.empty()) currentPosition = std::min<uint8_t>(SMARTHOME_API.GetBlindPosition(id), 100);
    Sync();
}

void SmartHomeBlindControl::SetPosition(uint8_t pos) {
    currentPosition = std::min<uint8_t>(pos, 100);
    Sync();
}

void SmartHomeBlindControl::SetTilt(uint8_t value) {
    currentTilt = std::min<uint8_t>(value, 100);
    Sync();
}

void SmartHomeBlindControl::SetTiltEnabled(bool enable) { tiltRow->SetVisible(enable); }

void SmartHomeBlindControl::AddPreset(const std::string& name, uint8_t pos) {
    const uint8_t clamped = std::min<uint8_t>(pos, 100);
    auto button = Kit::Button(GetIdentifier() + "_Preset" + std::to_string(presetRow->GetChildCount()),
                              name, false, 24);
    button->SetFontSize(10);
    Grow(*button);
    button->SetOnClick([this, clamped]() { MoveTo(clamped); });
    presetRow->AddChild(button);
    presetRow->SetVisible(true);
}

void SmartHomeBlindControl::MoveTo(uint8_t pos) {
    currentPosition = std::min<uint8_t>(pos, 100);
    if (!deviceId.empty()) SMARTHOME_API.SetBlindPosition(deviceId, currentPosition);
    if (onPositionChange) onPositionChange(currentPosition);
    Sync();
}

void SmartHomeBlindControl::Sync() {
    syncing = true;
    preview->Set(currentPosition, currentTilt);
    positionLabel->SetText(Percent(currentPosition));
    if (!position->IsDragging()) position->SetValue(currentPosition);
    if (!tilt->IsDragging()) tilt->SetValue(currentTilt);
    syncing = false;
    RequestRedraw();
}

// ============================================================================
// Sensor display - read only
// ============================================================================

double SensorHistoryMinimumSpan(SmartHomeSensorType type) {
    switch (type) {
        case SmartHomeSensorType::Temperature: return 2.0;   // °C
        case SmartHomeSensorType::Humidity:    return 10.0;  // %
        case SmartHomeSensorType::Power:       return 20.0;  // W
        case SmartHomeSensorType::Light:       return 50.0;  // lx
        default:                               return 1.0;
    }
}

SmartHomeSensorDisplay::SmartHomeSensorDisplay(const std::string& identifier,
                                               float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    AsControl(*this, 4);
    valueLabel = Label(identifier + "_Value", "-", 26, kText, true);
    AddChild(valueLabel);
    kindLabel = Label(identifier + "_Kind", "", 10, kTextDim);
    AddChild(kindLabel);
    graphData = std::make_shared<ChartDataVector>();
    graph = CreateLineChartElement(identifier + "_Graph", 0, 0, 216, 80);
    graph->SetLineColor(kAccent);
    graph->SetLineWidth(1.5f);
    graph->SetShowDataPoints(false);
    graph->SetShowValueLabels(false);
    graph->SetEnableTooltips(false);
    graph->SetBackgroundColor(kPanelBg);
    graph->SetPlotAreaColor(kPanelBg);
    // A sparkline: no axes or grid at this size; the range is in the caption.
    graph->SetShowAxes(false);
    graph->SetShowGrid(false);
    graph->SetXAxisLabelMode(XAxisLabelMode::DataLabel);
    graph->SetEmptyMessage("No readings yet");
    graph->SetEmptyMessageColor(kTextDim);
    graph->SetDataSource(graphData);
    Grow(*graph);
    AddChild(graph);
    thresholdLabel = Label(identifier + "_Thresholds", "", 9, kTextDim);
    thresholdLabel->SetVisible(false);
    AddChild(thresholdLabel);
    Sync();
}

SmartHomeSensorDisplay::~SmartHomeSensorDisplay() = default;

void SmartHomeSensorDisplay::SetDevice(const std::string& id) {
    deviceId = id;
    currentReading = SMARTHOME_API.GetSensorReading(id);
    sensorType = currentReading.Type;
    Sync();
}

void SmartHomeSensorDisplay::SetSensorType(SmartHomeSensorType type) {
    sensorType = type;
    Sync();
    RefreshGraph();
}

void SmartHomeSensorDisplay::AddReading(const SmartHomeSensorReading& reading) {
    history.push_back(reading);
    // Keep the window bounded: a sensor reporting every few seconds would
    // grow this without limit otherwise.
    constexpr size_t kMaxHistory = 2048;
    if (history.size() > kMaxHistory) {
        history.erase(history.begin(), history.begin() + static_cast<long>(history.size() - kMaxHistory));
    }
    RefreshGraph();
}

// The readings of the last graphHours hours, newest last, labelled with
// their time of day.
void SmartHomeSensorDisplay::RefreshGraph() {
    std::vector<ChartDataPoint> points;
    if (!history.empty()) {
        const uint64_t newest = history.back().Timestamp;
        const uint64_t window = static_cast<uint64_t>(graphHours) * 3600ull * 1000ull;
        size_t first = history.size() - 1;
        while (first > 0 && (newest <= window || history[first - 1].Timestamp >= newest - window)) --first;
        for (size_t i = first; i < history.size(); ++i) {
            const time_t t = static_cast<time_t>(history[i].Timestamp / 1000);
            std::tm local{};
            localtime_r(&t, &local);
            char clock[8];
            std::snprintf(clock, sizeof clock, "%02d:%02d", local.tm_hour, local.tm_min);
            points.emplace_back(static_cast<double>(points.size()), history[i].Value, 0.0, clock, 0.0);
        }
    }
    graphRange.clear();
    if (points.size() >= 2) {
        auto [lo, hi] = std::minmax_element(points.begin(), points.end(),
            [](const ChartDataPoint& a, const ChartDataPoint& b) { return a.y < b.y; });
        graphRange = Fixed1(lo->y) + "-" + Fixed1(hi->y) + ", " + std::to_string(graphHours) + " h";
    }
    graph->SetMinimumValueSpan(SensorHistoryMinimumSpan(sensorType));
    graphData->LoadFromArray(points);
    graph->SetDataSource(graphData);
    Sync();
}

void SmartHomeSensorDisplay::SetCurrentReading(const SmartHomeSensorReading& reading) {
    currentReading = reading;
    if (sensorType == SmartHomeSensorType::Unknown) sensorType = reading.Type;
    Sync();
}

void SmartHomeSensorDisplay::SetThresholds(float low, float high) {
    if (low > high) std::swap(low, high);
    lowThreshold = low;
    highThreshold = high;
    thresholdsSet = true;
    Sync();
}

void SmartHomeSensorDisplay::SetDisplayOptions(bool showGraph, bool unit, bool timestamp) {
    graph->SetVisible(showGraph);
    showUnit = unit;
    showTimestamp = timestamp;
    Sync();
}

void SmartHomeSensorDisplay::SetGraphRange(int hours) {
    graphHours = std::max(1, hours);
    RefreshGraph();
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
    if (currentReading.Triggered) return kBad;
    if (thresholdsSet && (currentReading.Value < lowThreshold ||
                          currentReading.Value > highThreshold)) {
        return kWarn;
    }
    return kText;
}

void SmartHomeSensorDisplay::Sync() {
    const std::string value = FormatValue(currentReading.Value);
    const std::string unit = GetUnitString();
    const bool numeric = value != "Triggered" && value != "Clear";
    valueLabel->SetText(value + (showUnit && numeric && !unit.empty() ? " " + unit : ""));
    valueLabel->SetTextColor(GetValueColor());
    std::string kind = SensorTypeToString(sensorType);
    if (!graphRange.empty()) kind += " · " + graphRange;
    kindLabel->SetText(kind);
    kindLabel->SetVisible(showTimestamp);
    thresholdLabel->SetText("alert below " + Fixed1(lowThreshold) + " or above " + Fixed1(highThreshold));
    thresholdLabel->SetVisible(thresholdsSet);
    RequestRedraw();
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
