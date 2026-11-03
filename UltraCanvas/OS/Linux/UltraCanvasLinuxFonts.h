// OS/Linux/UltraCanvasLinuxFonts.h
// INTERNAL: the font side of a Linux application - the default families, the
// bundled fonts registered with fontconfig, an application's own font file -
// which is fontconfig's and Pango's business, not the display's, and so is
// shared by the X11 and the Wayland backends. Not a public header.
// Version: 1.1.0 - LoadBundledFonts says how many it added; RegisterPlatformBundledFonts calls it
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRenderContext.h"   // FontStyle

#include <string>

namespace UltraCanvas {
namespace LinuxFonts {

    FontStyle DetectSystemFontStyle();
    FontStyle DetectMonospacedFontStyle();
    // The bundled fonts into this process's fontconfig, and a
    // fontconfig-backed Pango font map as the default; how many font files
    // were added. RegisterPlatformBundledFonts on Linux.
    int LoadBundledFonts();
    // An application font, into this process's fontconfig only.
    bool RegisterFontFile(const std::string& fontFilePath);

} // namespace LinuxFonts
} // namespace UltraCanvas
