// UltraCanvas/Plugins/Vector/UltraCanvasDWGDecoder.h
// Native DWG (AutoCAD Drawing) decoder - reads the binary drawing database
// of R13 through R2018 files (AC1012, AC1014, AC1015, AC1018, AC1021,
// AC1024, AC1027, AC1032) and emits the drawing as tagged ASCII DXF, the
// exchange format the DXF reader already understands. Written from the
// published structure of the format (the Open Design Alliance's "Open
// Design Specification for .dwg files"): the bit-coded value types, the
// R13-R2000 section locator layout, the R2004+ encrypted file header with
// its LZ77-compressed system and data pages, the R2007 Reed-Solomon coded
// pages, the object map, the CLASSES table for variable-type entities, and
// the per-entity field layouts. No third-party code is involved, so the
// reader ships under the framework's own licence; the GPL LibreDWG tools
// remain an optional fallback in DWGConverter for anything this decoder
// declines.
//
// Coverage: LINE, POINT, CIRCLE, ARC, ELLIPSE, LWPOLYLINE, POLYLINE (2D/3D,
// polyface and polygon meshes with their VERTEX chains), SPLINE, HATCH
// (all boundary edge types and solid/pattern/gradient flags), SOLID, TRACE,
// 3DFACE, TEXT, ATTRIB, MTEXT, LEADER, INSERT/MINSERT with block
// definitions (BLOCKS section), the seven DIMENSION types (through their
// rendered dimension blocks), plus the LAYER, LTYPE, STYLE tables and true
// colours, lineweights, linetype and visibility state. Anything else (3D solids, images, proxies, tables,
// multileaders, ...) is skipped and reported through the warning callback.
// Version: 1.0.0
// Last Modified: 2026-09-08
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace UltraCanvas {
namespace VectorConverter {

struct DWGDecodeResult {
    bool ok = false;
    std::string dxf;              // DXF text (R2000 tagged ASCII) when ok
    std::string version;          // "AC1015", ... as found in the file
    std::string error;            // why decoding failed when !ok
    unsigned entities = 0;        // model-space entities emitted
    unsigned blocks = 0;          // block definitions emitted
    unsigned skipped = 0;         // objects of unsupported types
};

// Decode the DWG file image in `data`. Non-fatal problems (unsupported
// entity types, unreadable objects) go to `warn` when set; the result's
// counters summarise them. Never throws.
DWGDecodeResult DecodeDWG(const std::string& data,
                          const std::function<void(const std::string&)>& warn = {});

// True when `head` (the first bytes of a file) carries a DWG version magic
// this decoder handles natively.
bool DWGDecoderSupportsVersion(const std::string& head);

} // namespace VectorConverter
} // namespace UltraCanvas
