// include/UltraCanvasMenuRegistry.h
// The command registry's former name. UltraCanvasMenuRegistry became
// UltraCanvasCommandRegistry when it began to build toolbars as well as menus;
// this header keeps code written against the old name compiling. New code
// includes UltraCanvasCommandRegistry.h and uses the new name.
// Version: 2.0.0 - an alias of UltraCanvasCommandRegistry
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommandRegistry.h"

namespace UltraCanvas {

    using UltraCanvasMenuRegistry = UltraCanvasCommandRegistry;

} // namespace UltraCanvas
