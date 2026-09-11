// Plugins/Models/UltraCanvasModelMesh3D.h
// Bridge between ModelStorage::ModelDocument (the framework's universal 3D
// structure, in core) and the flat Mesh3D the STL plugin's viewer and the
// Filer's thumbnails already use.
//
// The direction of the dependency matters: this header lives on the plugin
// side and includes both, so core never has to know a plugin type exists.
// It is the migration seam — code keeps taking a Mesh3D while readers and
// writers move over to ModelDocument one format at a time.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_MODEL_MESH3D_BRIDGE_H
#define ULTRACANVAS_MODEL_MESH3D_BRIDGE_H

#include "DataFormats/UltraCanvasModelStorage.h"
#include "Models/STL/UltraCanvas3DTypes.h"

#include <string>

namespace UltraCanvas {

// Every triangle in the document, with node transforms applied, as one flat
// float mesh. This is the lossy direction by design — the viewer wants one
// buffer to upload, not a scene — so materials, texture coordinates, custom
// attributes, skins and animation do not survive it.
Mesh3D ModelDocumentToMesh3D(const ModelStorage::ModelDocument& document);

// One primitive, with no transform applied.
Mesh3D MeshPrimitiveToMesh3D(const ModelStorage::MeshPrimitive& primitive,
                             const std::string& name = "");

// A flat mesh as a single-mesh document: one scene, one node, one triangle
// primitive. Positions widen to double; nothing is lost.
ModelStorage::ModelDocument Mesh3DToModelDocument(const Mesh3D& mesh);

} // namespace UltraCanvas

#endif // ULTRACANVAS_MODEL_MESH3D_BRIDGE_H
