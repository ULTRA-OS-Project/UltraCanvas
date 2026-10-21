// Apps/SmartHomeCenter/ui/SmartHomeCenterPaths.h
// Where SmartHome Center keeps the home's store: the per-user data
// directory (%LOCALAPPDATA%, ~/Library/Application Support, or
// $XDG_DATA_HOME / ~/.local/share), in ULTRA/SmartHome/. The demo home has
// a file of its own, so simulated devices never mix with a real home.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace SmartHomeCenter {

// UTF-8; empty when there is no home directory to put it in.
std::string DefaultStorePath(bool demoHome);

} // namespace SmartHomeCenter
