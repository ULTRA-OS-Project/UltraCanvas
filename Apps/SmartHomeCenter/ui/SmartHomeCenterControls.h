// Apps/SmartHomeCenter/ui/SmartHomeCenterControls.h
// A device's controls, as framework elements: a switch for anything that
// switches, brightness and colour temperature sliders for a light that has
// them, target and mode for a thermostat, lock and unlock, a blind's
// position, a sensor's reading (and, in the demo home, buttons that stage
// its events). The Home page's inspector and the Device page each hold one.
//
// Build() makes the controls for a device; Update() writes its new state
// into them in place - a slider being dragged is left alone - and answers
// false when they no longer fit (another kind, or the device went offline),
// so the owner builds them again.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "SmartHomeCenterModel.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSwitch.h"

#include <memory>
#include <string>

namespace SmartHomeCenter {

class DeviceControls {
public:
    // `idPrefix` keeps the element ids of two instances apart.
    DeviceControls(std::string idPrefix, std::string iconsDir, bool demoHome);

    // The column the controls live in; add it once to a page.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Panel() const { return panel; }

    void Build(const DeviceView& view);
    bool Update(const DeviceView& view);
    void Clear();

private:
    std::string Key(const DeviceView& view) const;
    std::string Id(const std::string& name) const { return prefix + name; }

    std::string prefix;
    std::string iconsDir;
    bool demoHome = false;
    std::string builtFor;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> panel;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> value;          // the big figure
    std::shared_ptr<UltraCanvas::UltraCanvasSwitch> toggle;
    std::shared_ptr<UltraCanvas::UltraCanvasSlider> level;         // brightness / position
    std::shared_ptr<UltraCanvas::UltraCanvasSlider> warmth;        // colour temperature
    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl> mode;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> target;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> action;        // lock / unlock
    float pendingTarget = 0.0f;   // thermostat target being stepped
    bool updating = false;        // writing values into controls: ignore their echoes
};

} // namespace SmartHomeCenter
