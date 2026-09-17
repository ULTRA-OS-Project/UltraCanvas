// core/UltraCanvasGraphicsPluginSystem.cpp
// The registry's storage used to live here: three namespace-scope statics,
// defined in this translation unit. They are function-local statics on the
// class now (see the header), so that a plugin registering itself from a
// static initialiser cannot reach a registry that has not been constructed
// yet. Nothing is left to define, and the file stays so the target keeps a
// translation unit for the header to be compiled against on its own.
// Version: 2.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework

#include "UltraCanvasGraphicsPluginSystem.h"
