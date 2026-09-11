// Plugins/Models/Alembic/UltraCanvasAlembicConverter.h
// Alembic (.abc) geometry read into ModelStorage::ModelDocument.
//
// Alembic is a *cache* format, not a modelling one: it is what a VFX pipeline
// hands between applications once the rigging, modifiers and simulation have
// already been evaluated. That makes it the opposite of STEP in every respect
// — baked polygon meshes rather than exact surfaces, per-frame samples rather
// than one definition — and it makes it a good format to read, because what is
// in the file is what an artist saw.
//
// This reader takes the **first time sample** of every property. An Alembic
// holding an animation therefore arrives as its first frame; the document has
// ModelAnimation and morph targets to hold the rest, and the survey records
// carrying them as the next step rather than pretending otherwise.
//
// What is read:
//   AbcGeom_Xform      the transform hierarchy, as ModelNode matrices
//   AbcGeom_PolyMesh   P, .faceIndices, .faceCounts, N, uv - n-gons preserved
//   AbcGeom_SubD       the same, read as its control cage, with a warning that
//                      the subdivided surface is not what is in the file
//   AbcGeom_FaceSet    face groups, which become one primitive per set so a
//                      per-material assignment survives
//   visible            an object marked invisible is read and flagged, not
//                      dropped - the caller decides
//
// Two conventions worth stating, because both are silent corruption when got
// wrong. Alembic winds a face's indices the opposite way round from the
// outward-normal convention this framework and OBJ use, so every face is
// reversed on import; the sample this was written against disagrees with its
// own stored normals on 1 680 of 1 681 faces if it is not. And N and uv are
// *face-varying* - one value per face corner rather than per vertex - so
// corners are de-indexed into distinct document vertices, exactly as the OBJ
// reader does for its three independent index streams.
//
// Read only. Alembic's value is as a pipeline cache, and writing one that a
// DCC will accept means matching a schema far more strictly than reading it;
// a document that needs to leave this framework has OBJ, STEP and the rest.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_ALEMBIC_CONVERTER_H
#define ULTRACANVAS_ALEMBIC_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class AlembicConverter : public ImportOnlyConverter {
public:
    ModelFormat GetFormat() const override { return ModelFormat::Alembic; }
    std::string GetFormatName() const override { return "Alembic"; }
    std::string GetFormatVersion() const override { return "Ogawa backend, AbcGeom schemas"; }
    std::vector<std::string> GetFileExtensions() const override { return {".abc"}; }
    std::string GetMimeType() const override { return "application/alembic"; }
    FormatCapabilities GetCapabilities() const override;

    std::shared_ptr<ModelStorage::ModelDocument> Import(
            const std::string& filename,
            const ConversionOptions& options = ConversionOptions()) override;
    std::shared_ptr<ModelStorage::ModelDocument> ImportFromMemory(
            const std::vector<uint8_t>& data,
            const ConversionOptions& options = ConversionOptions()) override;
    std::shared_ptr<ModelStorage::ModelDocument> ImportFromStream(
            std::istream& stream,
            const ConversionOptions& options = ConversionOptions()) override;

    bool ValidateFile(const std::string& filename) const override;
    bool ValidateData(const std::vector<uint8_t>& data) const override;
};

} // namespace ModelConverter
} // namespace UltraCanvas

#endif // ULTRACANVAS_ALEMBIC_CONVERTER_H
