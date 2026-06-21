// include/Plugins/Charts/UltraCanvasHexLayout.h
// Pointy-top hexagonal grid layout math (header-only, dependency-free) used by
// the hexbin / hexagonal heatmap. Offset coordinates: odd rows are shifted right
// by half a hex width. Pure double arithmetic so it can be unit-tested without
// any UI dependency.
//
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
#pragma once

#include <cmath>
#include <algorithm>

namespace UltraCanvas {

    struct HexLayout {
        double size = 0.0;      // hexagon circumradius (centre -> vertex)
        double w = 0.0;         // hex width = sqrt(3) * size (pointy-top)
        double rowStep = 0.0;   // vertical centre-to-centre = 1.5 * size
        double originX = 0.0;   // top-left of the centred grid bounding box
        double originY = 0.0;
        int cols = 0;
        int rows = 0;
    };

// Fit a cols x rows pointy-top hex grid inside the given rectangle, centred.
    inline HexLayout MakeHexLayout(double areaX, double areaY, double areaW, double areaH,
                                   int cols, int rows) {
        HexLayout L;
        L.cols = cols;
        L.rows = rows;
        if (cols <= 0 || rows <= 0 || areaW <= 0.0 || areaH <= 0.0) return L;

        const double sqrt3 = std::sqrt(3.0);
        // Width spans (cols + 0.5) hex widths (the 0.5 is the odd-row offset).
        double sizeFromW = areaW / ((cols + 0.5) * sqrt3);
        // Height spans 1.5*rows + 0.5 size units.
        double sizeFromH = areaH / (1.5 * rows + 0.5);
        L.size = std::min(sizeFromW, sizeFromH);
        L.w = sqrt3 * L.size;
        L.rowStep = 1.5 * L.size;

        double gridW = (cols + 0.5) * L.w;
        double gridH = (1.5 * rows + 0.5) * L.size;
        L.originX = areaX + (areaW - gridW) / 2.0;
        L.originY = areaY + (areaH - gridH) / 2.0;
        return L;
    }

// Centre of the hexagon at offset coordinate (col, row).
    inline void HexCenter(const HexLayout& L, int col, int row, double& cx, double& cy) {
        double offset = (row & 1) ? (L.w * 0.5) : 0.0;
        cx = L.originX + L.w * 0.5 + col * L.w + offset;
        cy = L.originY + L.size + row * L.rowStep;
    }

// Find the hex cell whose centre is nearest to (px, py). For a hex grid the
// nearest centre is exactly the containing hexagon. Returns false if the point
// falls outside the grid (farther than one circumradius from every centre).
    inline bool HexNearestCell(const HexLayout& L, double px, double py, int& outCol, int& outRow) {
        if (L.size <= 0.0 || L.rowStep <= 0.0) return false;

        int r0 = static_cast<int>(std::lround((py - L.originY - L.size) / L.rowStep));
        double best = 1e300;
        int bc = -1, br = -1;
        for (int r = r0 - 1; r <= r0 + 1; ++r) {
            if (r < 0 || r >= L.rows) continue;
            double offset = (r & 1) ? (L.w * 0.5) : 0.0;
            int c0 = static_cast<int>(std::lround((px - L.originX - L.w * 0.5 - offset) / L.w));
            for (int c = c0 - 1; c <= c0 + 1; ++c) {
                if (c < 0 || c >= L.cols) continue;
                double cx, cy;
                HexCenter(L, c, r, cx, cy);
                double dx = px - cx, dy = py - cy;
                double d = dx * dx + dy * dy;
                if (d < best) { best = d; bc = c; br = r; }
            }
        }
        if (bc < 0) return false;
        if (best > L.size * L.size) return false; // outside the grid
        outCol = bc;
        outRow = br;
        return true;
    }

} // namespace UltraCanvas
