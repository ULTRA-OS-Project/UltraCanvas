// OS/Linux/UltraCanvasLinuxKeys.h
// INTERNAL: the X keysym to UCKeys table, shared by the X11 backend (XLookupKeysym)
// and the Wayland backend (xkbcommon, whose XKB_KEY_* values are the X keysyms).
// Not a public header.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasEvent.h"

namespace UltraCanvas {

    // XK_Return -> UCKeys::Enter, XK_a -> UCKeys::A, ...; Unknown for a keysym
    // the framework has no key for.
    UCKeys UCKeyFromKeysym(unsigned long keysym);

} // namespace UltraCanvas
