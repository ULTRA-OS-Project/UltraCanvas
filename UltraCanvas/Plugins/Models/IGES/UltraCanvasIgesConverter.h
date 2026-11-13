// Plugins/Models/IGES/UltraCanvasIgesConverter.h
// IGES 5.3 boundary representation, read into ModelStorage::ModelDocument::Brep
// and written back out of it.
//
// IGES is the format CAD exchanged surfaces in before STEP, and still the one
// many CAM, CAE and surfacing tools reach for. It carries a model in one of
// two ways, and this reader takes both:
//
//   solids     the manifold solid B-rep object (186) and the topology under
//              it: shell 514, face 510, loop 508, edge list 504, vertex list
//              502. Edges are shared between faces, so a closed part arrives
//              as a closed BrepSolid.
//   surfaces   trimmed surfaces (144), each a surface with an outer boundary
//              and holes given as curves on that surface (142); plus bounded
//              planes (108) and untrimmed surfaces that stand on their own.
//              These are what most IGES files contain, and they arrive as one
//              open BrepSolid of independent faces, as STEP's
//              shell_based_surface_model does.
//
// What is read:
//   curves      line 110, circular arc 100, ellipse arc 104 (form 1),
//               copious data 106 as a polyline (forms 11, 12, 13, 63),
//               parametric spline 112 (converted exactly to a B-spline),
//               rational B-spline 126, composite curve 102 (one edge per
//               segment), transformation matrix 124 on any of them
//   surfaces    plane 108 and 190, cylinder 192, cone 194, sphere 196,
//               torus 198, ruled 118, revolution 120, tabulated cylinder 122,
//               rational B-spline 128
//   attributes  colour (the eight standard colour numbers and colour
//               definition 314), names (name property 406 form 15, else the
//               directory label), the unit from the global section
//
// Trimming curves in a surface's parameter space (the B curve of a 142, the
// parameter curves of a 508) are used only when they agree with the edge in
// model space: they are checked at both ends and the middle against the 3D
// edge, and dropped otherwise, so a parameterisation that differs from the
// one assumed here costs a projection rather than a wrong face.
//
// What is not read, and is said: hyperbolic and parabolic conic arcs (read
// as the straight chord between their ends), offset surfaces (140), bounded
// surfaces (143), subfigure instances (408), and the compressed and binary
// forms of the file.
//
// Writing produces IGES 5.3. A closed solid is written as a manifold solid
// B-rep object with shared edges, an open one as trimmed surfaces with their
// boundaries in model space; analytic surfaces keep their analytic entities
// (190-198) and NURBS stay NURBS. A document holding meshes rather than
// solids is written as a faceted solid, one plane per facet, as the STEP
// writer does.
//
// Version: 1.0.0
// Last Modified: 2026-11-12
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_IGES_CONVERTER_H
#define ULTRACANVAS_IGES_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class IgesConverter : public IModelFormatConverter {
public:
    ModelFormat GetFormat() const override { return ModelFormat::IGES; }
    std::string GetFormatName() const override { return "IGES"; }
    std::string GetFormatVersion() const override { return "5.3"; }
    std::vector<std::string> GetFileExtensions() const override { return {".igs", ".iges"}; }
    std::string GetMimeType() const override { return "model/iges"; }
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
};

} // namespace ModelConverter
} // namespace UltraCanvas

#endif // ULTRACANVAS_IGES_CONVERTER_H
