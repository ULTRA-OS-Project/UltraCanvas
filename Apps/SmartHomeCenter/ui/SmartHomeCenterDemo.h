// Apps/SmartHomeCenter/ui/SmartHomeCenterDemo.h
// What the demo home starts with besides its devices: four scenes and five
// automations, by device name, and a place (Berlin) for sunrise and sunset.
// The window and the command line both call it, so --scene finds "Good
// night" whether or not the window has ever run. No UI types.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

namespace SmartHomeCenter {

extern const char* const kSceneMorning;
extern const char* const kSceneEvening;
extern const char* const kSceneAway;
extern const char* const kSceneNight;

// Adds the demo scenes when the home has none, the demo automations when it
// has none, and a location when there is none - so what the person changed
// or deleted stays so. Call after the store has opened.
void SeedDemoHome();

} // namespace SmartHomeCenter
