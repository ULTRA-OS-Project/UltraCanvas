// core/DataFormats/UltraCanvasCADPalette.cpp
// The ACI palette resolution declared in DataFormats/UltraCanvasCADPalette.h.
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasCADPalette.h"

#include <cmath>
#include <cstdint>

namespace UltraCanvas {

Color AciPaletteColor(int aci) {
    switch (aci) {
        case 1: return Color(255, 0, 0, 255);
        case 2: return Color(255, 255, 0, 255);
        case 3: return Color(0, 255, 0, 255);
        case 4: return Color(0, 255, 255, 255);
        case 5: return Color(0, 0, 255, 255);
        case 6: return Color(255, 0, 255, 255);
        case 7: return Color(0, 0, 0, 255);      // white on screen; black on page
        case 8: return Color(128, 128, 128, 255);
        case 9: return Color(192, 192, 192, 255);
        default: break;
    }
    if (aci >= 250 && aci <= 255) {
        int v = 51 + (aci - 250) * 40;   // 51..251 grey ramp
        return Color(static_cast<uint8_t>(v), static_cast<uint8_t>(v),
                     static_cast<uint8_t>(v), 255);
    }
    if (aci < 10 || aci > 249) return Color(0, 0, 0, 255);
    int idx = aci - 10;
    double hue = (idx / 10) * 15.0;              // 24 hues, 15 degrees apart
    int variant = idx % 10;
    static const double values[] = {1.0, 0.8, 0.6, 0.5, 0.3};
    double v = values[variant / 2];
    double s = (variant % 2) ? 0.5 : 1.0;
    double c = v * s, hp = hue / 60.0, x = c * (1 - std::fabs(std::fmod(hp, 2.0) - 1));
    double r = 0, g = 0, b = 0;
    if (hp < 1) { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else { r = c; b = x; }
    double m = v - c;
    auto B = [&](double f) { return static_cast<uint8_t>(std::lround((f + m) * 255)); };
    return Color(B(r), B(g), B(b), 255);
}

} // namespace UltraCanvas
