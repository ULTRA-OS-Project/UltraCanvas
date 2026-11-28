// Apps/ChatCube/android/ChatCubeAndroidProviders.h
// ChatCube for Android: the UltraIM providers linked into libChatCube.so.
//
// On the desktop every network is a module ChatCube loads from
// Plugins/UltraIM when it is needed. An APK has no such folder to load from,
// and a module there would carry a copy of the static core of its own - two
// UltraIMs, two event buses. So on Android the providers' archives are linked
// into the app library (Apps/ChatCube/CMakeLists.txt) and registered here,
// right after UltraIM_Initialize; UltraIM_LoadProvider then finds each one
// registered and loads nothing.
//
// Version: 0.1.0
// Last Modified: 2026-11-21
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>
#include <vector>

namespace ChatCube {

// Registers every provider this build linked in, ChatCube Direct first;
// returns the ids registered.
std::vector<std::string> RegisterLinkedProviders();

} // namespace ChatCube
