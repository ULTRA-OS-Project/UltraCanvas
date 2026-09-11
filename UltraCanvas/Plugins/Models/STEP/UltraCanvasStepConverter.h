// Plugins/Models/STEP/UltraCanvasStepConverter.h
// STEP (ISO 10303) AP203 / AP214 / AP242 boundary representation, read into
// ModelStorage::ModelDocument::Brep and written back out of it.
//
// STEP is the format engineering actually exchanges solids in, and it carries
// no triangles at all: a part is trimmed surfaces — planes, cylinders, cones,
// spheres, tori, surfaces of revolution and extrusion, and rational NURBS —
// stitched by a topology of faces, loops, edges and vertices. That is what
// UltraCanvasBrepStorage.h exists to hold, and this is the reader that fills
// it. Nothing is tessellated on the way in unless the caller asks
// (ConversionOptions::TessellateOnImport): the exact bodies are the point, and
// a mesh made at a tolerance the file never stated is a decision only the
// caller can make.
//
// What is read:
//   geometry    cartesian_point, direction, vector, axis2_placement_2d/3d,
//               line, circle, ellipse, parabola, hyperbola, polyline,
//               b_spline_curve_with_knots and its rational form, trimmed_curve,
//               plane, cylindrical_surface, conical_surface, spherical_surface,
//               toroidal_surface, surface_of_linear_extrusion,
//               surface_of_revolution, b_spline_surface_with_knots and its
//               rational form
//   trimming    pcurve / definitional_representation through surface_curve,
//               seam_curve and intersection_curve; where a file carries none,
//               the surface is inverted instead
//   topology    vertex_point, edge_curve, oriented_edge, edge_loop, poly_loop,
//               vertex_loop, face_bound, face_outer_bound, advanced_face,
//               face_surface, closed_shell, open_shell, manifold_solid_brep,
//               brep_with_voids, shell_based_surface_model, faceted_brep
//   context     si_unit and conversion_based_unit (the file's length unit is
//               recorded, never applied — geometry stays in the numbers the
//               file used), product and product_definition names,
//               styled_item -> colour_rgb per face and per solid
//
// What is not, and is said rather than implied: assembly placements
// (next_assembly_usage_occurrence and the transformation that goes with it)
// are reported through the warning callback and not applied, so a multi-part
// file arrives with every part in its own coordinates; and PMI, tolerances and
// construction history are carried as Extras text and interpreted by nothing.
//
// Writing produces an AP203 advanced_brep_shape_representation. A document
// holding meshes rather than solids is written as a faceted b-rep — planar
// faces with shared edges, which is what STEP has for a mesh and what every
// CAD system will read back.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_STEP_CONVERTER_H
#define ULTRACANVAS_STEP_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class StepConverter : public IModelFormatConverter {
public:
    ModelFormat GetFormat() const override { return ModelFormat::STEP; }
    std::string GetFormatName() const override { return "STEP (ISO 10303)"; }
    std::string GetFormatVersion() const override { return "AP203 / AP214 / AP242, Part 21"; }
    std::vector<std::string> GetFileExtensions() const override {
        return {".step", ".stp", ".p21"};
    }
    std::string GetMimeType() const override { return "model/step"; }
    FormatCapabilities GetCapabilities() const override;

    bool CanImport() const override { return true; }
    bool CanExport() const override { return true; }

    std::shared_ptr<ModelStorage::ModelDocument> Import(
            const std::string& filename,
            const ConversionOptions& options = ConversionOptions()) override;
    std::shared_ptr<ModelStorage::ModelDocument> ImportFromMemory(
            const std::vector<uint8_t>& data,
            const ConversionOptions& options = ConversionOptions()) override;
    std::shared_ptr<ModelStorage::ModelDocument> ImportFromStream(
            std::istream& stream,
            const ConversionOptions& options = ConversionOptions()) override;

    bool Export(const ModelStorage::ModelDocument& document,
                const std::string& filename,
                const ConversionOptions& options = ConversionOptions()) override;
    bool ExportToMemory(const ModelStorage::ModelDocument& document,
                        std::vector<uint8_t>& outData,
                        const ConversionOptions& options = ConversionOptions()) override;
    bool ExportToStream(const ModelStorage::ModelDocument& document,
                        std::ostream& stream,
                        const ConversionOptions& options = ConversionOptions()) override;

    bool ValidateFile(const std::string& filename) const override;
    bool ValidateData(const std::vector<uint8_t>& data) const override;

    // A mesh has no surfaces of its own, so writing one to STEP means making
    // them: one plane per facet, with the edges shared between neighbours so
    // the result is a solid rather than a bag of loose triangles. Exposed
    // because it is useful on its own — it is also how a mesh format's
    // document becomes something a CAD system will open.
    static void FacetMeshesIntoBrep(ModelStorage::ModelDocument& document,
                                    const ConversionOptions& options = ConversionOptions());
};

} // namespace ModelConverter
} // namespace UltraCanvas

#endif // ULTRACANVAS_STEP_CONVERTER_H
