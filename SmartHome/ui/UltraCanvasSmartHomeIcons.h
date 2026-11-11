// UltraCanvasSmartHomeIcons.h
// Which icon and colour stand for a device, for every Smart Home widget that
// shows one: the topology's nodes, the dashboard's cards. The icons are the
// line icons in media/icons/smarthome/ (white strokes, drawn on a disc of the
// device's colour), shared with SmartHome Center.
// Version: 1.0.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasSmartHome.h"

#include <string>

namespace UltraCanvas {
namespace SmartHome {

// media/icons/smarthome/ under the application's resources, with the slash.
std::string SmartHomeIconsDir();

// The icon's name ("light", "thermostat", "tv" ...): by category, by the
// kind of reading for a sensor, and for a plug or switch by what its name
// says is plugged in (a plug called "Kettle" gets the kettle). Never empty.
std::string SmartHomeDeviceIconName(const SmartHomeDeviceInfo& device,
                                    SmartHomeSensorType sensor = SmartHomeSensorType::Unknown);

// The icon's file. Asks SmartHomeAPI for a sensor's kind of reading.
std::string SmartHomeDeviceIconPath(const SmartHomeDeviceInfo& device);

// The disc colour for a category: lights amber, climate orange, security
// green, sensors blue, power slate ...
Color SmartHomeCategoryColor(SmartHomeDeviceCategory category);

} // namespace SmartHome
} // namespace UltraCanvas
