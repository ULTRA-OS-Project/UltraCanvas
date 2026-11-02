// Apps/SmartHomeCenter/ui/SmartHomeCenterControls.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SmartHomeCenterControls.h"
#include "SmartHomeCenterStyle.h"

#include <algorithm>
#include <cmath>

using namespace UltraCanvas;
using namespace UltraCanvas::SmartHome;

namespace SmartHomeCenter {

DeviceControls::DeviceControls(std::string idPrefix, std::string icons, bool demo)
    : prefix(std::move(idPrefix)), iconsDir(std::move(icons)), demoHome(demo) {
    panel = Column(Id("Controls"), 12);
    panel->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    panel->layoutItem.SetFlexShrink(0);
}

std::string DeviceControls::Key(const DeviceView& v) const {
    return v.info.DeviceId + "/" + std::to_string(static_cast<int>(v.kind)) + "/" + (v.online ? "1" : "0");
}

void DeviceControls::Clear() {
    panel->ClearChildren();
    value.reset();
    toggle.reset();
    level.reset();
    warmth.reset();
    mode.reset();
    target.reset();
    action.reset();
    builtFor.clear();
}

void DeviceControls::Build(const DeviceView& v) {
    Clear();
    builtFor = Key(v);
    const std::string id = v.info.DeviceId;
    auto& api = SmartHomeAPI::Instance();

    auto controlRow = [&](const std::string& key, const std::string& caption) {
        auto row = Row(Id("Row_" + key), 10);
        Fixed(row, -1, 32);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        auto label = Text(Id("RowLabel_" + key), caption, 14, kInk, false, 22);
        Grow(label);
        label->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        row->AddChild(label);
        panel->AddChild(row);
        return row;
    };
    auto bigValue = [&](const std::string& text) {
        value = Text(Id("Value"), text, 34, kInk, true, 46);
        panel->AddChild(value);
    };
    auto slider = [&](const std::string& key, float min, float max) {
        auto s = CreateHorizontalSlider(Id("Slider_" + key), 0, 0, 280, 28, min, max);
        s->SetColors(Color(226, 230, 239), kAccent, kSurface);
        s->SetTrackHeight(6);
        s->SetHandleSize(20);
        s->SetValueDisplay(SliderValueDisplay::NoDisplay);
        Fixed(s, -1, 28);
        s->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        panel->AddChild(s);
        return s;
    };
    auto power = [&]() {
        auto row = controlRow("power", "Power");
        toggle = MakeSwitch(Id("Toggle"), v.on);
        toggle->onStateChanged = [this, id](CheckedState, CheckedState now) {
            if (!updating) SmartHomeAPI::Instance().SetSwitchState(id, now == CheckedState::Checked);
        };
        row->AddChild(toggle);
    };
    auto buttonPair = [&](const std::string& key, const std::string& a, std::function<void()> onA,
                          const std::string& b, std::function<void()> onB) {
        auto buttons = Row(Id("Pair_" + key), 10, CSSLayout::AlignItems::Stretch);
        Fixed(buttons, -1, 40);
        buttons->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        auto first = MakeButton(Id("PairA_" + key), a, ButtonKind::Secondary);
        auto second = MakeButton(Id("PairB_" + key), b, ButtonKind::Secondary);
        Grow(first);
        Grow(second);
        first->SetOnClick(std::move(onA));
        second->SetOnClick(std::move(onB));
        buttons->AddChild(first);
        buttons->AddChild(second);
        panel->AddChild(buttons);
    };

    if (!v.online) {
        panel->AddChild(Paragraph(Id("Offline"), "This device is not answering. Check its power, or that it is in range.",
                                  13, kAlarm.fg, 2));
        panel->InvalidateLayout();
        return;
    }
    switch (v.kind) {
        case DeviceKind::Light: {
            power();
            const SmartHomeLightState light = api.GetLightState(id);
            if (light.Capabilities & static_cast<uint32_t>(SmartHomeLightCapability::Brightness)) {
                controlRow("brightness", "Brightness");
                level = slider("brightness", 1, 100);
                level->onValueChanged = [this, id](float pct) {
                    if (updating) return;
                    SmartHomeLightState state;
                    state.On = true;
                    state.Brightness = static_cast<uint8_t>(std::lround(pct * 255.0f / 100.0f));
                    SmartHomeAPI::Instance().SetLightState(id, state);
                };
            }
            if (light.Capabilities & static_cast<uint32_t>(SmartHomeLightCapability::ColorTemp)) {
                controlRow("warmth", "Colour temperature");
                warmth = slider("warmth", 2200, 6500);
                warmth->SetTrackGradient({GradientStop(0.0, Color(255, 176, 80)),
                                          GradientStop(0.5, Color(255, 236, 210)),
                                          GradientStop(1.0, Color(200, 222, 255))});
                warmth->onValueChanged = [this, id](float kelvin) {
                    if (updating) return;
                    SmartHomeLightState state = SmartHomeAPI::Instance().GetLightState(id);
                    state.On = true;
                    state.ColorTemp = static_cast<uint16_t>(std::lround(kelvin));
                    state.Red = state.Green = state.Blue = 0;   // "unchanged"
                    SmartHomeAPI::Instance().SetLightState(id, state);
                };
            }
            break;
        }
        case DeviceKind::Switch:
            if (v.metered) bigValue(FormatPower(v.power));
            power();
            break;
        case DeviceKind::Thermostat: {
            const SmartHomeThermostatState t = api.GetThermostatState(id);
            bigValue(FormatTemperature(t.CurrentTemperature));
            pendingTarget = t.TargetTemperature;
            auto row = controlRow("target", "Target");
            auto minus = MakeButton(Id("Minus"), "−", ButtonKind::Secondary, 36);
            minus->SetPadding(0, 0);
            Fixed(minus, 40, 36);
            target = Text(Id("Target"), FormatTemperature(t.TargetTemperature), 16, kInk, true, 24);
            target->SetAlignment(TextAlignment::Center);
            Fixed(target, 86, 24);
            auto plus = MakeButton(Id("Plus"), "+", ButtonKind::Secondary, 36);
            plus->SetPadding(0, 0);
            Fixed(plus, 40, 36);
            auto step = [this, id](float delta) {
                pendingTarget = std::clamp(pendingTarget + delta, 10.0f, 30.0f);
                if (target) target->SetText(FormatTemperature(pendingTarget));
                SmartHomeAPI::Instance().SetThermostatTarget(id, pendingTarget);
            };
            minus->SetOnClick([step]() { step(-0.5f); });
            plus->SetOnClick([step]() { step(0.5f); });
            row->AddChild(minus);
            row->AddChild(target);
            row->AddChild(plus);

            mode = CreateSegmentedControl(Id("Mode"), 0, 0, 280, 34);
            for (const char* m : {"Heat", "Auto", "Off"}) mode->AddSegment(m);
            Fixed(mode, -1, 34);
            mode->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            mode->onSegmentSelected = [this, id](int index) {
                if (updating) return;
                static const char* modes[] = {"heat", "auto", "off"};
                if (index >= 0 && index < 3) SmartHomeAPI::Instance().SetThermostatMode(id, modes[index]);
            };
            panel->AddChild(mode);
            break;
        }
        case DeviceKind::Lock: {
            const bool locked = !v.active;
            action = MakeButton(Id("Lock"), locked ? "Unlock" : "Lock", ButtonKind::Primary);
            action->SetIcon(iconsDir + (locked ? "unlock.svg" : "lock.svg"));
            action->SetIconSize(18, 18);
            action->SetIconSpacing(8);
            action->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            action->SetOnClick([id]() {
                const bool nowLocked = SmartHomeAPI::Instance().GetLockState(id).Locked;
                SmartHomeAPI::Instance().SetLockState(id, !nowLocked);
            });
            panel->AddChild(action);
            break;
        }
        case DeviceKind::Blind:
            bigValue(v.value);
            controlRow("position", "Position");
            level = slider("position", 0, 100);
            level->onValueChanged = [this, id](float pct) {
                if (!updating) SmartHomeAPI::Instance().SetBlindPosition(id, static_cast<uint8_t>(std::lround(pct)));
            };
            buttonPair("blind", "Open", [id]() { SmartHomeAPI::Instance().SetBlindPosition(id, 100); },
                       "Close", [id]() { SmartHomeAPI::Instance().SetBlindPosition(id, 0); });
            break;
        case DeviceKind::ClimateSensor:
            bigValue(v.value);
            break;
        case DeviceKind::BinarySensor:
            bigValue(v.state);
            value->SetFontSize(24);
            if (demoHome) {
                buttonPair("sim", "Simulate event", [id]() { SmartHomeAPI::Instance().SendCommand({id, "simulateTrigger", {}}); },
                           "Clear", [id]() { SmartHomeAPI::Instance().SendCommand({id, "simulateClear", {}}); });
            }
            break;
        default:
            panel->AddChild(Paragraph(Id("None"), "This kind of device has no controls here yet.", 13, kMuted, 1));
            break;
    }
    panel->InvalidateLayout();
    Update(v);
}

bool DeviceControls::Update(const DeviceView& v) {
    if (Key(v) != builtFor) return false;
    auto& api = SmartHomeAPI::Instance();
    const std::string& id = v.info.DeviceId;
    updating = true;
    if (toggle) toggle->SetChecked(v.on);
    if (v.kind == DeviceKind::Light) {
        const SmartHomeLightState light = api.GetLightState(id);
        if (level && !level->IsDragging()) level->SetValue(std::max(1.0f, std::round(light.Brightness * 100.0f / 255.0f)));
        if (warmth && !warmth->IsDragging() && light.ColorTemp > 0) warmth->SetValue(static_cast<float>(light.ColorTemp));
    }
    if (v.kind == DeviceKind::Switch && value) value->SetText(FormatPower(v.power));
    if (v.kind == DeviceKind::Thermostat) {
        const SmartHomeThermostatState t = api.GetThermostatState(id);
        if (value) value->SetText(FormatTemperature(t.CurrentTemperature));
        if (target) target->SetText(FormatTemperature(t.TargetTemperature));
        pendingTarget = t.TargetTemperature;
        if (mode) mode->SetSelectedIndex(t.Mode == "auto" ? 1 : t.Mode == "off" ? 2 : 0);
    }
    if (v.kind == DeviceKind::Lock && action) {
        const bool locked = !v.active;
        action->SetText(locked ? "Unlock" : "Lock");
        action->SetIcon(iconsDir + (locked ? "unlock.svg" : "lock.svg"));
    }
    if (v.kind == DeviceKind::Blind) {
        if (value) value->SetText(v.value);
        if (level && !level->IsDragging()) level->SetValue(static_cast<float>(api.GetBlindPosition(id)));
    }
    if (v.kind == DeviceKind::ClimateSensor && value) value->SetText(v.value);
    if (v.kind == DeviceKind::BinarySensor && value) {
        value->SetText(v.state);
        value->SetTextColor(v.alert ? kAlarm.fg : v.active ? kSense.fg : kInk);
    }
    updating = false;
    panel->RequestRedraw();
    return true;
}

} // namespace SmartHomeCenter
