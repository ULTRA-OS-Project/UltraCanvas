// UltraCanvas/Plugins/Vector/UltraCanvasVectorFillRule.h
// A non-zero fill for a format that only fills even-odd.
//
// The model fills a shape by its VectorStyle::ShapeFillRule. Several of the
// formats the Vector plugin writes have no non-zero fill: libcdr reads every
// CorelDRAW curve even-odd, a DXF hatch fills by odd parity (and so a DWG,
// which is the DXF converted), Xara fills even-odd. A non-zero shape whose
// subpaths wind the same way round - a ring drawn as two circles in one
// direction, a glyph whose counter runs with its outline - would come out
// with a hole there that the model does not have.
//
// The two rules differ only in which subpaths are boundaries. Filled
// non-zero, a point is inside when the windings of the subpaths around it
// sum to anything but zero; a subpath is a real edge of the fill exactly
// when that state differs on its two sides. Keeping those subpaths and
// dropping the rest gives a path that fills even-odd as the original fills
// non-zero - exactly, as long as the subpaths do not cross one another
// (nested and side-by-side, which outlines, rings and holes are). Subpaths
// that cross are reported through `exact`; the result treats each as nested
// in or apart from the others by where its first point lies.
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework
#pragma once

#include "DataFormats/UltraCanvasVectorPathOps.h"

#include <vector>

namespace UltraCanvas {
    namespace VectorConverter {

        // The subpaths of `segs` (PathOps::NormalizePath's absolute moves,
        // lines and cubics) that, filled even-odd, fill what `segs` fills
        // non-zero. Unchanged when every subpath is already a boundary.
        // `exact`, when given, is set false when subpaths cross.
        std::vector<PathOps::FlatSeg> NonZeroAsEvenOdd(const std::vector<PathOps::FlatSeg>& segs,
                                                       bool* exact = nullptr);

    } // namespace VectorConverter
} // namespace UltraCanvas
