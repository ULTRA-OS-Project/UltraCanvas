// OS/Linux/UltraCanvasLinuxFonts.h
// INTERNAL: the font side of a Linux application - the default families, the
// bundled fonts registered with fontconfig, an application's own font file -
// which is fontconfig's and Pango's business, not the display's, and so is
// shared by the X11 and the Wayland backends. Not a public header.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRenderContext.h"   // FontStyle

#include <string>

namespace UltraCanvas {
namespace LinuxFonts {

    FontStyle DetectSystemFontStyle();
    FontStyle DetectMonospacedFontStyle();
    // The bundled DejaVu and emoji fonts into this process's fontconfig, and
    // a fontconfig-backed Pango font map as the default.
    void LoadBundledFonts();
    // An application font, into this process's fontconfig only.
    bool RegisterFontFile(const std::string& fontFilePath);

} // namespace LinuxFonts
} // namespace UltraCanvas
