// include/DataFormats/UltraCanvasCADPalette.h
// The AutoCAD Color Index (ACI) palette.
//
// ACI is how DXF and DWG name colours: an index into a fixed 256-entry palette
// rather than an RGB triple. Both the 2D vector converters and the 3D model
// converters have to resolve it, and neither owns it — so it lives here, as a
// core service, per the Masterfile rule that framework-wide facilities are
// never implemented inside a file-type plugin.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_CAD_PALETTE_H
#define ULTRACANVAS_CAD_PALETTE_H

#include "UltraCanvasCommonTypes.h"

namespace UltraCanvas {

// The colour an ACI index resolves to: the exact classic colours 1-9, the
// 250-255 grey ramp, and the standard 24-hue construction for 10-249. Anything
// outside 1-255 is black, which is what index 7 (white on screen) also means
// on a page.
//
// Shared between the DXF/DWG readers, which resolve entity and layer colours,
// and the DXF writer's nearest-ACI fallback, so colours round-trip
// consistently through ACI-only consumers.
Color AciPaletteColor(int aci);

} // namespace UltraCanvas

#endif // ULTRACANVAS_CAD_PALETTE_H
