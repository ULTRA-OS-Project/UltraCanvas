// include/UltraCanvasModelRaster.h
// A shaded three-quarter view of a mesh, rasterized in software.
//
// This was written for the Filer's 3D thumbnails, where it had to be software:
// the previews are produced on background worker threads, and the GL-backed
// viewer needs a window and a current context, neither of which a background
// decode has. It has no GL dependency at all - it transforms, projects,
// z-buffers and shades the triangles itself - which turns out to make it
// useful in a second place.
//
// That place is `UltraCanvasSTLElement` in a build without
// `ULTRACANVAS_ENABLE_GL`, which until now drew the words "build with
// -DULTRACANVAS_ENABLE_GL=ON for 3D preview" over a dark rectangle. The
// picture was always available; it was just sitting in another file.
//
// Deliberately not a renderer. There is no camera, no material, no texture
// and no interaction: one fixed pose, one head-light, flat shading. A build
// with GL still uses the real viewer, which is what you want the moment
// anyone drags to rotate. This is the still image for everything else.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_MODEL_RASTER_H
#define ULTRACANVAS_MODEL_RASTER_H

#include "Models/STL/UltraCanvas3DTypes.h"
#include "UltraCanvasImage.h"     // UCPixmap

#include <cstddef>
#include <memory>

namespace UltraCanvas {

// Above this, a mesh keeps its glyph rather than occupying a preview worker.
// The cost it guards is the *load*, not the raster: parsing a model into a
// Mesh3D outweighs drawing it by between four and a hundred times, and the
// raster at tile sizes is bound by triangle setup rather than by fill.
constexpr size_t kModelPreviewTriangleCap = 2000000;

// The mesh as an ARGB pixmap of `w` x `h` logical pixels, multiplied by
// `scale` for a HiDPI surface. Null when the mesh is empty, has no usable
// bounds, or is over the cap.
//
// The pose is the standard "from the front left and slightly above" that
// model viewers use, framed on the mesh's bounding sphere with a hair of
// margin. Shading is flat, computed from the triangle geometry rather than
// from stored normals - STL facet normals are often wrong or absent, and a
// preview that trusts them shows a model lit from inside. Two-sided, so a
// mesh with inconsistent winding still reads as a solid.
//
// The background is transparent, so the caller's own colour shows through.
//
// Thread-safe: it touches nothing but its arguments and its own buffers,
// which is what lets the Filer call it from several decode workers at once.
std::shared_ptr<UCPixmap> RenderMeshPreviewPixmap(const Mesh3D& mesh,
                                                  int w, int h, float scale = 1.0f);

} // namespace UltraCanvas

#endif // ULTRACANVAS_MODEL_RASTER_H
