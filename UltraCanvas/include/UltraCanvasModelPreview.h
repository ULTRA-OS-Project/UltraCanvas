// include/UltraCanvasModelPreview.h
// The seam that lets core display a 3D format it cannot itself read.
//
// Core carries exactly one model reader: STL, because the Filer's thumbnails
// and the media viewer's 3D pane have always needed one and it has no external
// dependencies. Every other format - OBJ, COLLADA, FBX, X3D/VRML, Alembic,
// MilkShape, .blend, STEP - lives in the Models plugin, which links *against*
// core. So core cannot call the plugin, and a viewer that hard-codes "stl"
// stays blind to nine formats the framework can already read.
//
// This inverts that without inverting the dependency. Core declares what it
// wants - "turn this path into a Mesh3D", "is this extension one you read" -
// and `RegisterModelFormatsPlugin()` installs an implementation on the way in.
// No provider means the answers fall back to STL, which is what a build with
// ULTRACANVAS_PLUGIN_MODELS=OFF gets and what every caller got before.
//
// It is deliberately narrow. A provider hands back a flat `Mesh3D`, not a
// `ModelDocument`: core has no idea that type exists, and a thumbnail wants
// one triangle buffer rather than a scene. Anything richer belongs on the
// plugin side of the line - see `Plugins/Models/UltraCanvasModelMesh3D.h`,
// which is where the lossy flattening is defined and documented.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_MODEL_PREVIEW_H
#define ULTRACANVAS_MODEL_PREVIEW_H

#include "Models/STL/UltraCanvas3DTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas {

// What a provider must answer. Both calls happen on the Filer's background
// preview workers as well as on the UI thread, so an implementation must be
// safe to call from several threads at once - the Models plugin's is, because
// a converter is constructed per call and shares nothing.
struct ModelPreviewProvider {
    // Every extension this provider reads, lowercase and without a dot. Used
    // to decide whether a file is a model at all, so it must not include
    // extensions the provider would then refuse.
    std::function<std::vector<std::string>()> Extensions;

    // Reads `path` into a flat triangle mesh. False when the file cannot be
    // read, is empty, or holds no triangles at all - a STEP file of pure
    // B-rep solids reads successfully and yields nothing, and that is a false
    // rather than an error.
    std::function<bool(const std::string& path, Mesh3D& out)> Load;

    bool Valid() const { return Extensions && Load; }
};

// Installs the provider. Called by RegisterModelFormatsPlugin(); an
// application that registers no plugins never calls it and keeps the STL-only
// behaviour. Passing an invalid provider clears it.
void SetModelPreviewProvider(ModelPreviewProvider provider);

// ===== WHAT CORE ASKS =====
//
// These are the calls the Filer and the media viewer make. Each folds the
// provider together with core's own STL support, so a caller never has to ask
// whether a plugin is present.

// True when something in this build can turn the extension into geometry:
// the provider, or core's STL loader. `extension` may carry a leading dot and
// any case, or be a whole path.
bool CanPreviewModelExtension(const std::string& extensionOrPath);

// Every extension CanPreviewModelExtension answers true for, lowercase and
// without a dot, sorted and deduplicated. For file dialogs and for the "what
// can this build open" question a UI wants to answer honestly.
std::vector<std::string> PreviewableModelExtensions();

// Reads a model into a flat mesh, through the provider where it claims the
// extension and through core's STL loader otherwise. False when nothing in
// this build reads it, or when what it read holds no triangles.
bool LoadModelPreviewMesh(const std::string& path, Mesh3D& out);

} // namespace UltraCanvas

#endif // ULTRACANVAS_MODEL_PREVIEW_H
