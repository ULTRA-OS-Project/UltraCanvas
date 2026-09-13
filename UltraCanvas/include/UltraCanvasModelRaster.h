// include/UltraCanvasModelRaster.h
// A 3D model as pixels: a shaded still of a mesh, rasterized in software, and
// the whole path from a model file to an editable raster layer at a chosen
// view.
//
// This is the 3D counterpart of UltraCanvasVectorRaster.h. A model, like a
// drawing, has no pixels of its own - but where a drawing only needs a size,
// a model also needs a *view*, so `ModelViewPose` (UltraCanvas3DTypes.h) is
// part of the question. `UltraCanvasSTLElement` orbits by changing exactly
// those three numbers, so the still a caller saves is the view the user set
// on screen.
//
// Everything here is software: it transforms, projects, z-buffers and shades
// the triangles itself, with no GL dependency and no window. That is what
// lets the Filer call it from background decode workers, what makes it the
// STL element's fallback in a build without `ULTRACANVAS_ENABLE_GL`, and what
// makes "save this view as a bitmap" work the same way on every build.
//
// Deliberately not a renderer: one head-light, flat two-sided shading, one
// colour, no material and no texture. A build with GL still uses the real
// viewer for interaction.
//
// Version: 2.0.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_MODEL_RASTER_H
#define ULTRACANVAS_MODEL_RASTER_H

#include "Models/STL/UltraCanvas3DTypes.h"
#include "UltraCanvasImage.h"          // UCPixmap
#include "UltraCanvasRasterLayer.h"    // UCRasterLayer, RasterPixel

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// Above this, a mesh keeps its glyph rather than occupying a preview worker.
// The cost it guards is the *load*, not the raster: parsing a model into a
// Mesh3D outweighs drawing it by between four and a hundred times, and the
// raster at tile sizes is bound by triangle setup rather than by fill.
constexpr size_t kModelPreviewTriangleCap = 2000000;

// The colour a model is drawn in when a caller expresses no preference — the
// same light blue-grey `UltraCanvasSTLElement` uses, so a still matches the
// viewer it was framed in.
inline const Vec3 kModelDefaultColor{0.78f, 0.80f, 0.85f};

// ===== A MESH AS A PICTURE =====

// The mesh as an ARGB pixmap of `w` x `h` logical pixels, multiplied by
// `scale` for a HiDPI surface. Null when the mesh is empty, has no usable
// bounds, or is over the cap.
//
// The pose is the standard "from the front left and slightly above" that
// model viewers use, framed on the mesh's bounding sphere with a hair of
// margin. Shading is flat, computed from the triangle geometry rather than
// from stored normals — STL facet normals are often wrong or absent, and a
// preview that trusts them shows a model lit from inside. Two-sided, so a
// mesh with inconsistent winding still reads as a solid.
//
// The background is transparent, so the caller's own colour shows through.
//
// Thread-safe: it touches nothing but its arguments and its own buffers,
// which is what lets the Filer call it from several decode workers at once.
std::shared_ptr<UCPixmap> RenderMeshPreviewPixmap(const Mesh3D& mesh,
                                                  int w, int h, float scale = 1.0f);

// The same picture from a pose the caller chooses, in a colour the caller
// chooses. The camera matches `UltraCanvasSTLElement`'s — the model is
// normalised to a unit radius, rotated by yaw then pitch, and seen through a
// 45° perspective from `pose.distance` radii away — so what the viewer shows
// and what this returns are the same view.
//
// Same guarantees as above: transparent background, flat two-sided shading,
// thread-safe, null for an empty or oversized mesh.
std::shared_ptr<UCPixmap> RenderMeshPixmap(const Mesh3D& mesh, int w, int h,
                                           const ModelViewPose& pose,
                                           const Vec3& modelColor = kModelDefaultColor,
                                           float scale = 1.0f);

// ===== A MODEL FILE AS AN EDITABLE LAYER =====

// What a model file says about itself, without rendering it.
struct ModelSourceInfo {
    bool ok = false;               // this build can read it and it holds triangles
    size_t triangleCount = 0;
    size_t vertexCount = 0;
    BoundingBox3D bounds;          // in the file's own units
    std::string error;             // why `ok` is false
};

// How to turn it into pixels. The size is the caller's — a model has no
// natural one — and so is the view.
struct ModelRasterOptions {
    int width = 1024;
    int height = 768;
    ModelViewPose pose;
    // Painted under the model. Transparent by default, so a model dropped on
    // an image keeps whatever is beneath it.
    RasterPixel background = RasterPixel(0, 0, 0, 0);
    Vec3 modelColor = kModelDefaultColor;
    // Refuses anything larger, so a mistyped size cannot ask for a 40 GB
    // buffer. Counted on the target size, not the model.
    size_t maxPixels = 256u * 1024u * 1024u;
};

// True for an extension this build can turn into geometry — the cheap check a
// drop handler or a file dialog makes before doing any work. It answers for
// the *extension*: only InspectModelFile() opens the file.
//
// Which formats those are depends on the build and on the application: core
// reads STL by itself, and OBJ / COLLADA / FBX / X3D / Alembic / MilkShape /
// .blend / STEP arrive when the Models plugin is registered
// (UltraCanvasModelPreview.h is the seam).
bool IsModelGraphicsPath(const std::string& path);

// Every extension IsModelGraphicsPath() accepts in this build, lowercase and
// without a dot. A runtime answer, for a file filter that means it.
std::vector<std::string> GetModelRasterExtensions();

// Reads the model far enough to report its size and extent.
ModelSourceInfo InspectModelFile(const std::string& path);

// Reads `path` and renders it into a fresh layer from `options.pose`, or
// returns null and fills `error`. The layer is named after the file, so it
// reads sensibly in a layer list.
std::shared_ptr<UCRasterLayer> RasterizeModelFile(const std::string& path,
                                                  const ModelRasterOptions& options,
                                                  std::string& error);

// The same for a mesh already in hand — the path a viewer takes when the user
// has been orbiting the model it already loaded, so the file is not read a
// second time.
std::shared_ptr<UCRasterLayer> RasterizeMesh(const Mesh3D& mesh,
                                             const ModelRasterOptions& options,
                                             std::string& error);

} // namespace UltraCanvas

#endif // ULTRACANVAS_MODEL_RASTER_H
